// CommSetup.cpp - Communication module initialization and channel polling
#include "CommSetup.h"

CommSetup *CommSetup::instance = nullptr;

CommSetup::CommSetup() : stepCounter(0) { instance = this; }

// ─── Step logging ─────────────────────────────────────────────────────────────
void CommSetup::printStepHeader(const String &n) {
  Serial.println("[CommSetup] [" + String(++stepCounter) + "] " + n);
}
void CommSetup::printStepSuccess(const String &n) {
  Serial.println("[CommSetup] ✓ " + n + " ready");
}
void CommSetup::printStepFailure(const String &n, const String &r) {
  String m = "[CommSetup] ❌ " + n + " failed";
  if (r.length()) m += " — " + r;
  Serial.println(m);
}

// ─── Per-module init ──────────────────────────────────────────────────────────

#if ENABLE_BLE
bool CommSetup::initBLE() {
  printStepHeader("BLE");
  status.totalModules++;
  if (!bleComm.init()) { printStepFailure("BLE"); return false; }
  printStepSuccess("BLE");
  status.bleOk = true; status.successfulModules++;
  return true;
}
#endif

#if ENABLE_LORA
bool CommSetup::initLoRa() {
  printStepHeader("LoRa");
  status.totalModules++;
  if (!loraComm.init()) { printStepFailure("LoRa"); return false; }
  printStepSuccess("LoRa");
  status.loraOk = true; status.successfulModules++;
  return true;
}
#endif

#if ENABLE_WIFI
bool CommSetup::initWiFi() {
  printStepHeader("WiFi");
  status.totalModules++;
  if (wifiComm.init(WIFI_SSID, WIFI_PASS))
    Serial.println("[CommSetup]   IP: " + wifiComm.getIPAddress());
  else
    Serial.println("[CommSetup]   ⚠ WiFi connecting in background");
  printStepSuccess("WiFi");
  status.wifiOk = true; status.successfulModules++;
  return true;
}
#endif

#if ENABLE_MODEM
bool CommSetup::initModem() {
  printStepHeader("Modem (ModemBase)");
  status.totalModules++;
  if (!modemBase.init()) { printStepFailure("Modem"); return false; }
  printStepSuccess("Modem");
  status.modemOk = true; status.successfulModules++;
  return true;
}
#endif

#if ENABLE_SMS
bool CommSetup::initSMS() {
  printStepHeader("SMS (ModemSMS)");
  status.totalModules++;
  if (!modemSMS.configure()) { printStepFailure("SMS"); return false; }
  printStepSuccess("SMS");
  status.smsOk = true; status.successfulModules++;
  return true;
}
#endif

#if ENABLE_PPPOS
bool CommSetup::initPPPoS() {
  printStepHeader("PPPoS (ModemPPPoS)");
  status.totalModules++;
  if (!modemPPPoS.init()) { printStepFailure("PPPoS", "init() failed"); return false; }
  if (!modemPPPoS.connect(PPPOS_CONNECT_TIMEOUT_MS)) {
    printStepFailure("PPPoS", "connect() failed — check SIM/APN"); return false;
  }
  printStepSuccess("PPPoS");
  Serial.println("[CommSetup]   PPPoS IP: " + modemPPPoS.getLocalIP());
  status.ppposOk = true; status.successfulModules++;
  return true;
}
#endif

bool CommSetup::initNetworkRouter() {
  printStepHeader("NetworkRouter");
  status.totalModules++;
#if ENABLE_PPPOS && ENABLE_WIFI
  networkRouter.init(&modemPPPoS, &wifiComm);
#elif ENABLE_PPPOS
  networkRouter.init(&modemPPPoS, nullptr);
#elif ENABLE_WIFI
  networkRouter.init(nullptr, &wifiComm);
#else
  networkRouter.init(nullptr, nullptr);
  printStepFailure("NetworkRouter", "no bearers enabled");
  status.successfulModules++;
  return false;
#endif
  if (!networkRouter.connect()) {
    printStepFailure("NetworkRouter", "no bearer came up");
    status.networkRouterOk = false;
    status.successfulModules++;
    return false;
  }
  printStepSuccess("NetworkRouter");
  status.networkRouterOk = true; status.successfulModules++;
  return true;
}

