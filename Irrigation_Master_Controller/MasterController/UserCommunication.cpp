#include "UserCommunication.h"
#include "MessageQueue.h"
#include "CommSetup.h"

extern MessageQueue incomingQueue;
extern bool loraInitialized;

// ─── Constructor ──────────────────────────────────────────────────────────────

UserCommunication::UserCommunication()
  : commSetup(nullptr), nodeCommandCallback(nullptr) {
#if ENABLE_SMS
  smsComm  = nullptr;
#endif
#if ENABLE_MQTT
  mqttComm = nullptr;
#endif
#if ENABLE_HTTP
  httpComm = nullptr;
#endif
#if ENABLE_BLE
  bleComm  = nullptr;
#endif
#if ENABLE_LORA
  loraComm = nullptr;
#endif
#if ENABLE_WIFI
  wifiComm = nullptr;
#endif
}

// ─── Initialization ───────────────────────────────────────────────────────────

void UserCommunication::init(const String &adminPhoneNum, CommSetup *setup) {
  adminPhone = adminPhoneNum;
  commSetup  = setup;
  Serial.println("[UserComm] ✓ Initialized (admin: " + adminPhone + ")");
}

// Module setters — each only compiled when its flag is enabled.
#if ENABLE_SMS
void UserCommunication::setSMS(ModemSMS *sms) {
  smsComm = sms;
  Serial.println("[UserComm] ✓ SMS module registered");
}
#endif

#if ENABLE_MQTT
void UserCommunication::setMQTT(MQTTComm *mqtt) {
  mqttComm = mqtt;
  Serial.println("[UserComm] ✓ MQTT module registered");
}
#endif

#if ENABLE_HTTP
void UserCommunication::setHTTP(HTTPComm *http) {
  httpComm = http;
  Serial.println("[UserComm] ✓ HTTP module registered");
}
#endif

#if ENABLE_BLE
void UserCommunication::setBLE(BLEComm *ble) {
  bleComm = ble;
  Serial.println("[UserComm] ✓ BLE module registered");
}
#endif

#if ENABLE_LORA
void UserCommunication::setLoRa(LoRaComm *lora) {
  loraComm = lora;
  Serial.println("[UserComm] ✓ LoRa module registered");
}
#endif

#if ENABLE_WIFI
void UserCommunication::setWiFi(WiFiComm *wifi) {
  wifiComm = wifi;
  Serial.println("[UserComm] ✓ WiFi module registered");
}
#endif

void UserCommunication::setNodeCommandCallback(NodeCommandCallback callback) {
  nodeCommandCallback = callback;
  Serial.println("[UserComm] ✓ Node command callback registered");
}

// ─── Status Gathering ─────────────────────────────────────────────────────────

SystemStatusReport UserCommunication::gatherSystemStatus(std::vector<Schedule> *schedules,
                                                         bool scheduleRunning) {
  SystemStatusReport status;

  // Each field is only populated when the module is enabled.
#if ENABLE_BLE
  status.bleConnected   = (bleComm  != nullptr) ? bleComm->isConnected()  : false;
#else
  status.bleConnected   = false;
#endif

  status.loraInitialized = loraInitialized;

#if ENABLE_WIFI
  status.wifiConnected  = (wifiComm != nullptr) ? wifiComm->isConnected() : false;
  status.ipAddress      = (wifiComm != nullptr) ? wifiComm->getIPAddress() : "N/A";
  status.signalStrength = (wifiComm != nullptr) ? wifiComm->getSignalStrength() : 0;
#else
  status.wifiConnected  = false;
  status.ipAddress      = "N/A";
  status.signalStrength = 0;
#endif

#if ENABLE_SMS
  status.modemReady = (smsComm != nullptr);
  status.smsReady   = (smsComm != nullptr) ? smsComm->isReady() : false;
#else
  status.modemReady = false;
  status.smsReady   = false;
#endif

#if ENABLE_MQTT
  status.mqttConnected = (mqttComm != nullptr) ? mqttComm->isConnected() : false;
#else
  status.mqttConnected = false;
#endif

#if ENABLE_HTTP
  status.httpReady = (httpComm != nullptr) ? httpComm->isReady() : false;
#else
  status.httpReady = false;
#endif

  status.systemTime      = "System running";
  status.uptime          = String(millis() / 1000) + "s";
  status.scheduleRunning = scheduleRunning;
  status.wifiSSID        = WIFI_SSID;

  int enabled = 0, total = 0;
  if (schedules != nullptr) {
    total = schedules->size();
    for (auto &sch : *schedules) { if (sch.enabled) enabled++; }
  }
  status.successfulSchedules = enabled;
  status.failedSchedules     = 0;
  status.freeHeap            = ESP.getFreeHeap();
  status.totalHeap           = ESP.getHeapSize();

  return status;
}

