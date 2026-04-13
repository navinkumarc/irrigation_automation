// CommManager.h - Central communication manager
//
// CommManager owns ALL communication concerns. MasterController.ino knows
// only this class. It never touches individual modules, queues, or channels.
//
// What CommManager owns:
//   Transport modules   — BLE, LoRa, WiFi, ModemBase, ModemSMS, ModemPPPoS,
//                         MQTTComm, HTTPComm
//   NetworkRouter       — bearer selection and fallback (PPPoS / WiFi)
//   NodeCommunication   — LoRa node messaging (OPEN/CLOSE valves, telemetry)
//   UserCommunication   — user ↔ controller messaging via IChannelAdapter
//   MessageQueue        — inbound raw message ring buffer (LoRa, general)
//   MessageFormats      — compact message format helpers
//   IChannelAdapter /   — adapter pattern for outbound channels
//   ChannelAdapters
//   ChannelMessage      — uniform inbound message struct
//
// MasterController.ino API (the ONLY surface the application touches):
//   commMgr.begin()     — initialize everything
//   commMgr.process()   — call every loop(); drives all polling & background
//   commMgr.notify*()   — application events (schedule started, valve, error…)
//   commMgr.getStatus() — flat SystemStatus snapshot for display / diagnostics
//   commMgr.isHealthy() — heap / system health check

#ifndef COMM_MANAGER_H
#define COMM_MANAGER_H

#include <Arduino.h>
#include <vector>
#include "Config.h"

// ─── Comm-layer sub-modules (all internal to CommManager) ─────────────────────
#include "IChannelAdapter.h"
#include "ChannelAdapters.h"
#include "ChannelMessage.h"
#include "MessageQueue.h"
#include "MessageFormats.h"
#include "NetworkRouter.h"
#include "NodeCommunication.h"
#include "UserCommunication.h"

// Transport module includes — compiled only when flags are set
#if ENABLE_BLE
  #include "BLEComm.h"
  extern BLEComm bleComm;
#endif
#if ENABLE_LORA
  #include "LoRaComm.h"
  extern LoRaComm loraComm;
#endif
#if ENABLE_WIFI
  #include "WiFiComm.h"
  extern WiFiComm wifiComm;
#endif
#if ENABLE_MODEM
  #include "ModemBase.h"
#endif
#if ENABLE_SMS
  #include "ModemSMS.h"
  extern ModemSMS modemSMS;
#endif
#if ENABLE_PPPOS
  #include "ModemPPPoS.h"
  extern ModemPPPoS modemPPPoS;
#endif
#if ENABLE_MQTT
  #include "MQTTComm.h"
  extern MQTTComm mqtt;
#endif
#if ENABLE_HTTP
  #include "HTTPComm.h"
  extern HTTPComm httpComm;
#endif

// Forward declaration — Schedule is defined in Config.h
struct Schedule;

// ─── CommManagerStatus — init result per sub-module ──────────────────────────

struct CommManagerStatus {
  bool bleOk           = false;
  bool loraOk          = false;
  bool wifiOk          = false;
  bool modemOk         = false;
  bool smsOk           = false;
  bool ppposOk         = false;
  bool networkRouterOk = false;
  bool mqttOk          = false;
  bool httpOk          = false;
  bool nodeCommOk      = false;
  bool userCommOk      = false;

  int totalModules      = 0;
  int successfulModules = 0;

  String getStatusString() const {
    return String(successfulModules) + "/" + String(totalModules) + " modules ready";
  }
};

// ─── Application events MasterController can report ─────────────────────────

enum class CommEvent {
  SCHEDULE_STARTED,
  SCHEDULE_COMPLETED,
  SCHEDULE_FAILED,
  VALVE_ACTION,
  SYSTEM_ERROR,
  SYSTEM_WARNING
};

// ─── CommManager ──────────────────────────────────────────────────────────────

class CommManager {
private:
  // ── Init status ────────────────────────────────────────────────────────────
  CommManagerStatus initStatus;
  int               stepCounter = 0;
  bool              initialized = false;

  // ── Sub-module instances owned by CommManager ──────────────────────────────
  // NodeCommunication and UserCommunication live here, not in .ino
  NodeCommunication nodeComm;
  UserCommunication userComm;
  NetworkRouter     networkRouter;