#if ENABLE_MQTT
bool CommSetup::initMQTT() {
  printStepHeader("MQTT");
  status.totalModules++;
  if (!mqtt.init())      { printStepFailure("MQTT", "init() failed");      return false; }
  if (!mqtt.configure()) { printStepFailure("MQTT", "configure() failed"); return false; }
  printStepSuccess("MQTT");
  status.mqttOk = true; status.successfulModules++;
  return true;
}
#endif

#if ENABLE_HTTP
bool CommSetup::initHTTP() {
  printStepHeader("HTTP REST API");
  status.totalModules++;
  if (!httpComm.init(HTTP_SERVER_PORT)) { printStepFailure("HTTP"); return false; }
  printStepSuccess("HTTP");
  status.httpOk = true; status.successfulModules++;
  return true;
}
#endif

bool CommSetup::initNodeCommunication() {
  printStepHeader("Node Communication");
  status.totalModules++;
#if ENABLE_LORA
  if (!nodeComm.init(&loraComm)) { printStepFailure("NodeComm"); return false; }
#else
  Serial.println("[CommSetup]   LoRa disabled — node communication limited");
#endif
  printStepSuccess("Node Communication");
  status.nodeCommOk = true; status.successfulModules++;
  return true;
}

// ─── initUserCommunication() ──────────────────────────────────────────────────
// Creates adapter instances and registers them into UserCommunication.
// UserCommunication never sees the module types — only IChannelAdapter*.
bool CommSetup::initUserCommunication() {
  printStepHeader("UserCommunication + Channel Adapters");
  status.totalModules++;

  userComm.init(SMS_ALERT_PHONE_1);

  // Register one adapter per enabled, initialized module
#if ENABLE_SMS
  if (status.smsOk) {
    smsAdapter = new SMSChannelAdapter(modemSMS, SMS_ALERT_PHONE_1);
    userComm.registerAdapter(smsAdapter);
  }
#endif

#if ENABLE_MQTT
  if (status.mqttOk) {
    mqttAdapter = new MQTTChannelAdapter(mqtt, MQTT_TOPIC_ALERTS);
    userComm.registerAdapter(mqttAdapter);
  }
#endif

#if ENABLE_BLE
  if (status.bleOk) {
    bleAdapter = new BLEChannelAdapter(bleComm);
    userComm.registerAdapter(bleAdapter);
  }
#endif

#if ENABLE_LORA && ENABLE_LORA_USER_COMM
  if (status.loraOk) {
    loraAdapter = new LoRaChannelAdapter(loraComm, status.loraOk);
    userComm.registerAdapter(loraAdapter);
  }
#endif

  printStepSuccess("UserCommunication");
  status.userCommOk = true; status.successfulModules++;
  return true;
}

// ─── initializeAll() ──────────────────────────────────────────────────────────
CommSetupStatus CommSetup::initializeAll() {
  Serial.println("\n==========================================");
  Serial.println("  COMMUNICATION MODULE INITIALIZATION");
  Serial.println("==========================================\n");

#if ENABLE_BLE
  initBLE();
#endif
#if ENABLE_LORA
  initLoRa();
#endif
#if ENABLE_WIFI
  initWiFi();
#endif
#if ENABLE_MODEM
  initModem();       // Must precede SMS / PPPoS
#endif
#if ENABLE_SMS
  initSMS();
#endif
#if ENABLE_PPPOS
  initPPPoS();
#endif
  initNetworkRouter();
#if ENABLE_MQTT
  initMQTT();
#endif
#if ENABLE_HTTP
  initHTTP();
#endif
  initNodeCommunication();
  initUserCommunication(); // Always last — adapters need modules to be up

  printSummary();
  return status;
}