// ─── Format Helpers ───────────────────────────────────────────────────────────

String UserCommunication::formatStatusAsText(const SystemStatusReport &status) {
  String t = "\n========== SYSTEM STATUS ==========\n";
  t += "COMMUNICATION:\n";
  t += "  BLE:   " + String(status.bleConnected   ? "✓ Connected" : "✗ Off") + "\n";
  t += "  LoRa:  " + String(status.loraInitialized? "✓ Active"    : "✗ Off") + "\n";
  t += "  WiFi:  " + String(status.wifiConnected  ? "✓ Connected" : "✗ Off") + "\n";
  t += "  SMS:   " + String(status.smsReady       ? "✓ Ready"     : "✗ Off") + "\n";
  t += "  MQTT:  " + String(status.mqttConnected  ? "✓ Connected" : "✗ Off") + "\n";
  t += "  HTTP:  " + String(status.httpReady      ? "✓ Ready"     : "✗ Off") + "\n";
  t += "SCHEDULES:\n";
  t += "  Running: " + String(status.scheduleRunning ? "YES" : "NO") + "\n";
  t += "  Enabled: " + String(status.successfulSchedules) + "\n";
  t += "SYSTEM:\n";
  t += "  Uptime:    " + status.uptime + "\n";
  t += "  Free Heap: " + String(status.freeHeap / 1024) + " KB\n";
  t += "  IP:        " + status.ipAddress + "\n";
  t += "====================================\n";
  return t;
}

String UserCommunication::formatStatusAsJSON(const SystemStatusReport &status) {
  uint32_t used = status.totalHeap - status.freeHeap;
  uint32_t pct  = status.totalHeap ? (100 * used / status.totalHeap) : 0;
  String j = "{\n";
  j += "  \"communication\": {\n";
  j += "    \"ble\":  " + String(status.bleConnected    ? "true" : "false") + ",\n";
  j += "    \"lora\": " + String(status.loraInitialized ? "true" : "false") + ",\n";
  j += "    \"wifi\": " + String(status.wifiConnected   ? "true" : "false") + ",\n";
  j += "    \"sms\":  " + String(status.smsReady        ? "true" : "false") + ",\n";
  j += "    \"mqtt\": " + String(status.mqttConnected   ? "true" : "false") + ",\n";
  j += "    \"http\": " + String(status.httpReady       ? "true" : "false") + "\n";
  j += "  },\n";
  j += "  \"schedule\": { \"running\": " + String(status.scheduleRunning ? "true" : "false") + ", \"enabled\": " + String(status.successfulSchedules) + " },\n";
  j += "  \"resources\": { \"freeHeap\": " + String(status.freeHeap) + ", \"usage\": " + String(pct) + " }\n";
  j += "}\n";
  return j;
}

String UserCommunication::formatStatusAsBrief(const SystemStatusReport &status) {
  String b = "[Status] ";
  b += "SMS:"  + String(status.smsReady       ? "✓" : "✗") + " ";
  b += "MQTT:" + String(status.mqttConnected  ? "✓" : "✗") + " ";
  b += "HTTP:" + String(status.httpReady      ? "✓" : "✗") + " ";
  b += "WiFi:" + String(status.wifiConnected  ? "✓" : "✗") + " ";
  b += "BLE:"  + String(status.bleConnected   ? "✓" : "✗") + " ";
  b += "Sched:" + String(status.scheduleRunning ? "RUN" : "STOP") + " ";
  b += "Heap:" + String(status.freeHeap / 1024) + "KB";
  return b;
}

