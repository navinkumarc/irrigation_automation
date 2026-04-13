// CommSetup.h - Communication module initialization and channel polling
//
// Owns:
//   • Initializing every enabled transport module (BLE, LoRa, WiFi, Modem, MQTT, HTTP)
//   • Instantiating and registering IChannelAdapter objects into UserCommunication
//   • Polling each channel for inbound messages and delivering them as
//     ChannelMessages to userComm.onMessageReceived()
//   • Calling processBackground() on each module that needs it
//
// UserCommunication is NOT responsible for any of the above.

#ifndef COMM_SETUP_H
#define COMM_SETUP_H

#include <Arduino.h>
#include "Config.h"
#include "ChannelAdapters.h"   // Concrete IChannelAdapter implementations
#include "ChannelMessage.h"
#include "NetworkRouter.h"

// Optional transport module includes — only when flags are enabled
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

#include "NodeCommunication.h"
#include "UserCommunication.h"

extern NodeCommunication nodeComm;
extern UserCommunication userComm;
extern NetworkRouter     networkRouter;

// ─── CommSetupStatus ──────────────────────────────────────────────────────────

struct CommSetupStatus {
  bool bleOk          = false;
  bool loraOk         = false;
  bool wifiOk         = false;
  bool modemOk        = false;
  bool smsOk          = false;
  bool ppposOk        = false;
  bool networkRouterOk= false;
  bool mqttOk         = false;
  bool httpOk         = false;
  bool nodeCommOk     = false;
  bool userCommOk     = false;

  int totalModules      = 0;
  int successfulModules = 0;

  String getStatusString() const {
    return String(successfulModules) + "/" + String(totalModules) + " modules ready";
  }
};

// ─── CommSetup ────────────────────────────────────────────────────────────────

class CommSetup {
private:
  CommSetupStatus status;
  int             stepCounter;
  static CommSetup *instance;

  // Adapter instances — owned here, registered into userComm
#if ENABLE_SMS
  SMSChannelAdapter   *smsAdapter  = nullptr;
#endif
#if ENABLE_MQTT
  MQTTChannelAdapter  *mqttAdapter = nullptr;
#endif
#if ENABLE_BLE
  BLEChannelAdapter   *bleAdapter  = nullptr;
#endif
#if ENABLE_LORA && ENABLE_LORA_USER_COMM
  LoRaChannelAdapter  *loraAdapter = nullptr;
#endif

  // Per-module init
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
  bool initUserCommunication();   // Wires adapters, registers into userComm

  void printStepHeader (const String &name);
  void printStepSuccess(const String &name);
  void printStepFailure(const String &name, const String &reason = "");
  void printSummary();

public:
  CommSetup();

  CommSetupStatus initializeAll();
  CommSetupStatus getStatus() const;
  void            printStatus();
  String          getStatusString() const;
  bool            isFullyInitialized() const;
  int             getSuccessfulCount() const { return status.successfulModules; }
  int             getTotalModules()    const { return status.totalModules; }
  bool            reinitModule(const String &moduleName);
  String          getDetailedReport()  const;

  // ── Channel polling — call from main loop ─────────────────────────────────
  // Polls every enabled transport for inbound user messages and delivers
  // them to userComm.onMessageReceived(). Also drives module background work.
  void processChannels(std::vector<Schedule> *schedules,
                       bool *scheduleRunning,
                       bool *scheduleLoaded);
};

#endif // COMM_SETUP_H
