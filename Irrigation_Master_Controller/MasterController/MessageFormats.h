// MessageFormats.h - Compact message formats for SMS/cellular data efficiency
// Designed to minimize data usage for 100 SMS/day limit and cellular data costs
//
// Format convention: TYPE|SUBTYPE|KEY=VAL|KEY=VAL...
// - Use single char keys where possible (N=node, V=valve, T=time, etc.)
// - Use abbreviations (SCH=schedule, CMD=command, BATT=battery)
// - Avoid spaces and verbose descriptions
// - Keep total message length < 160 chars for single SMS

#ifndef MESSAGE_FORMATS_H
#define MESSAGE_FORMATS_H

#include <Arduino.h>

// ========== Message Type Prefixes ==========
#define MSG_EVENT "EVT"        // Normal events
#define MSG_ERROR "ERR"        // Errors
#define MSG_WARNING "WRN"      // Warnings
#define MSG_STATUS "STA"       // Status updates
#define MSG_COMMAND "CMD"      // Command results

// ========== Event Types ==========
#define EVT_BOOT "BOOT"
#define EVT_SCHEDULE_LOADED "SCH_LD"
#define EVT_SCHEDULE_START "SCH_ST"
#define EVT_SCHEDULE_DONE "SCH_DN"
#define EVT_VALVE_OPEN "V_OPN"
#define EVT_VALVE_CLOSE "V_CLS"
#define EVT_PUMP_ON "P_ON"
#define EVT_PUMP_OFF "P_OFF"
#define EVT_AUTO_CLOSE "A_CLS"
#define EVT_COMMAND_OK "C_OK"

// ========== Error Types ==========
#define ERR_SCHEDULE_INVALID "SCH_INV"
#define ERR_COMMAND_FAIL "C_FAIL"
#define ERR_LORA_FAIL "LRA_FL"
#define ERR_VALVE_FAIL "V_FAIL"

// ========== Warning Types ==========
#define WRN_LOW_BATTERY "LO_BAT"
#define WRN_TIMEOUT "TMOUT"

// ========== Status Keys (single char preferred) ==========
#define KEY_NODE "N"           // Node ID
#define KEY_VALVE "V"          // Valve number
#define KEY_BATTERY "B"        // Battery percentage
#define KEY_COMMAND "C"        // Command string
#define KEY_SCHEDULE_ID "S"    // Schedule ID
#define KEY_TIME "T"           // Time in seconds
#define KEY_REASON "R"         // Reason code
#define KEY_LORA "L"          // LoRa status (1/0)
#define KEY_MQTT "M"          // MQTT status (1/0)
#define KEY_WIFI "W"          // WiFi status (1/0)
#define KEY_PPPOS "P"         // PPPoS status (1/0)

// ========== Helper Class for Building Messages ==========
class MessageFormatter {
public:
  // Boot messages
  static String formatBoot(bool loraOk, bool mqttOk, bool networkOk) {
    // Format: EVT|BOOT|L=1|M=0|N=1
    // Example: "EVT|BOOT|L=1|M=1|N=1" (18 chars)
    return String(MSG_EVENT) + "|" + EVT_BOOT +
           "|L=" + (loraOk ? "1" : "0") +
           "|M=" + (mqttOk ? "1" : "0") +
           "|N=" + (networkOk ? "1" : "0");
  }

  // Schedule events
  static String formatScheduleLoaded(const String &schedId) {
    // Format: EVT|SCH_LD|S=id
    // Example: "EVT|SCH_LD|S=SCH001" (20 chars)
    return String(MSG_EVENT) + "|" + EVT_SCHEDULE_LOADED + "|S=" + schedId;
  }

  static String formatScheduleStart(const String &schedId) {
    // Format: EVT|SCH_ST|S=id
    // Example: "EVT|SCH_ST|S=SCH001" (20 chars)
    return String(MSG_EVENT) + "|" + EVT_SCHEDULE_START + "|S=" + schedId;
  }

  static String formatScheduleDone(const String &schedId) {
    // Format: EVT|SCH_DN|S=id
    // Example: "EVT|SCH_DN|S=SCH001" (20 chars)
    return String(MSG_EVENT) + "|" + EVT_SCHEDULE_DONE + "|S=" + schedId;
  }

  static String formatScheduleInvalid() {
    // Format: ERR|SCH_INV
    // Example: "ERR|SCH_INV" (11 chars)
    return String(MSG_ERROR) + "|" + ERR_SCHEDULE_INVALID;
  }