// ─── Diagnostic Printing ─────────────────────────────────────────────────────

void UserCommunication::printSystemStatus(std::vector<Schedule> *schedules, bool scheduleRunning) {
  Serial.println(formatStatusAsText(gatherSystemStatus(schedules, scheduleRunning)));
}

void UserCommunication::printBriefStatus(std::vector<Schedule> *schedules, bool scheduleRunning) {
  Serial.println(formatStatusAsBrief(gatherSystemStatus(schedules, scheduleRunning)));
}

void UserCommunication::printCommStatus() {
  Serial.println("\n========== COMM STATUS ==========");
  if (commSetup != nullptr) commSetup->printStatus();

#if ENABLE_BLE
  Serial.println("BLE:");
  if (bleComm != nullptr) bleComm->printStatus();
  else Serial.println("  Not registered");
#endif

#if ENABLE_WIFI
  Serial.println("WiFi:");
  if (wifiComm != nullptr) {
    Serial.printf("  Connected: %s | IP: %s | Signal: %d dBm\n",
      wifiComm->isConnected() ? "YES" : "NO",
      wifiComm->getIPAddress().c_str(),
      wifiComm->getSignalStrength());
  } else Serial.println("  Not registered");
#endif

#if ENABLE_MQTT
  Serial.println("MQTT:");
  if (mqttComm != nullptr) Serial.printf("  Connected: %s\n", mqttComm->isConnected() ? "YES" : "NO");
  else Serial.println("  Not registered");
#endif

#if ENABLE_SMS
  Serial.println("SMS:");
  if (smsComm != nullptr) Serial.printf("  Ready: %s\n", smsComm->isReady() ? "YES" : "NO");
  else Serial.println("  Not registered");
#endif

#if ENABLE_HTTP
  Serial.println("HTTP:");
  if (httpComm != nullptr) Serial.printf("  Ready: %s\n", httpComm->isReady() ? "YES" : "NO");
  else Serial.println("  Not registered");
#endif

  Serial.println("=================================\n");
}

void UserCommunication::printSystemDiagnostics() {
  Serial.println("\n========== SYSTEM DIAGNOSTICS ==========");
  Serial.printf("  Free Heap: %u KB\n",  ESP.getFreeHeap()  / 1024);
  Serial.printf("  Total Heap: %u KB\n", ESP.getHeapSize()  / 1024);
  Serial.printf("  Uptime: %lu s\n",     millis() / 1000);
  Serial.println("========================================\n");
}

void UserCommunication::printNetworkDiagnostics() {
  Serial.println("\n========== NETWORK DIAGNOSTICS ==========");
#if ENABLE_WIFI
  if (wifiComm != nullptr) {
    Serial.printf("  WiFi: %s | IP: %s | Signal: %d dBm\n",
      wifiComm->isConnected() ? "Connected" : "Disconnected",
      wifiComm->getIPAddress().c_str(),
      wifiComm->getSignalStrength());
  }
#endif
#if ENABLE_MQTT
  if (mqttComm != nullptr)
    Serial.printf("  MQTT: %s\n", mqttComm->isConnected() ? "Connected" : "Disconnected");
#endif
  Serial.println("=========================================\n");
}

void UserCommunication::printLoRaDiagnostics() {
  Serial.println("\n========== LoRa DIAGNOSTICS ==========");
  Serial.printf("  Init: %s | Freq: %d MHz | SF: %d\n",
    loraInitialized ? "YES" : "NO",
    (int)(LORA_FREQUENCY / 1E6),
    LORA_SPREADING_FACTOR);
  Serial.println("======================================\n");
}