  // MessageQueue lives here — not visible to MasterController
  MessageQueue incomingQueue;

  // ── Channel adapters (owned, registered into userComm) ─────────────────────
#if ENABLE_SMS
  SMSChannelAdapter  *smsAdapter  = nullptr;
#endif
#if ENABLE_MQTT
  MQTTChannelAdapter *mqttAdapter = nullptr;
#endif
#if ENABLE_BLE
  BLEChannelAdapter  *bleAdapter  = nullptr;
#endif
#if ENABLE_LORA && ENABLE_LORA_USER_COMM
  LoRaChannelAdapter *loraAdapter = nullptr;
#endif

  // ── Application state refs — set in begin() ───────────────────────────────
  std::vector<Schedule> *schedules      = nullptr;
  bool                  *scheduleRunning = nullptr;
  bool                  *scheduleLoaded  = nullptr;

  // ── Private init steps ─────────────────────────────────────────────────────
#if ENABLE_BLE
  bool initBLE();
#endif
#if ENABLE_LORA
  bool initLoRa();
#endif
#if ENABLE_WIFI
  bool initWiFi();
#endif
#if ENABLE_MODEM
  bool initModem();
#endif
#if ENABLE_SMS
  bool initSMS();
#endif
#if ENABLE_PPPOS
  bool initPPPoS();
#endif
  bool initNetworkRouter();
#if ENABLE_MQTT
  bool initMQTT();
#endif
#if ENABLE_HTTP
  bool initHTTP();
#endif
  bool initNodeCommunication();
  bool initUserCommunication();

  // ── Inbound channel pollers — called from process() ───────────────────────
  void pollSMS();
  void pollMQTT();
  void pollHTTP();
  void pollLoRa();

  // ── Internal helpers ───────────────────────────────────────────────────────
  SystemStatus     buildSystemStatus() const;
  void             deliverMessage(const ChannelMessage &msg);
  void             printStepHeader (const String &name);
  void             printStepSuccess(const String &name);
  void             printStepFailure(const String &name, const String &reason = "");
  void             printSummary();

public:
  CommManager();

  // ── Lifecycle ──────────────────────────────────────────────────────────────

  // Initialize all communication sub-modules.
  // Must be called once in setup() after storage/config are loaded.
  // Stores refs to schedule state so process() can build SystemStatus.
  CommManagerStatus begin(std::vector<Schedule> *schedules,
                          bool *scheduleRunning,
                          bool *scheduleLoaded,
                          const String &adminPhone);

  // Drive all communication work: poll channels, run background tasks,
  // feed PPP stack, auto-reconnect. Call every loop().
  void process();

  // ── Application → CommManager event notifications ─────────────────────────
  // MasterController calls these; CommManager decides which channels to use.

  void notifyScheduleStarted  (const String &scheduleId);
  void notifyScheduleCompleted(const String &scheduleId);
  void notifyScheduleFailed   (const String &scheduleId, const String &reason);
  void notifyValveAction      (int nodeId, const String &valve, const String &action);
  void notifySystemError      (const String &message);
  void notifySystemWarning    (const String &message);

  // Send a free-form alert on all available channels.
  void sendAlert(const String &message, const String &severity = "INFO");

  // ── Status / diagnostics ──────────────────────────────────────────────────

  SystemStatus       getStatus()       const;
  CommManagerStatus  getInitStatus()   const;
  bool               isHealthy()       const;
  String             getHealthStatus() const;
  bool               isFullyInitialized() const;

  void printStatus()        const;
  void printBriefStatus()   const;
  void printDiagnostics()   const;
  String getStatusJSON()    const;

  // ── Node command wiring ────────────────────────────────────────────────────
  // MasterController registers a callback so USER "NODE x cmd" commands
  // reach the ScheduleManager / hardware layer.
  void setNodeCommandCallback(NodeCommandCallback cb);

  // Send a command directly to a node via LoRa.
  // Called by the application's handleNodeCommand callback.
  bool sendNodeCommand(int nodeId, const String &command);

  // Expose UserCommunication pointer for modules (e.g. ScheduleManager)
  // that need to send alerts. Returns nullptr if not initialized.
  UserCommunication* getUserComm();
};

// ─── Global instance ──────────────────────────────────────────────────────────
extern CommManager commMgr;

#endif // COMM_MANAGER_H
