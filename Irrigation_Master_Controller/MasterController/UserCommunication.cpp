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

  // 1. Reply on the originating channel
  if (msg.canReply()) {
    msg.reply(reply);
  }

  // 2. Log to Serial only if the channel did NOT already reply to Serial
  //    (Serial channel reply lambda already prints [Serial] → ...)
  if (msg.channel != "Serial") {
    Serial.println("[UserComm] → " + reply);
  }
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
  if (cmd == "NODES")            return handleNodesCommand();
  if (cmd == "DIAGNOSTICS")      return handleDiagnosticsCommand();
  if (cmd == "SCHEDULES")        return handleSchedulesCommand(sys);
  if (cmd == "STOP")             return handleStopCommand(scheduleRunning, scheduleLoaded);
  if (cmd == "CHECK")            return handleCheckCommand();
  if (cmd == "HELP")             return handleHelpCommand();
  if (cmd == "STATS")            return handleStatsCommand();
  if (cmd.startsWith("START "))  return handleStartCommand(raw.substring(6));
  if (cmd.startsWith("NODE "))   return handleNodeCommand(raw.substring(5));

  // ── Irrigation schedule commands ─────────────────────────────────────
  if (cmd.startsWith("ADD SCHED") || cmd.startsWith("DEL SCHED")
   || cmd.startsWith("ISCH ")     || cmd.startsWith("ISDL "))
    return handleScheduleCommand(raw);
  // G1/G2 I:... = irrigation schedule shorthand
  if ((cmd.startsWith("G1 ") || cmd.startsWith("G2 ")) && cmd.indexOf("I:") >= 0)
    return handleScheduleCommand(raw);

  // ── Fill group commands: FG1/FG2 ON|OFF|AUTO|STATUS ──────────────────
  if (cmd.startsWith("FG1 ") || cmd.startsWith("FG2 ")
   || cmd == "FG1" || cmd == "FG2")
    return handlePumpCommand(raw);

  // ── Tank status: T1|T2 STATUS ─────────────────────────────────────────
  if (cmd == "T1 STATUS" || cmd == "T2 STATUS"
   || cmd == "T1"        || cmd == "T2")
    return handlePumpCommand(raw);

  // ── POWER family ───────────────────────────────────────────────────────
  if (cmd == "POWER" || cmd.startsWith("POWER "))
    return handlePowerCommand(raw);

  // ── Well pump commands: W1|W2 ON|OFF|AUTO|STATUS ─────────────────────
  if (cmd.startsWith("W1 ") || cmd.startsWith("W2 ")
   || cmd == "W1" || cmd == "W2")
    return handlePumpCommand(raw);

  // ── Irrigation pump commands: G1|G2 ON|OFF|STATUS ────────────────────
  if (cmd.startsWith("G1 ") || cmd.startsWith("G2 ")
   || cmd == "G1" || cmd == "G2")
    return handlePumpCommand(raw);

  // ── Pump schedule commands: WSCH ... ──────────────────────────────────
  if (cmd.startsWith("WSCH ") || cmd == "WSCH LIST" || cmd == "WSCH STATUS"
   || cmd.startsWith("DEL W") || cmd.startsWith("DEL G")
   || cmd.startsWith("DIS W") || cmd.startsWith("DIS G")
   || cmd.startsWith("ENA W") || cmd.startsWith("ENA G")
   || cmd.startsWith("PSCHED "))
    return handlePumpCommand(raw);

  // ── Combined pump status ──────────────────────────────────────────────
  if (cmd == "PUMP STATUS" || cmd == "PUMP"
   || cmd.startsWith("WSP ") || cmd.startsWith("IPC "))
    return handlePumpCommand(raw);

  if (cmd == "RESTART" || cmd == "REBOOT") {
    sendAlert(MsgFmt::alertWarning("Controller restarting now..."), SEV_WARNING);
    delay(500); ESP.restart();
    return CommandResult(true, "RESTART", "Restarting...");
  }

  return CommandResult(false, "UNKNOWN",
    "Unknown: " + raw.substring(0, min((int)raw.length(), 20))
    + ". Send HELP");
}

// ─── Command handlers ─────────────────────────────────────────────────────────