void UserCommunication::printBLEDiagnostics() {
  Serial.println("\n========== BLE DIAGNOSTICS ==========");
#if ENABLE_BLE
  if (bleComm != nullptr) bleComm->printStatus();
  else Serial.println("  Not registered");
#else
  Serial.println("  BLE disabled");
#endif
  Serial.println("=====================================\n");
}

void UserCommunication::printScheduleStatus(std::vector<Schedule> *schedules, bool scheduleRunning) {
  Serial.println("\n========== SCHEDULE STATUS ==========");
  Serial.printf("  Running: %s\n", scheduleRunning ? "YES" : "NO");
  if (schedules != nullptr) {
    int enabled = 0;
    for (auto &sch : *schedules) { if (sch.enabled) enabled++; }
    Serial.printf("  Total: %d | Enabled: %d\n", (int)schedules->size(), enabled);
  }
  Serial.println("=====================================\n");
}

// ─── Status Retrieval ─────────────────────────────────────────────────────────

SystemStatusReport UserCommunication::getSystemStatus(std::vector<Schedule> *schedules,
                                                      bool scheduleRunning) {
  return gatherSystemStatus(schedules, scheduleRunning);
}

String UserCommunication::getFormattedStatus(std::vector<Schedule> *schedules,
                                             bool scheduleRunning, const String &format) {
  auto status = gatherSystemStatus(schedules, scheduleRunning);
  if (format == "json")  return formatStatusAsJSON(status);
  if (format == "brief") return formatStatusAsBrief(status);
  return formatStatusAsText(status);
}

String UserCommunication::getStatusJSON(std::vector<Schedule> *schedules, bool scheduleRunning) {
  return getFormattedStatus(schedules, scheduleRunning, "json");
}

// ─── Alerts & Notifications ───────────────────────────────────────────────────

void UserCommunication::sendAlert(const String &alertMessage, const String &severity) {
  String full = "[" + severity + "] " + alertMessage;
  Serial.println("[UserComm] ALERT: " + full);

#if ENABLE_SMS
  if (smsComm != nullptr && smsComm->isReady()) {
    smsComm->sendNotification(full);
  }
#endif

#if ENABLE_MQTT
  if (mqttComm != nullptr && mqttComm->isConnected()) {
    mqttComm->publish(MQTT_TOPIC_ALERTS, full);
  }
#endif

#if ENABLE_BLE
  if (bleComm != nullptr && bleComm->isConnected()) {
    bleComm->notify(full);
  }
#endif
}

void UserCommunication::broadcastSystemStatus(std::vector<Schedule> *schedules,
                                              bool scheduleRunning) {
  String brief = formatStatusAsBrief(gatherSystemStatus(schedules, scheduleRunning));
  Serial.println("[UserComm] Broadcasting: " + brief);

#if ENABLE_MQTT
  if (mqttComm != nullptr && mqttComm->isConnected()) {
    mqttComm->publish(MQTT_TOPIC_STATUS, brief);
  }
#endif

#if ENABLE_BLE
  if (bleComm != nullptr && bleComm->isConnected()) {
    bleComm->notify(brief);
  }
#endif
}

void UserCommunication::notifyScheduleUpdate(const String &scheduleName, const String &status) {
  sendAlert("Schedule '" + scheduleName + "': " + status, "INFO");
}
void UserCommunication::onScheduleStarted(const String &scheduleId)  { notifyScheduleUpdate(scheduleId, "STARTED");   }
void UserCommunication::onScheduleCompleted(const String &scheduleId){ notifyScheduleUpdate(scheduleId, "COMPLETED"); }
void UserCommunication::onScheduleFailed(const String &scheduleId, const String &reason) {
  notifyScheduleUpdate(scheduleId, "FAILED: " + reason);
}
void UserCommunication::onValveAction(int nodeId, const String &valve, const String &action) {
  sendAlert("Node " + String(nodeId) + ": " + valve + " " + action, "INFO");
}
void UserCommunication::onSystemError(const String &errorMessage)   { sendAlert("ERROR: "   + errorMessage,   "ERROR");   }
void UserCommunication::onSystemWarning(const String &warningMessage){ sendAlert("WARNING: " + warningMessage, "WARNING"); }

