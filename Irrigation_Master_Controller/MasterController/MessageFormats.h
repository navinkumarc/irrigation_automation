// MessageFormats.h  —  Common message format definitions
//
// Single source of truth for ALL message formats used across:
//   • Master → Node   (LoRa commands via LoRaComm / NodeCommunication)
//   • Node → Master   (LoRa telemetry, auto-close, ACK)
//   • Master → User   (alerts and status via UserCommunication)
//
// ═══════════════════════════════════════════════════════════════════════
//  MASTER → NODE  (LoRa command wire format)
// ───────────────────────────────────────────────────────────────────────
//  CMD|MID=<msgId>|<TYPE>|N=<nodeId>,S=<schedId>,I=<seqIndex>[,T=<ms>]
//
//  Command types (TYPE field):
//    OPEN    open a valve            (T=duration_ms required)
//    CLOSE   close a valve
//    PING    connectivity check
//    STATUS  request telemetry
//
//  Example:
//    CMD|MID=42|OPEN|N=1,S=SCH001,I=0,T=300000
//    CMD|MID=43|CLOSE|N=1,S=SCH001,I=0
//    CMD|MID=44|PING|N=2,S=,I=0
//
// ═══════════════════════════════════════════════════════════════════════
//  NODE → MASTER  (LoRa response / telemetry wire format)
// ───────────────────────────────────────────────────────────────────────
//  ACK from node:
//    ACK|<mid>|<cmdType>|<nodeId>|<schedId>|<seqIndex>
//    Example: ACK|42|OPEN|1|SCH001|0
//
//  Telemetry (STAT):
//    STAT|N=<id>,BATT=<pct>,BV=<volts>,SOLV=<volts>[,V=<states>][,M=<moisture>]
//    Example: STAT|N=1,BATT=85,BV=3.7,SOLV=4.2,V=0000
//
//  Auto-close notification:
//    AUTO_CLOSE|N=<id>[,R=<reason>]
//    Example: AUTO_CLOSE|N=1,R=timeout
//
// ═══════════════════════════════════════════════════════════════════════
//  MASTER → USER  (alert / event text sent over SMS/MQTT/BLE/LoRa/Serial)
// ───────────────────────────────────────────────────────────────────────
//  Format: [<SEVERITY>] <EVENT>: <detail>
//  Severity tokens: INFO | WARNING | ERROR
//
//  Standard events:
//    [INFO] Schedule '<id>' started
//    [INFO] Schedule '<id>' completed
//    [ERROR] Schedule '<id>' failed: <reason>
//    [INFO] Node <n>: valve <v> <opened|closed>
//    [INFO] Node <n>: auto-closed (<reason>)
//    [INFO] Node <n>: battery <pct>% (<volts>V)
//    [WARNING] <message>
//    [ERROR] <message>
//    [INFO] System booted: ch=<channel> bearer=<bearer> heap=<kb>KB

#ifndef MESSAGE_FORMATS_H
#define MESSAGE_FORMATS_H

#include <Arduino.h>

// ─── Separator used in all LoRa wire messages ─────────────────────────────────
#define MSG_SEP   "|"
#define MSG_KV    "="
#define MSG_DELIM ","

// ─── Master → Node command prefix ────────────────────────────────────────────
#define MSG_CMD_PREFIX   "CMD"

// ─── Command type tokens ──────────────────────────────────────────────────────
#define CMD_OPEN   "OPEN"
#define CMD_CLOSE  "CLOSE"
#define CMD_PING   "PING"
#define CMD_STATUS "STATUS"

// ─── Node → Master prefix tokens ─────────────────────────────────────────────
#define MSG_ACK_PREFIX        "ACK"
#define MSG_STAT_PREFIX       "STAT"
#define MSG_AUTO_CLOSE_PREFIX "AUTO_CLOSE"

// ─── Telemetry field keys ─────────────────────────────────────────────────────
#define KEY_MID      "MID"     // Message ID
#define KEY_NODE_ID  "N"       // Node ID
#define KEY_SCHED_ID "S"       // Schedule ID
#define KEY_SEQ_IDX  "I"       // Sequence index
#define KEY_DURATION "T"       // Duration in ms
#define KEY_BATTERY  "BATT"    // Battery percentage
#define KEY_BATT_V   "BV"      // Battery voltage
#define KEY_SOLAR_V  "SOLV"    // Solar voltage
#define KEY_VALVES   "V"       // Valve states string
#define KEY_MOISTURE "M"       // Moisture levels string
#define KEY_REASON   "R"       // Reason string

// ─── User alert severity tokens ───────────────────────────────────────────────
#define SEV_INFO    "INFO"
#define SEV_WARNING "WARNING"
#define SEV_ERROR   "ERROR"

// ═══════════════════════════════════════════════════════════════════════════════
// MsgFmt  —  static builder/parser for all message formats
// ═══════════════════════════════════════════════════════════════════════════════

class MsgFmt {
public:

  // ── Master → Node commands ─────────────────────────────────────────────────

  // Build: CMD|MID=<mid>|OPEN|N=<node>,S=<sched>,I=<idx>,T=<ms>
  static String cmdOpen(uint32_t mid, int nodeId,
                        const String &schedId, int seqIdx, uint32_t durationMs) {
    return String(MSG_CMD_PREFIX MSG_SEP KEY_MID MSG_KV) + String(mid)
         + MSG_SEP CMD_OPEN
         + MSG_SEP KEY_NODE_ID MSG_KV + String(nodeId)
         + MSG_DELIM KEY_SCHED_ID MSG_KV + schedId
         + MSG_DELIM KEY_SEQ_IDX MSG_KV + String(seqIdx)
         + MSG_DELIM KEY_DURATION MSG_KV + String(durationMs);
  }

