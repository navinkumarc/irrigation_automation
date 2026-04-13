// CommSetup.cpp - Centralized communication module initialization
#include "CommSetup.h"

CommSetup *CommSetup::instance = nullptr;

CommSetup::CommSetup() : stepCounter(0) {
  status.totalModules      = 0;
  status.successfulModules = 0;
  instance = this;
}

void CommSetup::printStepHeader(const String &moduleName) {
  stepCounter++;
  Serial.println("[CommSetup] [" + String(stepCounter) + "] " + moduleName);
}
void CommSetup::printStepSuccess(const String &moduleName) {
  Serial.println("[CommSetup] ✓ " + moduleName + " ready");
}
void CommSetup::printStepFailure(const String &moduleName, const String &reason) {
  String msg = "[CommSetup] ❌ " + moduleName + " failed";
  if (reason.length() > 0) msg += " — " + reason;
  Serial.println(msg);
}

// ─── Per-module init ──────────────────────────────────────────────────────────

#if ENABLE_BLE
bool CommSetup::initBLE() {
  printStepHeader("BLE");
  status.totalModules++;
  if (!bleComm.init()) { printStepFailure("BLE", "init() returned false"); return false; }
  printStepSuccess("BLE");
  status.bleOk = true; status.successfulModules++;
  return true;
}
#endif

#if ENABLE_LORA
bool CommSetup::initLoRa() {
  printStepHeader("LoRa");
  status.totalModules++;
  if (!loraComm.init()) { printStepFailure("LoRa", "init() returned false"); return false; }
  printStepSuccess("LoRa");
  status.loraOk = true; status.successfulModules++;
  return true;
}
#endif

#if ENABLE_WIFI
bool CommSetup::initWiFi() {
  printStepHeader("WiFi");
  status.totalModules++;
  if (wifiComm.init(WIFI_SSID, WIFI_PASS)) {
    Serial.println("[CommSetup]   IP: " + wifiComm.getIPAddress());
  } else {
    Serial.println("[CommSetup]   ⚠ WiFi connecting in background");
  }
  printStepSuccess("WiFi");
  status.wifiOk = true; status.successfulModules++;
  return true;
}
#endif

#if ENABLE_MODEM
bool CommSetup::initModem() {
  printStepHeader("Modem (EC200U / ModemBase)");
  status.totalModules++;
  if (!modemBase.init()) { printStepFailure("Modem", "init() returned false"); return false; }
  printStepSuccess("Modem");
  status.modemOk = true; status.successfulModules++;
  return true;
}
#endif

#if ENABLE_SMS
// ModemSMS depends on ModemBase (MODEM_MODE_SMS).
// initModem() must succeed before initSMS() is called.
bool CommSetup::initSMS() {
  printStepHeader("SMS (ModemSMS)");
  status.totalModules++;
  if (!modemSMS.configure()) { printStepFailure("SMS", "configure() returned false"); return false; }
  printStepSuccess("SMS");
  status.smsOk = true; status.successfulModules++;
  return true;
}
#endif

#if ENABLE_PPPOS
// ModemPPPoS depends on ModemBase (MODEM_MODE_DATA).
// initModem() must succeed before initPPPoS() is called.
// ENABLE_SMS must be 0 (enforced by #error in Config.h).
bool CommSetup::initPPPoS() {
  printStepHeader("PPPoS data (ModemPPPoS)");
  status.totalModules++;
  if (!modemPPPoS.init()) {
    printStepFailure("PPPoS", "init() returned false");
    return false;
  }
  if (!modemPPPoS.connect(PPPOS_CONNECT_TIMEOUT_MS)) {
    printStepFailure("PPPoS", "connect() failed — check SIM and APN");
    return false;
  }
  printStepSuccess("PPPoS");
  Serial.println("[CommSetup]   PPPoS IP: " + modemPPPoS.getLocalIP());
  status.ppposOk = true; status.successfulModules++;
  return true;
}
#endif

#if ENABLE_MQTT
bool CommSetup::initMQTT() {
  printStepHeader("MQTT");
  status.totalModules++;
  if (!mqtt.init()) { printStepFailure("MQTT", "init() returned false"); return false; }
  if (!mqtt.configure()) { printStepFailure("MQTT", "configure() returned false"); return false; }
  printStepSuccess("MQTT");
  status.mqttOk = true; status.successfulModules++;
  return true;
}
#endif

#if ENABLE_HTTP
bool CommSetup::initHTTP() {
  printStepHeader("HTTP REST API");
  status.totalModules++;
  if (!httpComm.init(HTTP_SERVER_PORT)) { printStepFailure("HTTP", "init() returned false"); return false; }
  printStepSuccess("HTTP");
  status.httpOk = true; status.successfulModules++;
  return true;
}
#endif