// ─── processChannels() ────────────────────────────────────────────────────────
// Called every loop(). Polls all inbound channels and routes any messages
// to userComm.onMessageReceived(). Also runs module background work.
// UserCommunication is never called except via onMessageReceived().
void CommSetup::processChannels(std::vector<Schedule> *schedules,
                                 bool *scheduleRunning,
                                 bool *scheduleLoaded) {
  SystemStatus sys = buildSystemStatus(schedules, scheduleRunning ? *scheduleRunning : false);

  // ── SMS ─────────────────────────────────────────────────────────────────────
#if ENABLE_SMS
  if (status.smsOk && modemSMS.isReady()) {
    modemSMS.processBackground();  // Read URCs
    std::vector<SMSMessage> incoming = modemSMS.processIncomingMessages(SMS_ALERT_PHONE_1);
    for (auto &sms : incoming) {
      // Build reply lambda capturing sender and index
      String sender = sms.sender;
      int    idx    = sms.index;
      auto   reply  = [sender, idx](const String &resp) {
        modemSMS.sendSMS(sender, resp);
        modemSMS.deleteSMS(idx);
      };
      ChannelMessage msg(sms.message, sms.sender, "SMS", reply);
      userComm.onMessageReceived(msg, scheduleRunning, scheduleLoaded, sys);
    }
  }
#endif

  // ── MQTT ────────────────────────────────────────────────────────────────────
#if ENABLE_MQTT
  if (status.mqttOk) {
    mqtt.processBackground();
    // MQTT commands arrive via the MQTTComm message callback set in initMQTT.
    // The callback (set below in initMQTT) calls onMQTTMessage() which builds
    // a ChannelMessage and calls userComm.onMessageReceived().
  }
#endif

  // ── HTTP ────────────────────────────────────────────────────────────────────
#if ENABLE_HTTP
  if (status.httpOk) {
    httpComm.processBackground();
    if (httpComm.hasCommands()) {
      auto cmds = httpComm.getCommands();
      httpComm.clearCommands();
      for (auto &cmd : cmds) {
        String src = cmd.source;
        // HTTP has no persistent reply path; response sent immediately via sendResponse
        auto reply = [src](const String &resp) {
          // HTTP responses are sent synchronously; logging only here
          Serial.println("[CommSetup] HTTP reply to " + src + ": " + resp);
        };
        ChannelMessage msg(cmd.command, cmd.source, "HTTP", reply);
        userComm.onMessageReceived(msg, scheduleRunning, scheduleLoaded, sys);
      }
    }
  }
#endif

  // ── WiFi background ─────────────────────────────────────────────────────────
#if ENABLE_WIFI
  wifiComm.processBackground();
#endif

  // ── NetworkRouter background (PPP stack feed + reconnect) ───────────────────
  networkRouter.processBackground();
}

// ─── buildSystemStatus() ─────────────────────────────────────────────────────
// Collects live state from all modules into the flat SystemStatus struct.
// UserCommunication receives this struct — it never queries modules directly.
SystemStatus CommSetup::buildSystemStatus(std::vector<Schedule> *schedules,
                                           bool scheduleRunning) const {
  SystemStatus s;
  s.scheduleRunning = scheduleRunning;
  s.uptimeSeconds   = millis() / 1000;
  s.freeHeapBytes   = ESP.getFreeHeap();
  s.totalHeapBytes  = ESP.getHeapSize();

  if (schedules) {
    s.totalSchedules = schedules->size();
    for (auto &sch : *schedules) if (sch.enabled) s.enabledSchedules++;
  }

#if ENABLE_BLE
  s.bleConnected  = status.bleOk  && bleComm.isConnected();
#endif
#if ENABLE_LORA
  s.loraUp        = status.loraOk;
#endif
#if ENABLE_WIFI
  s.wifiConnected = status.wifiOk && wifiComm.isConnected();
  if (s.wifiConnected) s.networkIP = wifiComm.getIPAddress();
#endif
#if ENABLE_PPPOS
  s.ppposConnected = status.ppposOk && modemPPPoS.isConnected();
  if (s.ppposConnected) s.networkIP = modemPPPoS.getLocalIP();
#endif
#if ENABLE_SMS
  s.smsReady      = status.smsOk  && modemSMS.isReady();
#endif
#if ENABLE_MQTT
  s.mqttConnected = status.mqttOk && mqtt.isConnected();
#endif
#if ENABLE_HTTP
  s.httpReady     = status.httpOk && httpComm.isReady();
#endif

  return s;
}

// ─── Status / Diagnostics ─────────────────────────────────────────────────────

CommSetupStatus CommSetup::getStatus() const { return status; }