  // Build: CMD|MID=<mid>|CLOSE|N=<node>,S=<sched>,I=<idx>
  static String cmdClose(uint32_t mid, int nodeId,
                         const String &schedId, int seqIdx) {
    return String(MSG_CMD_PREFIX MSG_SEP KEY_MID MSG_KV) + String(mid)
         + MSG_SEP CMD_CLOSE
         + MSG_SEP KEY_NODE_ID MSG_KV + String(nodeId)
         + MSG_DELIM KEY_SCHED_ID MSG_KV + schedId
         + MSG_DELIM KEY_SEQ_IDX MSG_KV + String(seqIdx);
  }

  // Build: CMD|MID=<mid>|PING|N=<node>,S=,I=0
  static String cmdPing(uint32_t mid, int nodeId) {
    return String(MSG_CMD_PREFIX MSG_SEP KEY_MID MSG_KV) + String(mid)
         + MSG_SEP CMD_PING
         + MSG_SEP KEY_NODE_ID MSG_KV + String(nodeId)
         + MSG_DELIM KEY_SCHED_ID MSG_KV
         + MSG_DELIM KEY_SEQ_IDX MSG_KV "0";
  }

  // Build: CMD|MID=<mid>|STATUS|N=<node>,S=,I=0
  static String cmdStatus(uint32_t mid, int nodeId) {
    return String(MSG_CMD_PREFIX MSG_SEP KEY_MID MSG_KV) + String(mid)
         + MSG_SEP CMD_STATUS
         + MSG_SEP KEY_NODE_ID MSG_KV + String(nodeId)
         + MSG_DELIM KEY_SCHED_ID MSG_KV
         + MSG_DELIM KEY_SEQ_IDX MSG_KV "0";
  }

  // Generic builder for sendWithAck() — preserves existing LoRaComm logic
  static String buildNodeCmd(uint32_t mid, const String &cmdType, int nodeId,
                             const String &schedId, int seqIdx, uint32_t durationMs = 0) {
    String s = String(MSG_CMD_PREFIX MSG_SEP KEY_MID MSG_KV) + String(mid)
             + MSG_SEP + cmdType
             + MSG_SEP KEY_NODE_ID MSG_KV + String(nodeId)
             + MSG_DELIM KEY_SCHED_ID MSG_KV + schedId
             + MSG_DELIM KEY_SEQ_IDX MSG_KV + String(seqIdx);
    if (cmdType == CMD_OPEN && durationMs > 0)
      s += MSG_DELIM KEY_DURATION MSG_KV + String(durationMs);
    return s;
  }

  // ── Node → Master: extract a key=value field from a pipe-delimited message ──

  // Returns value for "KEY=<value>" from a comma-delimited field section.
  // e.g. extractField("STAT|N=1,BATT=85,BV=3.7", "BATT") → "85"
  static String extractField(const String &msg, const char *key) {
    String needle = String(key) + MSG_KV;
    int pos = msg.indexOf(needle);
    if (pos < 0) return "";
    int start = pos + needle.length();
    int end = msg.indexOf(MSG_DELIM, start);
    if (end < 0) end = msg.indexOf(MSG_SEP, start);
    if (end < 0) end = msg.length();
    return msg.substring(start, end);
  }

  // ── Master → User event alerts ─────────────────────────────────────────────

  static String alertScheduleStarted(const String &id) {
    return "[" SEV_INFO "] Schedule '" + id + "' started";
  }

  static String alertScheduleCompleted(const String &id) {
    return "[" SEV_INFO "] Schedule '" + id + "' completed";
  }

  static String alertScheduleFailed(const String &id, const String &reason) {
    return "[" SEV_ERROR "] Schedule '" + id + "' failed: " + reason;
  }

  static String alertValveOpen(int nodeId, int valveId, uint32_t durationMs) {
    return "[" SEV_INFO "] Node " + String(nodeId)
         + ": valve " + String(valveId)
         + " opened (" + String(durationMs / 1000) + "s)";
  }

  static String alertValveClose(int nodeId, int valveId) {
    return "[" SEV_INFO "] Node " + String(nodeId)
         + ": valve " + String(valveId) + " closed";
  }

  static String alertAutoClose(int nodeId, const String &reason) {
    return "[" SEV_INFO "] Node " + String(nodeId)
         + ": auto-closed (" + reason + ")";
  }

  static String alertNodeBattery(int nodeId, int pct, float volts) {
    char buf[64];
    snprintf(buf, sizeof(buf),
      "[" SEV_INFO "] Node %d: battery %d%% (%.2fV)", nodeId, pct, volts);
    return String(buf);
  }

  static String alertNodeTelemetry(int nodeId, int battPct,
                                   float battV, float solarV,
                                   const String &valveStates) {
    char buf[128];
    snprintf(buf, sizeof(buf),
      "[" SEV_INFO "] Node %d: BATT=%d%% BV=%.2f SOLV=%.2f V=%s",
      nodeId, battPct, battV, solarV, valveStates.c_str());
    return String(buf);
  }

  static String alertWarning(const String &msg) {
    return "[" SEV_WARNING "] " + msg;
  }

  static String alertError(const String &msg) {
    return "[" SEV_ERROR "] " + msg;
  }

  static String alertBoot(const String &activeChannel,
                          const String &bearer, uint32_t freeHeap) {
    return "[" SEV_INFO "] System booted: ch=" + activeChannel
         + " bearer=" + bearer
         + " heap=" + String(freeHeap / 1024) + "KB";
  }
};

#endif // MESSAGE_FORMATS_H
