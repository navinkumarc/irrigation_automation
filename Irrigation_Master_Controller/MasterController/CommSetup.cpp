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

// ─── Per-Module Init — each compiled only when its flag is enabled ────────────

#if ENABLE_BLE
bool CommSetup::initBLE() {
  printStepHeader("BLE");
  status.totalModules++;
  if (!bleComm.init()) { printStepFailure("BLE", "init() returned false"); return false; }
  printStepSuccess("BLE");
  status.bleOk = true;
  status.successfulModules++;
  return true;
}
#endif

#if ENABLE_LORA
bool CommSetup::initLoRa() {
  printStepHeader("LoRa");
  status.totalModules++;
  if (!loraComm.init()) { printStepFailure("LoRa", "init() returned false"); return false; }
  printStepSuccess("LoRa");
  status.loraOk = true;
  status.successfulModules++;
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
  status.wifiOk = true;
  status.successfulModules++;
  return true;
}
#endif

#if ENABLE_MODEM
bool CommSetup::initModem() {
  printStepHeader("Modem (EC200U)");
  status.totalModules++;
  if (!modemBase.init()) { printStepFailure("Modem", "init() returned false"); return false; }
  printStepSuccess("Modem");
  status.modemOk = true;
  status.successfulModules++;
  return true;
}
#endif

#if ENABLE_SMS
bool CommSetup::initSMS() {
  printStepHeader("SMS");
  status.totalModules++;
  if (!modemSMS.configure()) { printStepFailure("SMS", "configure() returned false"); return false; }
  printStepSuccess("SMS");
  status.smsOk = true;
  status.successfulModules++;
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
  status.mqttOk = true;
  status.successfulModules++;
  return true;
}
#endif

#if ENABLE_HTTP
bool CommSetup::initHTTP() {
  printStepHeader("HTTP REST API");
  status.totalModules++;
  if (!httpComm.init(HTTP_SERVER_PORT)) { printStepFailure("HTTP", "init() returned false"); return false; }
  printStepSuccess("HTTP");
  status.httpOk = true;
  status.successfulModules++;
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
  status.nodeCommOk = true;
  status.successfulModules++;
  return true;
}

bool CommSetup::initUserCommunication() {
  printStepHeader("User Communication");
  status.totalModules++;
  // Core init — admin phone and setup reference
  userComm.init(SMS_ALERT_PHONE_1, this);

  // Register each enabled module via setter
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
  status.userCommOk = true;
  status.successfulModules++;
  return true;
}

// ─── initializeAll ────────────────────────────────────────────────────────────

CommSetupStatus CommSetup::initializeAll() {
  Serial.println("\n==========================================");
  Serial.println("  COMMUNICATION MODULE INITIALIZATION");
  Serial.println("==========================================\n");

  // Each init is compiled only when its flag is enabled.
  // Order matters: modem must be up before SMS.
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
  initModem();
#endif
#if ENABLE_SMS
  initSMS();
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
  Serial.printf("  BLE:    %s\n", status.bleOk      ? "✓ OK" : "✗ FAILED");
#endif
#if ENABLE_LORA
  Serial.printf("  LoRa:   %s\n", status.loraOk     ? "✓ OK" : "✗ FAILED");
#endif
#if ENABLE_WIFI
  Serial.printf("  WiFi:   %s\n", status.wifiOk     ? "✓ OK" : "⚠ CONNECTING");
#endif
#if ENABLE_MODEM
  Serial.printf("  Modem:  %s\n", status.modemOk    ? "✓ OK" : "✗ FAILED");
#endif
#if ENABLE_SMS
  Serial.printf("  SMS:    %s\n", status.smsOk      ? "✓ OK" : "✗ FAILED");
#endif
#if ENABLE_MQTT
  Serial.printf("  MQTT:   %s\n", status.mqttOk     ? "✓ OK" : "⚠ CONNECTING");
#endif
#if ENABLE_HTTP
  Serial.printf("  HTTP:   %s\n", status.httpOk     ? "✓ OK" : "✗ FAILED");
#endif
  Serial.printf("  Nodes:  %s\n", status.nodeCommOk  ? "✓ OK" : "✗ FAILED");
  Serial.printf("  UserComm: %s\n", status.userCommOk? "✓ OK" : "✗ FAILED");
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
  r += "SMS:  " + String(status.smsOk  ? "OK" : "FAILED") + "\n";
#endif
#if ENABLE_MQTT
  r += "MQTT: " + String(status.mqttOk ? "OK" : "FAILED") + "\n";
#endif
#if ENABLE_HTTP
  r += "HTTP: " + String(status.httpOk ? "OK" : "FAILED") + "\n";
#endif
  return r;
}

bool CommSetup::reinitModule(const String &moduleName) {
  String m = moduleName; m.toUpperCase();
#if ENABLE_BLE
  if (m == "BLE")   return initBLE();
#endif
#if ENABLE_LORA
  if (m == "LORA")  return initLoRa();
#endif
#if ENABLE_WIFI
  if (m == "WIFI")  return initWiFi();
#endif
#if ENABLE_MODEM
  if (m == "MODEM") return initModem();
#endif
#if ENABLE_SMS
  if (m == "SMS")   return initSMS();
#endif
#if ENABLE_MQTT
  if (m == "MQTT")  return initMQTT();
#endif
#if ENABLE_HTTP
  if (m == "HTTP")  return initHTTP();
#endif
  Serial.println("[CommSetup] Unknown module: " + moduleName);
  return false;
}