bool CommSetup::isFullyInitialized() const {
  return status.totalModules > 0 && status.successfulModules == status.totalModules;
}

void CommSetup::printSummary() {
  Serial.println("\n==========================================");
  Serial.println("  INITIALIZATION SUMMARY");
  Serial.println("==========================================");
  Serial.printf("Modules: %d/%d OK\n", status.successfulModules, status.totalModules);
  Serial.println();
#if ENABLE_BLE
  Serial.printf("  BLE:     %s\n", status.bleOk           ? "✓ OK" : "✗ FAILED");
#endif
#if ENABLE_LORA
  Serial.printf("  LoRa:    %s\n", status.loraOk          ? "✓ OK" : "✗ FAILED");
#endif
#if ENABLE_WIFI
  Serial.printf("  WiFi:    %s\n", status.wifiOk          ? "✓ OK" : "⚠ CONNECTING");
#endif
#if ENABLE_MODEM
  Serial.printf("  Modem:   %s\n", status.modemOk         ? "✓ OK" : "✗ FAILED");
#endif
#if ENABLE_SMS
  Serial.printf("  SMS:     %s\n", status.smsOk           ? "✓ OK" : "✗ FAILED");
#endif
#if ENABLE_PPPOS
  Serial.printf("  PPPoS:   %s\n", status.ppposOk         ? "✓ OK" : "✗ FAILED");
#endif
  Serial.printf("  Router:  %s\n", status.networkRouterOk ? "✓ OK" : "⚠ NO BEARER");
#if ENABLE_MQTT
  Serial.printf("  MQTT:    %s\n", status.mqttOk          ? "✓ OK" : "⚠ CONNECTING");
#endif
#if ENABLE_HTTP
  Serial.printf("  HTTP:    %s\n", status.httpOk          ? "✓ OK" : "✗ FAILED");
#endif
  Serial.printf("  Nodes:   %s\n", status.nodeCommOk      ? "✓ OK" : "✗ FAILED");
  Serial.printf("  UserComm:%s\n", status.userCommOk      ? "✓ OK" : "✗ FAILED");
  Serial.println("==========================================\n");
}

void CommSetup::printStatus() { printSummary(); }

String CommSetup::getStatusString() const {
  return String(status.successfulModules) + "/" + String(status.totalModules) + " ready";
}

String CommSetup::getDetailedReport() const {
  String r = "=== COMM SETUP REPORT ===\n";
  r += "Modules: " + String(status.successfulModules) + "/" + String(status.totalModules) + "\n";
#if ENABLE_SMS
  r += "SMS:    " + String(status.smsOk           ? "OK" : "FAILED") + "\n";
#endif
#if ENABLE_PPPOS
  r += "PPPoS:  " + String(status.ppposOk         ? "OK" : "FAILED") + "\n";
#endif
  r += "Router: " + String(status.networkRouterOk ? "OK" : "NO BEARER") + "\n";
#if ENABLE_MQTT
  r += "MQTT:   " + String(status.mqttOk          ? "OK" : "FAILED") + "\n";
#endif
#if ENABLE_HTTP
  r += "HTTP:   " + String(status.httpOk          ? "OK" : "FAILED") + "\n";
#endif
  return r;
}

bool CommSetup::reinitModule(const String &moduleName) {
  String m = moduleName; m.toUpperCase();
#if ENABLE_BLE
  if (m == "BLE")    return initBLE();
#endif
#if ENABLE_LORA
  if (m == "LORA")   return initLoRa();
#endif
#if ENABLE_WIFI
  if (m == "WIFI")   return initWiFi();
#endif
#if ENABLE_MODEM
  if (m == "MODEM")  return initModem();
#endif
#if ENABLE_SMS
  if (m == "SMS")    return initSMS();
#endif
#if ENABLE_PPPOS
  if (m == "PPPOS")  return initPPPoS();
#endif
  if (m == "ROUTER") return initNetworkRouter();
#if ENABLE_MQTT
  if (m == "MQTT")   return initMQTT();
#endif
#if ENABLE_HTTP
  if (m == "HTTP")   return initHTTP();
#endif
  Serial.println("[CommSetup] Unknown module: " + moduleName);
  return false;
}