// ─── Health ───────────────────────────────────────────────────────────────────

bool UserCommunication::isSystemHealthy() {
  return ESP.getFreeHeap() >= 50000;
}

String UserCommunication::getHealthStatus() {
  if (isSystemHealthy()) return "HEALTHY";
  return "ISSUES: Low Memory (" + String(ESP.getFreeHeap() / 1024) + " KB free)";
}

// ─── Command Routing ──────────────────────────────────────────────────────────

CommandResult UserCommunication::handleStatusCommand() {
  CommandResult r;
  r.success     = true;
  r.commandType = "STATUS";
  r.response    = "SMS:" +  String(
#if ENABLE_SMS
    (smsComm  && smsComm->isReady())   ? "ON" : "OFF"
#else
    "DISABLED"
#endif
  ) + " MQTT:" + String(
#if ENABLE_MQTT
    (mqttComm && mqttComm->isConnected()) ? "ON" : "OFF"
#else
    "DISABLED"
#endif
  ) + " HTTP:" + String(
#if ENABLE_HTTP
    (httpComm && httpComm->isReady()) ? "ON" : "OFF"
#else
    "DISABLED"
#endif
  );
  return r;
}

CommandResult UserCommunication::handleDiagnosticsCommand() {
  CommandResult r; r.success = true; r.commandType = "DIAGNOSTICS";
  r.response = "See serial output"; printSystemDiagnostics();
  return r;
}

CommandResult UserCommunication::handleSchedulesCommand(std::vector<Schedule> *schedules) {
  CommandResult r; r.success = true; r.commandType = "SCHEDULES";
  int en = 0, total = 0;
  if (schedules) { total = schedules->size(); for (auto &s : *schedules) if (s.enabled) en++; }
  r.response = String(en) + "/" + String(total) + " enabled";
  return r;
}

CommandResult UserCommunication::handleStopCommand(bool *scheduleRunning, bool *scheduleLoaded) {
  CommandResult r; r.success = true; r.commandType = "STOP";
  r.response = "All schedules stopped";
  if (scheduleRunning) *scheduleRunning = false;
  if (scheduleLoaded)  *scheduleLoaded  = false;
  return r;
}

CommandResult UserCommunication::handleStartCommand(const String &schedId) {
  CommandResult r; r.success = true; r.commandType = "START";
  r.response = "Starting: " + schedId;
  return r;
}

CommandResult UserCommunication::handleSMSOnCommand(bool *enableSMSBroadcast) {
  CommandResult r; r.success = true; r.commandType = "SMS_ON";
  r.response = "SMS alerts enabled";
  if (enableSMSBroadcast) *enableSMSBroadcast = true;
  return r;
}

CommandResult UserCommunication::handleSMSOffCommand(bool *enableSMSBroadcast) {
  CommandResult r; r.success = true; r.commandType = "SMS_OFF";
  r.response = "SMS alerts disabled";
  if (enableSMSBroadcast) *enableSMSBroadcast = false;
  return r;
}

CommandResult UserCommunication::handleCheckCommand() {
  CommandResult r; r.success = true; r.commandType = "CHECK";
  r.response = "System check complete"; return r;
}

CommandResult UserCommunication::handleNodeCommand(const String &cmd) {
  CommandResult r; r.commandType = "NODE";
  if (nodeCommandCallback) {
    int sp = cmd.indexOf(' ');
    if (sp > 0) {
      int    nodeId  = cmd.substring(0, sp).toInt();
      String nodeCmd = cmd.substring(sp + 1);
      r.success  = nodeCommandCallback(nodeId, nodeCmd);
      r.response = r.success ? "Sent to node " + String(nodeId) : "Failed";
    } else { r.success = false; r.response = "Usage: NODE <id> <cmd>"; }
  } else { r.success = false; r.response = "Node callback not set"; }
  return r;
}

CommandResult UserCommunication::handleHelpCommand() {
  CommandResult r; r.success = true; r.commandType = "HELP";
  r.response = getHelpText(); return r;
}

