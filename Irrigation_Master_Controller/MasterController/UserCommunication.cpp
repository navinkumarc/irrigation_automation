// UserCommunication.cpp - User ↔ Controller message exchange
// No module headers. No #if ENABLE_X guards. Pure business logic.
#include "UserCommunication.h"
#include <Arduino.h>
#include "MessageFormats.h"

// ─── Constructor ──────────────────────────────────────────────────────────────
UserCommunication::UserCommunication() : nodeCommandCallback(nullptr) {}

// ─── init() ───────────────────────────────────────────────────────────────────
void UserCommunication::init(const String &adminPhoneNumber) {
  adminPhone = adminPhoneNumber;
  Serial.println("[UserComm] ✓ Initialized (admin: " + adminPhone + ")");
}

// ─── registerAdapter() ────────────────────────────────────────────────────────
void UserCommunication::registerAdapter(IChannelAdapter *adapter) {
  if (adapter == nullptr) return;
  adapters.push_back(adapter);
  Serial.println("[UserComm] ✓ Adapter registered: " + String(adapter->channelName()));
}

void UserCommunication::setNodeCommandCallback(NodeCommandCallback cb) {
  nodeCommandCallback = cb;
  Serial.println("[UserComm] ✓ Node command callback registered");
}

// ─── onMessageReceived() ──────────────────────────────────────────────────────
// Primary inbound entry point. CommManager channel pollers call this;
// UserCommunication never polls channels itself.
void UserCommunication::onMessageReceived(const ChannelMessage &msg,
                                          bool *scheduleRunning,
                                          bool *scheduleLoaded,
                                          const SystemStatus &sys) {
  Serial.printf("[UserComm] ← [%s] %s: %s\n",
    msg.channel.c_str(), msg.sender.c_str(), msg.text.c_str());

  CommandResult result = dispatchCommand(msg.text, scheduleRunning, scheduleLoaded, sys);

  String reply = (result.success ? "✓ " : "✗ ") + result.commandType + ": " + result.response;

  // 1. Reply directly on the originating channel (if a reply path exists)
  if (msg.canReply()) {
    msg.reply(reply);
  }

  // 2. Log to Serial
  Serial.println("[UserComm] → " + reply);
}

// ─── sendAlert() ──────────────────────────────────────────────────────────────
void UserCommunication::sendAlert(const String &message, const String &severity) {
  String full = "[" + severity + "] " + message;
  Serial.println("[UserComm] ALERT: " + full);
  broadcast(full);
}

// ─── broadcastStatus() ────────────────────────────────────────────────────────
void UserCommunication::broadcastStatus(const SystemStatus &sys) {
  String brief = formatStatusBrief(sys);
  Serial.println("[UserComm] Broadcast status: " + brief);
  broadcast(brief);
}

// ─── Application event hooks ──────────────────────────────────────────────────
void UserCommunication::onScheduleStarted  (const String &id) { sendAlert(MsgFmt::alertScheduleStarted(id),    SEV_INFO);    }
void UserCommunication::onScheduleCompleted(const String &id) { sendAlert(MsgFmt::alertScheduleCompleted(id),  SEV_INFO);    }
void UserCommunication::onScheduleFailed   (const String &id, const String &reason) {
  sendAlert(MsgFmt::alertScheduleFailed(id, reason),      SEV_ERROR);
}
void UserCommunication::onValveAction(int nodeId, const String &valve, const String &action) {
  sendAlert("[" SEV_INFO "] Node " + String(nodeId) + ": " + valve + " " + action, SEV_INFO);
}
void UserCommunication::onSystemError  (const String &msg) { sendAlert(MsgFmt::alertError(msg),   SEV_ERROR);   }
void UserCommunication::onSystemWarning(const String &msg) { sendAlert(MsgFmt::alertWarning(msg), SEV_WARNING); }

// ─── broadcast() ─────────────────────────────────────────────────────────────
void UserCommunication::broadcast(const String &message) {
  for (IChannelAdapter *a : adapters) {
    if (a && a->isAvailable()) {
      if (!a->send(message)) {
        Serial.printf("[UserComm] ⚠ %s send failed\n", a->channelName());
      }
    }
  }
}

void UserCommunication::broadcastExcept(const String &message, const char *excludeChannel) {
  for (IChannelAdapter *a : adapters) {
    if (a && a->isAvailable() && strcmp(a->channelName(), excludeChannel) != 0) {
      a->send(message);
    }
  }
}

