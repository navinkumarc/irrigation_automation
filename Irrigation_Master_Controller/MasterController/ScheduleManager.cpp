// ScheduleManager.cpp
#include "ScheduleManager.h"
#include "UserCommunication.h"
#include "IPController.h"
#include "IrrigationSequencer.h"
#include "Utils.h"
#include <ArduinoJson.h>

ScheduleManager::ScheduleManager() : userComm(nullptr), nodeComm(nullptr),
                                      ipCtrl(nullptr), sequencer(nullptr),
                                      openValveCount(0) {}

/**
 * Initialize with UserCommunication pointer
 */
void ScheduleManager::init(UserCommunication* comm, NodeCommunication* nc,
                            IPController* ipc, IrrigationSequencer* seq) {
  userComm   = comm;
  nodeComm   = nc;
  ipCtrl     = ipc;
  sequencer  = seq;
  if (comm) Serial.println("[ScheduleManager] ✓ UserCommunication set");
  if (nc)   Serial.println("[ScheduleManager] ✓ NodeCommunication set");
  if (ipc)  Serial.println("[ScheduleManager] ✓ IPController set");
  if (seq)  Serial.println("[ScheduleManager] ✓ IrrigationSequencer set");
}

bool ScheduleManager::startIrrigationPump(const String &reason) {
  if (!ipCtrl) { Serial.println("[Schedule] ❌ IPController not set"); return false; }
  return ipCtrl->start(reason);
}

void ScheduleManager::stopIrrigationPump(const String &reason) {
  if (ipCtrl) ipCtrl->stop(reason);
}

void ScheduleManager::valveOpened() {
  openValveCount++;
  if (ipCtrl) ipCtrl->setOpenValveCount(openValveCount);
}

void ScheduleManager::valveClosed() {
  if (openValveCount > 0) openValveCount--;
  if (ipCtrl) ipCtrl->setOpenValveCount(openValveCount);
}

void ScheduleManager::setOpenValveCount(int n) {
  openValveCount = max(0, n);
  if (ipCtrl) ipCtrl->setOpenValveCount(openValveCount);
}

bool ScheduleManager::openNode(int node, int idx, uint32_t duration) {
  Serial.printf("[Schedule] Opening node %d (idx %d, duration %lu ms)\n", node, idx, duration);
  if (!nodeComm) { Serial.println("[Schedule] ❌ NodeCommunication not set"); return false; }
  return nodeComm->sendValveOpen(node, "", idx, duration);
}

bool ScheduleManager::closeNode(int node, int idx) {
  Serial.printf("[Schedule] Closing node %d (idx %d)\n", node, idx);
  if (!nodeComm) { Serial.println("[Schedule] ❌ NodeCommunication not set"); return false; }
  return nodeComm->sendValveClose(node, "", idx);
}

/**
 * Send notification through UserCommunication
 * Falls back to extern userComm if local pointer not set
 */
void ScheduleManager::notifyStatus(const String &message) {
  Serial.printf("[Schedule] Status: %s\n", message.c_str());
  // userComm pointer is set in init() via commMgr.getUserComm().
  // If not set, log to Serial only — no global fallback needed.
  if (userComm != nullptr) {
    userComm->sendAlert(message, "INFO");
  }
}

