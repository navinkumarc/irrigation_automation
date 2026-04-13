// UserCommunication.h - Centralized user communication and diagnostics
// All optional communication modules are loosely coupled via #if guards and setters.
// Removing any module's .h/.cpp files is safe as long as its ENABLE_ flag is 0.
#ifndef USER_COMMUNICATION_H
#define USER_COMMUNICATION_H

#include <Arduino.h>
#include <vector>
#include <functional>
#include "Config.h"

// Optional module includes — only compiled when their flag is enabled.
#if ENABLE_SMS
  #include "ModemSMS.h"
#endif
#if ENABLE_MQTT
  #include "MQTTComm.h"
#endif
#if ENABLE_HTTP
  #include "HTTPComm.h"
#endif
#if ENABLE_BLE
  #include "BLEComm.h"
#endif
#if ENABLE_LORA
  #include "LoRaComm.h"
#endif
#if ENABLE_WIFI
  #include "WiFiComm.h"
#endif

// Forward declarations
class CommSetup;
struct Schedule;

// ─── Result & Status Structures ───────────────────────────────────────────────

struct CommandResult {
  bool   success;
  String response;
  String commandType;
};

struct SystemStatusReport {
  bool   bleConnected;
  bool   loraInitialized;
  bool   wifiConnected;
  bool   modemReady;
  bool   smsReady;
  bool   mqttConnected;
  bool   httpReady;

  String   systemTime;
  String   uptime;
  int      successfulSchedules;
  int      failedSchedules;
  bool     scheduleRunning;
  String   currentSchedule;

  uint32_t freeHeap;
  uint32_t totalHeap;

  String   ipAddress;
  String   wifiSSID;
  int      signalStrength;
};

typedef std::function<bool(int nodeId, const String &command)> NodeCommandCallback;

// ─── UserCommunication ────────────────────────────────────────────────────────

class UserCommunication {
private:
  // Optional module pointers — only declared when their flag is enabled.
#if ENABLE_SMS
  ModemSMS  *smsComm;
#endif
#if ENABLE_MQTT
  MQTTComm  *mqttComm;
#endif
#if ENABLE_HTTP
  HTTPComm  *httpComm;
#endif
#if ENABLE_BLE
  BLEComm   *bleComm;
#endif
#if ENABLE_LORA
  LoRaComm  *loraComm;
#endif
#if ENABLE_WIFI
  WiFiComm  *wifiComm;
#endif

  CommSetup           *commSetup;
  NodeCommandCallback  nodeCommandCallback;
  String               adminPhone;

  // Status helpers
  SystemStatusReport gatherSystemStatus(std::vector<Schedule> *schedules, bool scheduleRunning);
  String formatStatusAsText(const SystemStatusReport &status);
  String formatStatusAsJSON(const SystemStatusReport &status);
  String formatStatusAsBrief(const SystemStatusReport &status);

  // Command handlers
  CommandResult handleStatusCommand();
  CommandResult handleDiagnosticsCommand();
  CommandResult handleSchedulesCommand(std::vector<Schedule> *schedules);
  CommandResult handleStopCommand(bool *scheduleRunning, bool *scheduleLoaded);
  CommandResult handleStartCommand(const String &schedId);
  CommandResult handleSMSOnCommand(bool *enableSMSBroadcast);
  CommandResult handleSMSOffCommand(bool *enableSMSBroadcast);
  CommandResult handleCheckCommand();
  CommandResult handleNodeCommand(const String &cmd);
  CommandResult handleHelpCommand();
  CommandResult handleStatsCommand();
  CommandResult handleReportCommand();

  CommandResult routeCommand(const String &cmd, std::vector<Schedule> *schedules,
                             bool *scheduleRunning, bool *scheduleLoaded,
                             bool *enableSMSBroadcast);

  void sendResponse(const String &response, const String &channel);
  void sendMultiChannelResponse(const String &response);

public:
  UserCommunication();

