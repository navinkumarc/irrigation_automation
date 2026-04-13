// ScheduleManager.cpp
#include "ScheduleManager.h"
#include "UserCommunication.h"
#include <ArduinoJson.h>

// Forward reference to global loraComm (defined in IrrigationController.ino)
extern LoRaComm loraComm;

// Forward reference to global userComm (fallback if not initialized)
extern UserCommunication userComm;

ScheduleManager::ScheduleManager() : userComm(nullptr) {}

/**
 * Initialize with UserCommunication pointer
 */
void ScheduleManager::init(UserCommunication* comm) {
  userComm = comm;
  if (comm != nullptr) {
    Serial.println("[ScheduleManager] ✓ Initialized with UserCommunication");
  } else {
    Serial.println("[ScheduleManager] ⚠ Initialized without UserCommunication (will use extern)");
  }
}

void ScheduleManager::setPump(bool on) {
  pinMode(PUMP_PIN, OUTPUT);
  if (PUMP_ACTIVE_HIGH) {
    digitalWrite(PUMP_PIN, on ? HIGH : LOW);
  } else {
    digitalWrite(PUMP_PIN, on ? LOW : HIGH);
  }
  Serial.printf("[Pump] %s\n", on ? "ON" : "OFF");
}

bool ScheduleManager::openNode(int node, int idx, uint32_t duration) {
  Serial.printf("[Schedule] Opening node %d (idx %d, duration %lu ms)\n", node, idx, duration);
  return loraComm.sendWithAck("OPEN", node, "", idx, duration);
}

bool ScheduleManager::closeNode(int node, int idx) {
  Serial.printf("[Schedule] Closing node %d (idx %d)\n", node, idx);
  return loraComm.sendWithAck("CLOSE", node, "", idx, 0);
}

/**
 * Send notification through UserCommunication
 * Falls back to extern userComm if local pointer not set
 */
void ScheduleManager::notifyStatus(const String &message) {
  Serial.printf("[Schedule] Status: %s\n", message.c_str());
  
  // Use instance pointer if available
  if (userComm != nullptr) {
    userComm->sendAlert(message, "INFO");
  } else {
    // Fallback to global extern userComm
    ::userComm.sendAlert(message, "INFO");
  }
}

bool ScheduleManager::parseCompact(const String &compact, Schedule &s) {
  // Reset schedule to defaults
  s.id = "";
  s.rec = 'O';
  s.start_epoch = 0;
  s.timeStr = "";
  s.weekday_mask = 0;
  s.seq.clear();
  s.pump_on_before_ms = PUMP_ON_LEAD_DEFAULT_MS;
  s.pump_off_after_ms = PUMP_OFF_DELAY_DEFAULT_MS;
  s.enabled = true;
  s.next_run_epoch = 0;
  s.ts = 0;
  
  int p = compact.indexOf("SCH|");
  String body = (p >= 0) ? compact.substring(p + 4) : compact;
  body.trim();
  
  int pos = 0;
  while (pos < (int)body.length()) {
    int comma = body.indexOf(',', pos);
    String token = (comma == -1) ? body.substring(pos) : body.substring(pos, comma);
    token.trim();
    
    int eq = token.indexOf('=');
    if (eq > 0) {
      String k = token.substring(0, eq);
      String v = token.substring(eq + 1);
      k.trim();
      v.trim();
      
      if (k == "ID") {
        s.id = v;
      } else if (k == "REC") {
        // FIXED: Use signed char instead of char for ArduinoJson compatibility
        s.rec = v.length() ? v.charAt(0) : 'O';
      } else if (k == "T") {
        s.timeStr = v;
      } else if (k == "SEQ") {
        String seqs = v;
        int spos = 0;
        while (spos < (int)seqs.length()) {
          int semi = seqs.indexOf(';', spos);
          String pair = (semi == -1) ? seqs.substring(spos) : seqs.substring(spos, semi);
          int colon = pair.indexOf(':');
          if (colon > 0) {
            SeqStep st;
            st.node_id = pair.substring(0, colon).toInt();
            st.duration_ms = (uint32_t)pair.substring(colon + 1).toInt() * 1000UL;
            s.seq.push_back(st);
          }
          if (semi == -1) break;
          spos = semi + 1;
        }
      } else if (k == "WD") {
        String tmp = v;
        tmp.toUpperCase();
        int sp = 0;
        while (sp < (int)tmp.length()) {
          int cm = tmp.indexOf(',', sp);
          String dayStr = (cm == -1) ? tmp.substring(sp) : tmp.substring(sp, cm);
          // Parse day strings if needed
          if (cm == -1) break;
          sp = cm + 1;
        }
      }
    }
    
    if (comma == -1) break;
    pos = comma + 1;
  }
  
  return true;
}

bool ScheduleManager::parseJSON(const String &json, Schedule &s) {
  DynamicJsonDocument doc(2048);
  
  if (deserializeJson(doc, json) != DeserializationError::Ok) {
    Serial.println("[Schedule] JSON parse error");
    return false;
  }
  
  s.id = doc["id"] | "";
  // FIXED: Use signed char instead of char for JSON
  s.rec = (doc["rec"].as<String>().length() > 0) ? doc["rec"].as<String>().charAt(0) : 'O';
  s.timeStr = doc["time"] | "";
  s.enabled = doc["enabled"] | true;
  s.pump_on_before_ms = doc["pump_on_before"] | PUMP_ON_LEAD_DEFAULT_MS;
  s.pump_off_after_ms = doc["pump_off_after"] | PUMP_OFF_DELAY_DEFAULT_MS;
  
  s.seq.clear();
  JsonArray seqArray = doc["seq"].as<JsonArray>();
  for (JsonObject item : seqArray) {
    SeqStep st;
    st.node_id = item["node"] | 0;
    st.duration_ms = (uint32_t)(item["duration"] | 0) * 1000UL;
    s.seq.push_back(st);
  }
  
  return true;
}

void ScheduleManager::startIfDue() {
  // Verify UserCommunication is initialized
  if (userComm == nullptr) {
    Serial.println("[ScheduleManager] ⚠ UserComm not initialized, using global extern");
  }
  
  Serial.println("[Schedule] Checking if schedule is due...");
  notifyStatus("Schedule check initiated");
  
  // Add your schedule start logic here
  // Example:
  // if (time_is_due) {
  //   notifyStatus("Schedule started");
  // }
}