// ─── dispatchCommand() ────────────────────────────────────────────────────────
CommandResult UserCommunication::dispatchCommand(const String &raw,
                                                  bool *scheduleRunning,
                                                  bool *scheduleLoaded,
                                                  const SystemStatus &sys) {
  String cmd = raw; cmd.trim(); cmd.toUpperCase();

  if (cmd == "STATUS")           return handleStatusCommand(sys);
  if (cmd == "DIAGNOSTICS")      return handleDiagnosticsCommand();
  if (cmd == "SCHEDULES")        return handleSchedulesCommand(sys);
  if (cmd == "STOP")             return handleStopCommand(scheduleRunning, scheduleLoaded);
  if (cmd == "CHECK")            return handleCheckCommand();
  if (cmd == "HELP")             return handleHelpCommand();
  if (cmd == "STATS")            return handleStatsCommand();
  if (cmd.startsWith("START "))  return handleStartCommand(raw.substring(6));
  if (cmd.startsWith("NODE "))   return handleNodeCommand(raw.substring(5));
  // Irrigation schedule commands
  if (cmd.startsWith("ADD SCHED") || cmd.startsWith("DEL SCHED")
   || cmd.startsWith("ISCHED ")   || cmd.startsWith("ISDEL "))
    return handleScheduleCommand(raw);
  // Short irrigation schedule: G1 I:... (starts with G1 or G2)
  if ((cmd.startsWith("G1 ") || cmd.startsWith("G2 ")) && cmd.indexOf("I:") >= 0)
    return handleScheduleCommand(raw);
  if (cmd == "RESTART" || cmd == "REBOOT") {
    sendAlert(MsgFmt::alertWarning("Controller restarting now..."), SEV_WARNING);
    delay(500); ESP.restart();
    return CommandResult(true, "RESTART", "Restarting...");
  }
  // Pump commands forwarded via callback
  if (cmd.startsWith("WSP ") || cmd.startsWith("IPC "))
    return handlePumpCommand(raw);
  if (cmd == "PUMP STATUS")
    return handlePumpCommand(raw);

  return CommandResult(false, "UNKNOWN", "Unknown command. Send HELP for list.");
}

// ─── Command handlers ─────────────────────────────────────────────────────────

CommandResult UserCommunication::handleStatusCommand(const SystemStatus &sys) {
  return CommandResult(true, "STATUS", formatStatusBrief(sys));
}

CommandResult UserCommunication::handleDiagnosticsCommand() {
  printSystemDiagnostics();
  return CommandResult(true, "DIAGNOSTICS", "See serial output");
}

CommandResult UserCommunication::handleSchedulesCommand(const SystemStatus &sys) {
  String resp = String(sys.enabledSchedules) + "/" + String(sys.totalSchedules) + " enabled";
  if (sys.scheduleRunning)
    resp += " | Running: " + sys.currentScheduleId;
  return CommandResult(true, "SCHEDULES", resp);
}

CommandResult UserCommunication::handleStopCommand(bool *scheduleRunning, bool *scheduleLoaded) {
  if (scheduleRunning) *scheduleRunning = false;
  if (scheduleLoaded)  *scheduleLoaded  = false;
  return CommandResult(true, "STOP", "All schedules stopped");
}

CommandResult UserCommunication::handleStartCommand(const String &schedId) {
  if (schedId.length() == 0)
    return CommandResult(false, "START", "Usage: START <schedule_id>");
  return CommandResult(true, "START", "Starting: " + schedId);
}

CommandResult UserCommunication::handleCheckCommand() {
  return CommandResult(isSystemHealthy(), "CHECK", getHealthStatus());
}

CommandResult UserCommunication::handleNodeCommand(const String &args) {
  if (!nodeCommandCallback)
    return CommandResult(false, "NODE", "Node callback not set");

  String a = args; a.trim();
  int sp = a.indexOf(' ');
  if (sp <= 0)
    return CommandResult(false, "NODE", "Usage: NODE <id> <command>");

  int    nodeId  = a.substring(0, sp).toInt();
  String nodeCmd = a.substring(sp + 1); nodeCmd.trim();

  bool ok = nodeCommandCallback(nodeId, nodeCmd);
  return CommandResult(ok, "NODE",
    ok ? "Sent to node " + String(nodeId) : "Node " + String(nodeId) + " did not respond");
}

CommandResult UserCommunication::handleHelpCommand() {
  return CommandResult(true, "HELP", getHelpText());
}