/**
 * Parse compact schedule format.
 *
 * SEQ token format: "N:V:D" triples separated by semicolons.
 *   N = node_id   (integer)
 *   V = valve_id  (integer)  — FIX: was previously ignored
 *   D = duration  (seconds)
 *
 * Example: SEQ=1:2:300;1:3:600  → node 1 valve 2 for 5 min, then node 1 valve 3 for 10 min
 */
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

      // Short keys: I=id R=rec T=time D=day_mask M=dur_min Q=sequence
      // Long keys:  ID    REC    T      WD         —         SEQ  (kept for compat)
      if (k == "ID" || k == "I") {
        s.id = v;
      } else if (k == "REC" || k == "R") {
        s.rec = v.length() ? v.charAt(0) : 'O';
      } else if (k == "T") {
        s.timeStr = v;
      } else if (k == "M") {
        // M = duration in minutes (used by irrigation schedule for pump timing)
        s.pump_on_before_ms  = IPC_PUMP_START_DELAY_MS;
        s.pump_off_after_ms  = IPC_PUMP_STOP_DELAY_MS;
      } else if (k == "PON") {
        s.pump_on_before_ms = (uint32_t)v.toInt();
      } else if (k == "POFF") {
        s.pump_off_after_ms = (uint32_t)v.toInt();
      } else if (k == "SEQ" || k == "Q") {
        // FIX: Parse "N:V:D" triples. Previously only handled "N:D" (2-field),
        // silently ignoring valve_id. Now supports both 2-field (N:D, valve=0)
        // and 3-field (N:V:D) formats for backward compatibility.
        String seqs = v;
        int spos = 0;
        while (spos < (int)seqs.length()) {
          int semi = seqs.indexOf(';', spos);
          String triple = (semi == -1) ? seqs.substring(spos) : seqs.substring(spos, semi);
          triple.trim();

          // Support two step formats:
          //   Short: N.V.M  (dot-sep, M = minutes)   e.g. 1.2.20
          //   Long:  N:V:S  (colon-sep, S = seconds)  e.g. 1:2:1200
          char sep = (triple.indexOf('.') > 0) ? '.' : ':';
          bool inMinutes = (sep == '.');
          int c1 = triple.indexOf(sep);
          if (c1 > 0) {
            SeqStep st;
            st.node_id = (uint8_t)triple.substring(0, c1).toInt();

            int c2 = triple.indexOf(sep, c1 + 1);
            if (c2 > 0) {
              st.valve_id    = (uint8_t)triple.substring(c1 + 1, c2).toInt();
              uint32_t val   = (uint32_t)triple.substring(c2 + 1).toInt();
              st.duration_ms = inMinutes ? val * 60000UL : val * 1000UL;
            } else {
              st.valve_id    = 0;
              uint32_t val   = (uint32_t)triple.substring(c1 + 1).toInt();
              st.duration_ms = inMinutes ? val * 60000UL : val * 1000UL;
            }
            s.seq.push_back(st);
          }

          if (semi == -1) break;
          spos = semi + 1;
        }
      } else if (k == "WD" || k == "D") {
        // Parse weekday bitmask or comma-separated day names
        // Numeric: WD=62  (bitmask)
        // Named:   WD=MON,WED,FRI
        String tmp = v;
        tmp.trim();
        if (tmp.length() > 0 && isdigit(tmp.charAt(0))) {
          s.weekday_mask = (uint8_t)tmp.toInt();
        } else {
          tmp.toUpperCase();
          s.weekday_mask = 0;
          const char* days[] = {"SUN","MON","TUE","WED","THU","FRI","SAT"};
          for (int d = 0; d < 7; d++) {
            if (tmp.indexOf(days[d]) >= 0) s.weekday_mask |= (1 << d);
          }
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
  s.rec = (doc["rec"].as<String>().length() > 0) ? doc["rec"].as<String>().charAt(0) : 'O';
  s.timeStr = doc["time"] | "";
  s.enabled = doc["enabled"] | true;
  s.pump_on_before_ms = doc["pump_on_before"] | PUMP_ON_LEAD_DEFAULT_MS;
  s.pump_off_after_ms = doc["pump_off_after"] | PUMP_OFF_DELAY_DEFAULT_MS;

  s.seq.clear();
  JsonArray seqArray = doc["seq"].as<JsonArray>();
  for (JsonObject item : seqArray) {
    SeqStep st;
    st.node_id    = item["node"]     | 0;
    st.valve_id   = item["valve"]    | 0;   // FIX: was missing
    st.duration_ms = (uint32_t)(item["duration"] | 0) * 1000UL;
    s.seq.push_back(st);
  }

  return true;
}

void ScheduleManager::startIfDue() {
  if (!sequencer || sequencer->isRunning()) return;

  time_t now = time(nullptr);
  if (now < 1000000) return;  // RTC not set

  for (auto &sched : schedules) {
    if (!sched.enabled) continue;
    if (sched.next_run_epoch == 0) continue;
    if (now < sched.next_run_epoch) continue;

    // Schedule is due — validate and start
    if (sched.seq.empty()) {
      Serial.println("[ScheduleManager] ⚠ Schedule " + sched.id + " has no steps");
      continue;
    }

    Serial.println("[ScheduleManager] ▶ Starting schedule: " + sched.id);
    if (sequencer->start(sched.seq, sched.id)) {
      if (userComm) userComm->sendAlert(
        "[INFO] Schedule " + sched.id + " started — " + String(sched.seq.size()) + " step(s)",
        "INFO");
      // Advance next_run for recurring schedules
      if (sched.rec == 'D') {
        sched.next_run_epoch += 86400;
      } else if (sched.rec == 'W') {
        int hour = 0, min = 0;
        parseTimeHHMM(sched.timeStr, hour, min);
        sched.next_run_epoch = nextWeekdayOccurrence(
          now + 60, sched.weekday_mask, hour, min);
      } else {
        // One-time: disable after firing
        sched.enabled = false;
      }
    } else {
      if (userComm) userComm->sendAlert(
        "[ERROR] Schedule " + sched.id + " failed to start", "ERROR");
    }
    break;  // One schedule at a time
  }
}

void ScheduleManager::process() {
  if (sequencer) sequencer->process();
  startIfDue();
}