CommandResult UserCommunication::handleStatsCommand() {
  CommandResult r; r.success = true; r.commandType = "STATS";
  r.response = "Free Heap: " + String(ESP.getFreeHeap() / 1024) + " KB"; return r;
}

CommandResult UserCommunication::handleReportCommand() {
  CommandResult r; r.success = true; r.commandType = "REPORT";
  r.response = "See serial"; return r;
}

CommandResult UserCommunication::routeCommand(const String &cmd, std::vector<Schedule> *schedules,
                                              bool *scheduleRunning, bool *scheduleLoaded,
                                              bool *enableSMSBroadcast) {
  String c = cmd; c.toUpperCase();
  if (c == "STATUS")      return handleStatusCommand();
  if (c == "DIAGNOSTICS") return handleDiagnosticsCommand();
  if (c == "SCHEDULES")   return handleSchedulesCommand(schedules);
  if (c == "STOP")        return handleStopCommand(scheduleRunning, scheduleLoaded);
  if (c == "SMS ON")      return handleSMSOnCommand(enableSMSBroadcast);
  if (c == "SMS OFF")     return handleSMSOffCommand(enableSMSBroadcast);
  if (c == "CHECK")       return handleCheckCommand();
  if (c == "HELP")        return handleHelpCommand();
  if (c == "STATS")       return handleStatsCommand();
  if (c == "REPORT")      return handleReportCommand();
  if (c.startsWith("NODE ")) return handleNodeCommand(cmd.substring(5));
  if (c.startsWith("START ")) return handleStartCommand(cmd.substring(6));
  CommandResult r; r.success = false; r.commandType = "UNKNOWN";
  r.response = "Unknown command. Type HELP.";
  return r;
}

void UserCommunication::sendResponse(const String &response, const String &channel) {
  Serial.println("[UserComm] [" + channel + "] " + response);
}

void UserCommunication::sendMultiChannelResponse(const String &response) {
#if ENABLE_BLE
  if (bleComm != nullptr && bleComm->isConnected()) bleComm->notify(response);
#endif
#if ENABLE_MQTT
  if (mqttComm != nullptr && mqttComm->isConnected()) mqttComm->publish(MQTT_TOPIC_COMMANDS, response);
#endif
}

void UserCommunication::sendCommandResponse(const String &command, const CommandResult &result,
                                            const String &channel) {
  String response = (result.success ? "✓ " : "✗ ") + result.commandType + ": " + result.response;
  Serial.println("[UserComm] Response: " + response);
  if (channel == "BLE") {
#if ENABLE_BLE
    if (bleComm != nullptr && bleComm->isConnected()) bleComm->notify(response);
#endif
  } else if (channel == "MQTT") {
#if ENABLE_MQTT
    if (mqttComm != nullptr && mqttComm->isConnected()) mqttComm->publish(MQTT_TOPIC_COMMANDS, response);
#endif
  }
}

String UserCommunication::getHelpText() {
  String h = "\n========== COMMANDS ==========\n";
  h += "STATUS      - System status\n";
  h += "DIAGNOSTICS - Full diagnostics\n";
  h += "SCHEDULES   - List schedules\n";
  h += "START <id>  - Start schedule\n";
  h += "STOP        - Stop all schedules\n";
  h += "SMS ON/OFF  - Toggle SMS alerts\n";
  h += "NODE <id> <cmd> - Node command\n";
  h += "STATS       - Memory stats\n";
  h += "HELP        - This help\n";
  h += "==============================\n";
  return h;
}

// ─── Channel Processors ───────────────────────────────────────────────────────