CommandResult UserCommunication::handleStatsCommand() {
  String resp = "Heap: " + String(ESP.getFreeHeap() / 1024) + "KB free / "
              + String(ESP.getHeapSize() / 1024) + "KB total"
              + " | Uptime: " + String(millis() / 1000) + "s";
  return CommandResult(true, "STATS", resp);
}

// ─── handleScheduleCommand() ─────────────────────────────────────────────────
CommandResult UserCommunication::handleScheduleCommand(const String &raw) {
  if (scheduleCommandCallback) return scheduleCommandCallback(raw);
  return CommandResult(false, "SCHED", "Schedule command handler not configured");
}

// ─── handlePumpCommand() ────────────────────────────────────────────────────
CommandResult UserCommunication::handlePumpCommand(const String &raw) {
  if (pumpCommandCallback) return pumpCommandCallback(raw);
  return CommandResult(false, "PUMP", "Pump controller not configured");
}

// ─── Format helpers ───────────────────────────────────────────────────────────

String UserCommunication::formatStatusBrief(const SystemStatus &sys) const {
  String s = "[Status] ";
  s += "Sched:"     + String(sys.scheduleRunning ? "RUN" : "IDLE") + " ";
  s += "SMS:"       + String(sys.smsReady       ? "OK" : "--") + " ";
  s += "Ch:" + sys.activeChannelName + " ";
  s += "MQTT:" + String(sys.mqttConnected ? "OK" : "--") + " ";
  s += "HTTP:" + String(sys.httpReady     ? "OK" : "--") + " ";
  s += "BT:"   + String(sys.bleConnected  ? "OK" : "--") + " ";
  s += "LoRa:" + String(sys.loraUp        ? "OK" : "--") + " ";
  s += "Heap:"      + String(sys.freeHeapBytes / 1024) + "KB";
  return s;
}

String UserCommunication::formatStatusText(const SystemStatus &sys) const {
  String t = "\n========== SYSTEM STATUS ==========\n";
  t += "SCHEDULE:\n";
  t += "  Running:  " + String(sys.scheduleRunning ? "YES (" + sys.currentScheduleId + ")" : "NO") + "\n";
  t += "  Schedules:" + String(sys.enabledSchedules) + "/" + String(sys.totalSchedules) + " enabled\n";
  t += "ACTIVE CHANNEL (mutually exclusive):\n";
  t += "  Channel:   " + sys.activeChannelName + "\n";
  t += "  SMS:       " + String(sys.smsReady      ? "OK (AT mode)"   : "Off") + "\n";
  t += "  MQTT:      " + String(sys.mqttConnected ? "OK (connected)" : "Off") + "\n";
  t += "  HTTP:      " + String(sys.httpReady     ? "OK (listening)" : "Off") + "\n";
  t += "INDEPENDENT CHANNELS:\n";
  t += "  Bluetooth: " + String(sys.bleConnected  ? "OK (connected)" : "Off/no client") + "\n";
  t += "  LoRa:      " + String(sys.loraUp        ? "OK"             : "Off") + "\n";
  t += "  Serial:    always ON\n";
  t += "INTERNET BEARER (for MQTT/HTTP):\n";
  t += "  PPPoS: " + String(sys.ppposUp ? "Up (primary)" : "Down") + "\n";
  t += "  WiFi:  " + String(sys.wifiUp  ? "Up (fallback)" : "Down") + "\n";
  t += "  IP:    " + (sys.networkIP.length() ? sys.networkIP : "N/A") + "\n";
  t += "SYSTEM:\n";
  t += "  Uptime:  " + String(sys.uptimeSeconds) + "s\n";
  t += "  Heap:    " + String(sys.freeHeapBytes / 1024) + "KB free\n";
  t += "====================================\n";
  return t;
}

