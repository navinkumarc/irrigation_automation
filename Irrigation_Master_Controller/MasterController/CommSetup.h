// CommSetup.h - Centralized communication module initialization
// All optional modules are guarded by their ENABLE_ flag.
// Removing a module's .h/.cpp files is safe as long as its flag is 0.
#ifndef COMM_SETUP_H
#define COMM_SETUP_H

#include <Arduino.h>
#include "Config.h"

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
  // modemBase is extern'd in ModemBase.h
#endif

// NetworkRouter is always included — it handles bearer selection
// for MQTT and HTTP regardless of which bearers are enabled.
#include "NetworkRouter.h"
extern NetworkRouter networkRouter;

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

// ─── CommSetupStatus ──────────────────────────────────────────────────────────

struct CommSetupStatus {
  bool bleOk;
  bool loraOk;
  bool wifiOk;
  bool modemOk;
  bool smsOk;
  bool ppposOk;          // ModemPPPoS init status
  bool networkRouterOk;  // NetworkRouter connect status
  bool mqttOk;
  bool httpOk;
  bool nodeCommOk;
  bool userCommOk;

  int totalModules;
  int successfulModules;

  CommSetupStatus()
    : bleOk(false), loraOk(false), wifiOk(false), modemOk(false),
      smsOk(false), ppposOk(false), networkRouterOk(false),
      mqttOk(false), httpOk(false),
      nodeCommOk(false), userCommOk(false),
      totalModules(0), successfulModules(0) {}

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
#if ENABLE_MQTT
  bool initMQTT();
#endif
#if ENABLE_HTTP
  bool initHTTP();
#endif
  bool initNetworkRouter();
  bool initNodeCommunication();
  bool initUserCommunication();

  void printStepHeader(const String &moduleName);
  void printStepSuccess(const String &moduleName);
  void printStepFailure(const String &moduleName, const String &reason = "");
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
  String          getDetailedReport() const;
};

#endif // COMM_SETUP_H