void UserCommunication::processAllChannels(std::vector<Schedule> *schedules,
                                           bool *scheduleRunning, bool *scheduleLoaded,
                                           bool *enableSMSBroadcast) {
#if ENABLE_SMS
  processSMSCommands(schedules, scheduleRunning, scheduleLoaded, enableSMSBroadcast);
#endif
#if ENABLE_LORA
  processLoRaCommands(schedules, scheduleRunning, scheduleLoaded, enableSMSBroadcast);
#endif
#if ENABLE_MQTT
  processMQTTCommands(schedules, scheduleRunning, scheduleLoaded, enableSMSBroadcast);
#endif
#if ENABLE_WIFI
  processWiFiCommands(schedules, scheduleRunning, scheduleLoaded, enableSMSBroadcast);
#endif
#if ENABLE_HTTP
  processHTTPCommands(schedules, scheduleRunning, scheduleLoaded, enableSMSBroadcast);
#endif
}

void UserCommunication::processSMSCommands(std::vector<Schedule> *schedules,
                                           bool *scheduleRunning, bool *scheduleLoaded,
                                           bool *enableSMSBroadcast) {
#if ENABLE_SMS
  if (smsComm == nullptr || !smsComm->isReady()) return;

  // Process background URCs first
  smsComm->processBackground();

  // Process any queued incoming messages
  std::vector<SMSMessage> commands = smsComm->processIncomingMessages(adminPhone);
  for (auto &msg : commands) {
    Serial.println("[UserComm] SMS command from " + msg.sender + ": " + msg.message);
    CommandResult result = routeCommand(msg.message, schedules, scheduleRunning,
                                        scheduleLoaded, enableSMSBroadcast);
    // Reply via SMS
    smsComm->sendSMS(msg.sender, result.response);
    smsComm->deleteSMS(msg.index);
  }
#endif
}

void UserCommunication::processLoRaCommands(std::vector<Schedule> *schedules,
                                            bool *scheduleRunning, bool *scheduleLoaded,
                                            bool *enableSMSBroadcast) {
#if ENABLE_LORA
  // LoRa command processing handled by NodeCommunication module
#endif
}

void UserCommunication::processMQTTCommands(std::vector<Schedule> *schedules,
                                            bool *scheduleRunning, bool *scheduleLoaded,
                                            bool *enableSMSBroadcast) {
#if ENABLE_MQTT
  if (mqttComm == nullptr || !mqttComm->isConnected()) return;
  mqttComm->processBackground();
#endif
}

void UserCommunication::processWiFiCommands(std::vector<Schedule> *schedules,
                                            bool *scheduleRunning, bool *scheduleLoaded,
                                            bool *enableSMSBroadcast) {
#if ENABLE_WIFI
  // WiFi background handled in main loop via wifiComm.processBackground()
#endif
}

void UserCommunication::processHTTPCommands(std::vector<Schedule> *schedules,
                                            bool *scheduleRunning, bool *scheduleLoaded,
                                            bool *enableSMSBroadcast) {
#if ENABLE_HTTP
  if (httpComm == nullptr || !httpComm->isReady()) return;
  httpComm->processBackground();

  if (!httpComm->hasCommands()) return;
  auto cmds = httpComm->getCommands();
  httpComm->clearCommands();

  for (auto &cmd : cmds) {
    Serial.println("[UserComm] HTTP command from " + cmd.source + ": " + cmd.command);
    CommandResult result = routeCommand(cmd.command, schedules, scheduleRunning,
                                        scheduleLoaded, enableSMSBroadcast);
    httpComm->sendResponse(result.response);
  }
#endif
}

void UserCommunication::processBLECommand(int nodeId, const String &command) {
  CommandResult result = routeCommand(command, nullptr, nullptr, nullptr, nullptr);
  Serial.printf("[UserComm] BLE [node %d]: %s → %s\n", nodeId, command.c_str(), result.response.c_str());
#if ENABLE_BLE
  if (bleComm != nullptr && bleComm->isConnected()) bleComm->notify(result.response);
#endif
}

void UserCommunication::processSerialCommand(const String &input,
                                             std::vector<Schedule> *schedules,
                                             bool *scheduleRunning, bool *scheduleLoaded) {
  CommandResult result = routeCommand(input, schedules, scheduleRunning, scheduleLoaded, nullptr);
  Serial.println("[UserComm] Serial: " + result.response);
}

void UserCommunication::processMessage(const String &message) {
  Serial.println("[UserComm] Message: " + message);
}