CommandResult UserCommunication::handleStatusCommand(const SystemStatus &sys) {
  // Clean device status: uptime, heap, channels, schedule
  String s;
  // Uptime
  uint32_t up = sys.uptimeSeconds;
  char uptimeBuf[24];
  snprintf(uptimeBuf, sizeof(uptimeBuf), "%ud%02uh%02um",
    up/86400, (up%86400)/3600, (up%3600)/60);
  s += "Uptime:" + String(uptimeBuf) + "\n";
  // Heap
  s += "Heap:" + String(sys.freeHeapBytes/1024) + "KB/"
     + String(sys.totalHeapBytes/1024) + "KB\n";
  // Schedule
  s += "Sched:" + String(sys.scheduleRunning
       ? "RUN(" + sys.currentScheduleId + ")" : "IDLE")
     + " " + String(sys.enabledSchedules) + "/"
     + String(sys.totalSchedules) + "active\n";
  // Channels
  s += "Ch:" + sys.activeChannelName;
  s += " LoRa:"   + String(sys.loraUp        ? "OK" : "--");
  s += " BLE:"    + String(sys.bleConnected   ? "OK" : "--");
  s += " SMS:"    + String(sys.smsReady       ? "OK" : "--") + "\n";
  // Network
  if (sys.ppposUp || sys.wifiUp)
    s += "Net:" + sys.bearerName + " IP:" + sys.networkIP + "\n";
  else
    s += "Net:offline\n";
  // Power (if power monitor callback set)
  if (powerStatusCallback) s += powerStatusCallback();
  return CommandResult(true, "STATUS", s);
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

// ─── handlePowerCommand() ─────────────────────────────────────────────────────
// POWER              — master power + live nodes list
// POWER M            — master power only
// POWER N1           — node 1 power
// POWER N1,N2        — node 1 and node 2 power
// POWER M N1,N2      — master + node 1 and node 2
CommandResult UserCommunication::handlePowerCommand(const String &raw) {
  String up = raw; up.trim(); up.toUpperCase();
  // Strip "POWER" prefix, trim rest
  String args = (up.length() > 6) ? up.substring(6) : "";
  args.trim();

  bool wantMaster = false;
  String nodeList = "";  // comma-sep nodeIds e.g. "1,2,7"

  if (args.length() == 0) {
    // Plain POWER — show master + live nodes list
    wantMaster = true;
    // nodeList stays empty — just show live list, no per-node detail
  } else {
    // Parse tokens: M = master, N1/N2/etc = node ids
    // e.g. "M N1,N2"  or "N1,N3"  or "M"
    // First check for M token
    if (args == "M" || args.startsWith("M ") || args.startsWith("M,")) {
      wantMaster = true;
      args = args.substring(1); args.trim();
      if (args.startsWith(",")) args = args.substring(1);
      args.trim();
    }
    // Remaining: N1,N2,N7 or N1 etc
    // Extract numeric ids from N-prefixed tokens
    String tmp = args; tmp.replace("N","").replace(" ","");
    // tmp is now "1,2,7" or "1" or ""
    nodeList = tmp;
    if (nodeList.length() == 0 && !wantMaster) wantMaster = true;
  }

  String result;

  // ── Master power section ────────────────────────────────────────────────
  if (wantMaster) {
    result += "Master:\n";
    if (powerStatusCallback) {
      result += powerStatusCallback();
    } else {
      result += "Power:unavailable\n";
    }
    // Always show live nodes list with plain POWER
    if (nodeList.length() == 0 && nodeStatusCallback) {
      result += nodeStatusCallback();
    }
  }

  // ── Per-node power section ─────────────────────────────────────────────
  if (nodeList.length() > 0) {
    if (nodePowerCallback) {
      result += nodePowerCallback(nodeList);
    } else {
      result += "Node power:unavailable\n";
    }
  }

  return CommandResult(true, "POWER", result);
}

CommandResult UserCommunication::handleNodesCommand() {
  if (nodeStatusCallback) return CommandResult(true, "NODES", nodeStatusCallback());
  return CommandResult(false, "NODES", "Node status not available");
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
    // ── System ───────────────────────────────────────────────────────
    "STATUS    uptime heap channels network power\n"
    "NODES     connected node controller status\n"
    "STATS     heap & uptime\n"
    "CHECK     health check\n"
    "DIAG      full diagnostic (serial only)\n"
    "RESTART   reboot controller\n"
    // ── Power / Battery ──────────────────────────────────────────────
    "POWER STATUS     voltage % source charge-state health\n"
    "BAT              alias for POWER STATUS\n"
    // ── Fill groups (WTT) ─────────────────────────────────────────────
    "FG1 ON|OFF|AUTO  fill group 1 control\n"
    "FG2 ON|OFF|AUTO  fill group 2 control\n"
    "FG1 STATUS       FG1:RUNNING(AUTO) T1:FILLING 45s\n"
    "T1 STATUS        tank 1 level (EMPTY/FILLING/FULL)\n"
    "T2 STATUS        tank 2 level\n"
    // ── Irrigation groups ─────────────────────────────────────────────
    "G1 ON|OFF        irrigation pump 1 manual\n"
    "G2 ON|OFF        irrigation pump 2 manual\n"
    "G1 STATUS        pump state + open valves\n"
    "PUMP STATUS      all pumps + tanks\n"
    // ── Irrigation schedule ───────────────────────────────────────────
    "SCHEDULES        list all irrigation schedules\n"
    "ISCH <grp> I:<id>,T:HH:MM,R:D|W|O[,D:mask],Q:n.v.m-...\n"
    "ISDL <id>        delete irrigation schedule\n"
    "START <id>       run schedule now\n"
    "STOP             stop running sequence\n"
    // ── Pump schedule (WTT / IPC) ─────────────────────────────────────
    "WSCH FG1 I:<id>,T:HH:MM,R:D|W|O[,D:mask][,M:min]\n"
    "WSCH LIST        list pump schedules\n"
    "WSCH STATUS      next run times\n"
    "DEL FG1:<id>     delete pump schedule\n"
    "DIS/ENA FG1:<id> disable/enable pump schedule\n"
    // ── Node ─────────────────────────────────────────────────────────
    "NODE <id> <cmd>  send command to node\n"
    // ── Setup (Serial only) ───────────────────────────────────────────
    "SETUP WTT ID:FG1,W:W1,T:T1  create fill group\n"
    "SETUP IRR ID:IG1,G:G1,M:1   create irrigation group\n"
    "SETUP NODE IG1,N:1,V:2,3    add node to group\n"
    "SETUP SHOW / SETUP DEL <id>\n"
    "HELP             this list\n";
}