String UserCommunication::formatStatusJSON(const SystemStatus &sys) const {
  uint32_t heapPct = sys.totalHeapBytes
    ? (100 * (sys.totalHeapBytes - sys.freeHeapBytes) / sys.totalHeapBytes) : 0;
  String j = "{\n";
  j += "  \"schedule\": {\"running\":" + String(sys.scheduleRunning ? "true" : "false")
     + ",\"enabled\":" + String(sys.enabledSchedules)
     + ",\"total\":"   + String(sys.totalSchedules) + "},\n";
  j += "  \"activeChannel\":\"" + sys.activeChannelName + "\",\n";
  j += "  \"channels\": {\"sms\":" + String(sys.smsReady      ? "true" : "false")
     + ",\"mqtt\":" + String(sys.mqttConnected ? "true" : "false")
     + ",\"http\":" + String(sys.httpReady     ? "true" : "false")
     + ",\"ble\":" + String(sys.bleConnected   ? "true" : "false")
     + ",\"lora\":" + String(sys.loraUp        ? "true" : "false")
     + ",\"serial\":true},\n";
  j += "  \"bearer\": {\"name\":\"" + sys.bearerName + "\"";
  j += ",\"pppos\":" + String(sys.ppposUp ? "true" : "false");
  j += ",\"wifi\":" + String(sys.wifiUp  ? "true" : "false");
  j += ",\"ip\":\"" + sys.networkIP + "\"},\n";
  j += "  \"system\": {\"uptimeSec\":" + String(sys.uptimeSeconds)
     + ",\"freeHeap\":"  + String(sys.freeHeapBytes)
     + ",\"heapUsePct\":" + String(heapPct) + "}\n";
  j += "}\n";
  return j;
}

// ─── Diagnostics (Serial only) ────────────────────────────────────────────────

void UserCommunication::printAdapterStatus() const {
  Serial.println("[UserComm] ===== Registered Channel Adapters =====");
  if (adapters.empty()) {
    Serial.println("[UserComm]   (none registered)");
  }
  for (const IChannelAdapter *a : adapters) {
    Serial.printf("[UserComm]   %-8s %s\n",
      a->channelName(), a->isAvailable() ? "✓ available" : "✗ unavailable");
  }
  Serial.println("[UserComm] =========================================");
}

void UserCommunication::printBriefStatus(const SystemStatus &sys) const {
  Serial.println(formatStatusBrief(sys));
}

void UserCommunication::printSystemStatus(const SystemStatus &sys) const {
  Serial.println(formatStatusText(sys));
}

void UserCommunication::printSystemDiagnostics() const {
  Serial.println("\n[UserComm] ===== System Diagnostics =====");
  Serial.printf("[UserComm]   Free Heap:  %u KB\n", ESP.getFreeHeap()  / 1024);
  Serial.printf("[UserComm]   Total Heap: %u KB\n", ESP.getHeapSize()  / 1024);
  Serial.printf("[UserComm]   Uptime:     %lu s\n",  millis() / 1000);
  printAdapterStatus();
  Serial.println("[UserComm] =========================================\n");
}

// ─── Status text getters ─────────────────────────────────────────────────────
String UserCommunication::getStatusText (const SystemStatus &sys) const { return formatStatusText(sys);  }
String UserCommunication::getStatusBrief(const SystemStatus &sys) const { return formatStatusBrief(sys); }
String UserCommunication::getStatusJSON (const SystemStatus &sys) const { return formatStatusJSON(sys);  }

// ─── Health ───────────────────────────────────────────────────────────────────
bool UserCommunication::isSystemHealthy() const {
  return ESP.getFreeHeap() >= 50000;
}

String UserCommunication::getHealthStatus() const {
  if (isSystemHealthy()) return "HEALTHY";
  return "ISSUES: Low memory (" + String(ESP.getFreeHeap() / 1024) + "KB free)";
}

// ─── Help text ────────────────────────────────────────────────────────────────
String UserCommunication::getHelpText() const {
  return
    "Commands:\n"
    "  STATUS           — channel & schedule status\n"
    "  SCHEDULES        — list schedules\n"
    "  ISCHED G1 I:id,T:HH:MM,R:D|W|O[,D:mask][,Q:steps]\n"
    "  ISDEL <id>       — delete irrigation schedule\n"
    "  START <id>       — run now\n"
    "  STOP             — stop all\n"
    "  NODE <id> <cmd>  — send command to node\n"
    "  STATS            — memory & uptime\n"
    "  DIAGNOSTICS      — full system diagnostic (serial)\n"
    "  CHECK            — health check\n"
    "  RESTART          — reboot the controller\n"
    "  W1|W2|W3 ON|OFF|AUTO|STATUS — well pump\n"
    "  G1|G2 ON|OFF|STATUS         — irrigation pump\n"
    "  PUMP STATUS                 — all pump status\n"
    "  PS W1 I:id,T:HH:MM,R:D|W|O[,D:mask][,M:min]\n"
    "  DEL/DIS/ENA W1:id | PS LIST|STATUS\n"
    "  HELP             — this list\n";
}