bool CommSetup::initNodeCommunication() {
  printStepHeader("Node Communication");
  status.totalModules++;
#if ENABLE_LORA
  if (!nodeComm.init(&loraComm)) { printStepFailure("Node Comm", "init() returned false"); return false; }
#else
  Serial.println("[CommSetup]   LoRa disabled — node communication limited");
#endif
  printStepSuccess("Node Communication");
  status.nodeCommOk = true; status.successfulModules++;
  return true;
}

bool CommSetup::initUserCommunication() {
  printStepHeader("User Communication");
  status.totalModules++;
  userComm.init(SMS_ALERT_PHONE_1, this);
#if ENABLE_SMS
  userComm.setSMS(&modemSMS);
#endif
#if ENABLE_MQTT
  userComm.setMQTT(&mqtt);
#endif
#if ENABLE_HTTP
  userComm.setHTTP(&httpComm);
#endif
#if ENABLE_BLE
  userComm.setBLE(&bleComm);
#endif
#if ENABLE_LORA
  userComm.setLoRa(&loraComm);
#endif
#if ENABLE_WIFI
  userComm.setWiFi(&wifiComm);
#endif
  printStepSuccess("User Communication");
  status.userCommOk = true; status.successfulModules++;
  return true;
}

// ─── initializeAll() ──────────────────────────────────────────────────────────
// Initialization order matters:
//   Modem (ModemBase) must come before SMS or PPPoS.
//   WiFi must come before MQTT (WiFi-based MQTT).
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
  initModem();   // Must succeed before SMS or PPPoS
#endif
#if ENABLE_SMS
  initSMS();     // Requires MODEM_MODE_SMS
#endif
#if ENABLE_PPPOS
  initPPPoS();   // Requires MODEM_MODE_DATA (mutually exclusive with SMS)
#endif
#if ENABLE_MQTT
  initMQTT();
#endif
#if ENABLE_HTTP
  initHTTP();
#endif

  initNodeCommunication();
  initUserCommunication();

  printSummary();
  return status;
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
  Serial.println("\nStatus:");
#if ENABLE_BLE
  Serial.printf("  BLE:    %s\n", status.bleOk    ? "✓ OK" : "✗ FAILED");
#endif
#if ENABLE_LORA
  Serial.printf("  LoRa:   %s\n", status.loraOk   ? "✓ OK" : "✗ FAILED");
#endif
#if ENABLE_WIFI
  Serial.printf("  WiFi:   %s\n", status.wifiOk   ? "✓ OK" : "⚠ CONNECTING");
#endif
#if ENABLE_MODEM
  Serial.printf("  Modem:  %s\n", status.modemOk  ? "✓ OK" : "✗ FAILED");
#endif
#if ENABLE_SMS
  Serial.printf("  SMS:    %s\n", status.smsOk    ? "✓ OK" : "✗ FAILED");
#endif
#if ENABLE_PPPOS
  Serial.printf("  PPPoS:  %s\n", status.ppposOk  ? "✓ OK" : "✗ FAILED");
#endif
#if ENABLE_MQTT
  Serial.printf("  MQTT:   %s\n", status.mqttOk   ? "✓ OK" : "⚠ CONNECTING");
#endif
#if ENABLE_HTTP
  Serial.printf("  HTTP:   %s\n", status.httpOk   ? "✓ OK" : "✗ FAILED");
#endif
  Serial.printf("  Nodes:    %s\n", status.nodeCommOk ? "✓ OK" : "✗ FAILED");
  Serial.printf("  UserComm: %s\n", status.userCommOk ? "✓ OK" : "✗ FAILED");
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
  r += "SMS:   " + String(status.smsOk   ? "OK" : "FAILED") + "\n";
#endif
#if ENABLE_PPPOS
  r += "PPPoS: " + String(status.ppposOk ? "OK" : "FAILED") + "\n";
#endif
#if ENABLE_MQTT
  r += "MQTT:  " + String(status.mqttOk  ? "OK" : "FAILED") + "\n";
#endif
#if ENABLE_HTTP
  r += "HTTP:  " + String(status.httpOk  ? "OK" : "FAILED") + "\n";
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
#if ENABLE_MQTT
  if (m == "MQTT")   return initMQTT();
#endif
#if ENABLE_HTTP
  if (m == "HTTP")   return initHTTP();
#endif
  Serial.println("[CommSetup] Unknown module: " + moduleName);
  return false;
}