  // ── Initialization ────────────────────────────────────────────────────────
  // Core init — only requires admin phone and optional CommSetup reference.
  void init(const String &adminPhoneNum, CommSetup *setup = nullptr);

  // Module setters — call after init() for each enabled module.
  // Each setter is only compiled when its flag is enabled, so removing
  // the module's files (with the flag off) will not break compilation.
#if ENABLE_SMS
  void setSMS(ModemSMS *sms);
#endif
#if ENABLE_MQTT
  void setMQTT(MQTTComm *mqtt);
#endif
#if ENABLE_HTTP
  void setHTTP(HTTPComm *http);
#endif
#if ENABLE_BLE
  void setBLE(BLEComm *ble);
#endif
#if ENABLE_LORA
  void setLoRa(LoRaComm *lora);
#endif
#if ENABLE_WIFI
  void setWiFi(WiFiComm *wifi);
#endif

  void setNodeCommandCallback(NodeCommandCallback callback);

  // ── Diagnostics ───────────────────────────────────────────────────────────
  void printSystemStatus(std::vector<Schedule> *schedules, bool scheduleRunning);
  void printBriefStatus(std::vector<Schedule> *schedules, bool scheduleRunning);
  void printCommStatus();
  void printSystemDiagnostics();
  void printNetworkDiagnostics();
  void printLoRaDiagnostics();
  void printBLEDiagnostics();
  void printScheduleStatus(std::vector<Schedule> *schedules, bool scheduleRunning);

  SystemStatusReport getSystemStatus(std::vector<Schedule> *schedules, bool scheduleRunning);
  String getFormattedStatus(std::vector<Schedule> *schedules, bool scheduleRunning,
                            const String &format = "text");
  String getStatusJSON(std::vector<Schedule> *schedules, bool scheduleRunning);

  // ── Notifications / Alerts ────────────────────────────────────────────────
  void broadcastSystemStatus(std::vector<Schedule> *schedules, bool scheduleRunning);
  void sendAlert(const String &alertMessage, const String &severity = "INFO");

  // ── Event Callbacks ───────────────────────────────────────────────────────
  void notifyScheduleUpdate(const String &scheduleName, const String &status);
  void onScheduleStarted(const String &scheduleId);
  void onScheduleCompleted(const String &scheduleId);
  void onScheduleFailed(const String &scheduleId, const String &reason);
  void onValveAction(int nodeId, const String &valve, const String &action);
  void onSystemError(const String &errorMessage);
  void onSystemWarning(const String &warningMessage);

  // ── Health ────────────────────────────────────────────────────────────────
  bool   isSystemHealthy();
  String getHealthStatus();

  // ── Command Processing ────────────────────────────────────────────────────
  void processAllChannels(std::vector<Schedule> *schedules, bool *scheduleRunning,
                          bool *scheduleLoaded, bool *enableSMSBroadcast);

  void processSMSCommands(std::vector<Schedule> *schedules, bool *scheduleRunning,
                          bool *scheduleLoaded, bool *enableSMSBroadcast);
  void processLoRaCommands(std::vector<Schedule> *schedules, bool *scheduleRunning,
                           bool *scheduleLoaded, bool *enableSMSBroadcast);
  void processMQTTCommands(std::vector<Schedule> *schedules, bool *scheduleRunning,
                           bool *scheduleLoaded, bool *enableSMSBroadcast);
  void processWiFiCommands(std::vector<Schedule> *schedules, bool *scheduleRunning,
                           bool *scheduleLoaded, bool *enableSMSBroadcast);
  void processHTTPCommands(std::vector<Schedule> *schedules, bool *scheduleRunning,
                           bool *scheduleLoaded, bool *enableSMSBroadcast);
  void processBLECommand(int nodeId, const String &command);
  void processSerialCommand(const String &input, std::vector<Schedule> *schedules,
                            bool *scheduleRunning, bool *scheduleLoaded);
  void processMessage(const String &message);

  void sendCommandResponse(const String &command, const CommandResult &result,
                           const String &channel = "AUTO");
  String getHelpText();
};

#endif // USER_COMMUNICATION_H