  // Valve events
  static String formatValveOpen(int nodeId, int valveNum, uint32_t durationSec) {
    // Format: EVT|V_OPN|N=id|V=num|T=sec
    // Example: "EVT|V_OPN|N=1|V=2|T=300" (24 chars)
    return String(MSG_EVENT) + "|" + EVT_VALVE_OPEN +
           "|N=" + String(nodeId) +
           "|V=" + String(valveNum) +
           "|T=" + String(durationSec);
  }

  static String formatValveClose(int nodeId, int valveNum) {
    // Format: EVT|V_CLS|N=id|V=num
    // Example: "EVT|V_CLS|N=1|V=2" (18 chars)
    return String(MSG_EVENT) + "|" + EVT_VALVE_CLOSE +
           "|N=" + String(nodeId) +
           "|V=" + String(valveNum);
  }

  static String formatAutoClose(int nodeId) {
    // Format: EVT|A_CLS|N=id
    // Example: "EVT|A_CLS|N=1" (13 chars)
    return String(MSG_EVENT) + "|" + EVT_AUTO_CLOSE + "|N=" + String(nodeId);
  }

  // Pump events
  static String formatPumpOn() {
    // Format: EVT|P_ON
    // Example: "EVT|P_ON" (8 chars)
    return String(MSG_EVENT) + "|" + EVT_PUMP_ON;
  }

  static String formatPumpOff() {
    // Format: EVT|P_OFF
    // Example: "EVT|P_OFF" (9 chars)
    return String(MSG_EVENT) + "|" + EVT_PUMP_OFF;
  }

  // Command results
  static String formatCommandSuccess(int nodeId, const String &cmd) {
    // Format: EVT|C_OK|N=id|C=cmd
    // Example: "EVT|C_OK|N=1|C=OPEN_2" (22 chars)
    return String(MSG_EVENT) + "|" + EVT_COMMAND_OK +
           "|N=" + String(nodeId) +
           "|C=" + cmd;
  }

  static String formatCommandFail(int nodeId, const String &cmd) {
    // Format: ERR|C_FAIL|N=id|C=cmd
    // Example: "ERR|C_FAIL|N=1|C=OPEN_2" (25 chars)
    return String(MSG_ERROR) + "|" + ERR_COMMAND_FAIL +
           "|N=" + String(nodeId) +
           "|C=" + cmd;
  }

  // Battery warnings
  static String formatLowBattery(int nodeId, int battPercent) {
    // Format: WRN|LO_BAT|N=id|B=pct
    // Example: "WRN|LO_BAT|N=1|B=15" (20 chars)
    return String(MSG_WARNING) + "|" + WRN_LOW_BATTERY +
           "|N=" + String(nodeId) +
           "|B=" + String(battPercent);
  }

  // Generic status message builder
  static String buildMessage(const String &type, const String &subtype) {
    // Format: TYPE|SUBTYPE
    return type + "|" + subtype;
  }

  static String buildMessage(const String &type, const String &subtype,
                             const String &key1, const String &val1) {
    // Format: TYPE|SUBTYPE|KEY=VAL
    return type + "|" + subtype + "|" + key1 + "=" + val1;
  }

  static String buildMessage(const String &type, const String &subtype,
                             const String &key1, const String &val1,
                             const String &key2, const String &val2) {
    // Format: TYPE|SUBTYPE|KEY=VAL|KEY=VAL
    return type + "|" + subtype +
           "|" + key1 + "=" + val1 +
           "|" + key2 + "=" + val2;
  }

  static String buildMessage(const String &type, const String &subtype,
                             const String &key1, const String &val1,
                             const String &key2, const String &val2,
                             const String &key3, const String &val3) {
    // Format: TYPE|SUBTYPE|KEY=VAL|KEY=VAL|KEY=VAL
    return type + "|" + subtype +
           "|" + key1 + "=" + val1 +
           "|" + key2 + "=" + val2 +
           "|" + key3 + "=" + val3;
  }
};

// ========== Message Size Estimates ==========
// Boot message:              ~20 chars
// Schedule loaded:           ~20 chars
// Schedule start/done:       ~20 chars
// Valve open:                ~25 chars
// Valve close:               ~18 chars
// Command success/fail:      ~25 chars
// Low battery warning:       ~20 chars
// Auto close:                ~13 chars
//
// Average message size:      ~20 chars
// SMS capacity:              160 chars (single SMS)
// Estimated messages/SMS:    ~8 events per SMS (if concatenated)
// Daily limit:               100 SMS = ~800 events
//
// For critical events only (boot, errors, warnings):
// Estimated usage:           5-10 SMS/day

#endif
