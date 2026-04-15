// PumpScheduleManager.cpp  —  Schedule-based pump control
#include "PumpScheduleManager.h"
#include "Config.h"
#include "WSPController.h"
#include "IPController.h"
#include "UserCommunication.h"
#include "StorageManager.h"
#include "Utils.h"
#include "MessageFormats.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

// ─── init() ──────────────────────────────────────────────────────────────────
void PumpScheduleManager::init(WSPController *w, IPController *i,
                                UserCommunication *uc, StorageManager *st) {
  wsp      = w;
  ipc      = i;
  userComm = uc;
  storage  = st;
  Serial.println("[PumpSched] Initialized");
}

// ─── sendAlert() ─────────────────────────────────────────────────────────────
void PumpScheduleManager::sendAlert(const String &msg) {
  Serial.println("[PumpSched] " + msg);
  if (userComm) userComm->sendAlert(msg, SEV_INFO);
}

// ─── generateId() ────────────────────────────────────────────────────────────
String PumpScheduleManager::generateId(PumpTarget t) {
  String prefix = (t == PumpTarget::WSP) ? "WS" : "IP";
  return prefix + String(millis() % 10000);
}

// ─── computeNextRun() ────────────────────────────────────────────────────────
void PumpScheduleManager::computeNextRun(PumpSchedule &s) {
  if (!s.enabled) { s.next_run = 0; return; }
  int hour = 0, minute = 0;
  if (!parseTimeHHMM(s.timeStr, hour, minute)) { s.next_run = 0; return; }

  time_t now = time(nullptr);
  struct tm tmnow;
  localtime_r(&now, &tmnow);

  if (s.rec == 'O') {
    // One-time: use today if time hasn't passed, else tomorrow
    struct tm t = tmnow;
    t.tm_hour = hour; t.tm_min = minute; t.tm_sec = 0;
    time_t candidate = mktime(&t);
    s.next_run = (candidate > now) ? candidate : candidate + 86400;

  } else if (s.rec == 'D') {
    struct tm t = tmnow;
    t.tm_hour = hour; t.tm_min = minute; t.tm_sec = 0;
    time_t candidate = mktime(&t);
    s.next_run = (candidate > now) ? candidate : candidate + 86400;

  } else if (s.rec == 'W') {
    s.next_run = nextWeekdayOccurrence(now, s.weekday_mask, hour, minute);
  }
}

// ─── process() ───────────────────────────────────────────────────────────────
void PumpScheduleManager::process() {
  time_t now = time(nullptr);
  if (now < 1000000) return;  // RTC not set yet

  // Auto-stop running pump when duration expires
  if (pumpRunning && runDurationMs > 0 &&
      millis() - runStartMs >= runDurationMs) {
    stopRunning("schedule-duration");
  }

  // Check each schedule
  for (auto &s : schedules) {
    if (!s.enabled || s.next_run == 0) continue;
    if (now >= s.next_run) {
      fireSchedule(s);
      // Advance next_run
      if (s.rec == 'O') {
        s.enabled = false;
        saveSchedule(s);
      } else {
        computeNextRun(s);
        saveSchedule(s);
      }
    }
  }
}

// ─── fireSchedule() ──────────────────────────────────────────────────────────
void PumpScheduleManager::fireSchedule(PumpSchedule &s) {
  String label = (s.target == PumpTarget::WSP) ? "Well pump" : "Irrigation pump";
  sendAlert("[INFO] Schedule " + s.id + ": " + label + " starting");

  bool ok = false;
  if (s.target == PumpTarget::WSP && wsp) {
    wsp->setMode(PumpMode::SCHEDULE);
    ok = wsp->start("sched:" + s.id);
  } else if (s.target == PumpTarget::IPC && ipc) {
    ipc->setMode(PumpMode::SCHEDULE);
    ok = ipc->start("sched:" + s.id);
  }

  if (ok && s.durationMs > 0) {
    pumpRunning   = true;
    runningTarget = s.target;
    runningSchedId= s.id;
    runStartMs    = millis();
    runDurationMs = s.durationMs;
  }
}

// ─── stopRunning() ───────────────────────────────────────────────────────────
void PumpScheduleManager::stopRunning(const String &reason) {
  if (!pumpRunning) return;
  if (runningTarget == PumpTarget::WSP && wsp) wsp->stop(reason);
  if (runningTarget == PumpTarget::IPC && ipc) ipc->stop(reason);
  sendAlert("[INFO] Schedule " + runningSchedId + ": pump stopped (" + reason + ")");
  pumpRunning = false;
  runDurationMs = 0;
}

// ─── addSchedule() ───────────────────────────────────────────────────────────
String PumpScheduleManager::addSchedule(PumpTarget t, const String &timeStr,
                                         char rec, uint32_t durationMs,
                                         uint8_t weekdays) {
  PumpSchedule s;
  s.id           = generateId(t);
  s.target       = t;
  s.timeStr      = timeStr;
  s.rec          = rec;
  s.durationMs   = durationMs;
  s.weekday_mask = weekdays;
  s.enabled      = true;
  computeNextRun(s);
  schedules.push_back(s);
  saveSchedule(s);
  Serial.println("[PumpSched] Added: " + s.id + " at " + timeStr);
  return s.id;
}

// ─── deleteSchedule() ────────────────────────────────────────────────────────
bool PumpScheduleManager::deleteSchedule(const String &id) {
  for (auto it = schedules.begin(); it != schedules.end(); ++it) {
    if (it->id == id) {
      deleteScheduleFile(id);
      schedules.erase(it);
      return true;
    }
  }
  return false;
}

bool PumpScheduleManager::enableSchedule(const String &id, bool en) {
  for (auto &s : schedules) {
    if (s.id == id) {
      s.enabled = en;
      if (en) computeNextRun(s);
      else    s.next_run = 0;
      saveSchedule(s);
      return true;
    }
  }
  return false;
}

// ─── listText() ──────────────────────────────────────────────────────────────
String PumpScheduleManager::listText() const {
  if (schedules.empty()) return "No pump schedules";
  String out;
  for (auto &s : schedules) {
    out += s.id + " ";
    out += (s.target == PumpTarget::WSP) ? "WSP" : "IPC";
    out += " " + s.timeStr;
    out += " rec:" + String(s.rec);
    if (s.durationMs > 0) out += " dur:" + String(s.durationMs/60000) + "min";
    out += s.enabled ? " [ON]" : " [OFF]";
    out += "\n";
  }
  return out;
}

String PumpScheduleManager::statusText() const {
  if (schedules.empty()) return "No pump schedules";
  String out;
  time_t now = time(nullptr);
  for (auto &s : schedules) {
    if (!s.enabled) continue;
    out += s.id + " → ";
    if (s.next_run > 0) {
      long secs = (long)(s.next_run - now);
      if (secs < 0) out += "overdue";
      else if (secs < 3600) out += String(secs/60) + "min";
      else out += String(secs/3600) + "h";
    } else {
      out += "not scheduled";
    }
    out += "\n";
  }
  return out.length() ? out : "No enabled schedules";
}

// ─── parseWeekdays() ─────────────────────────────────────────────────────────
uint8_t PumpScheduleManager::parseWeekdays(const String &wd) {
  String u = wd; u.toUpperCase();
  if (u.length() > 0 && isdigit(u.charAt(0))) return (uint8_t)u.toInt();
  uint8_t mask = 0;
  const char* days[] = {"SUN","MON","TUE","WED","THU","FRI","SAT"};
  for (int d = 0; d < 7; d++)
    if (u.indexOf(days[d]) >= 0) mask |= (1 << d);
  return mask;
}

// ─── handleCommand() ─────────────────────────────────────────────────────────
//
// SHORT COMMAND FORMAT (SMS-optimised, max 160 chars):
//
//   Pump/group schedule:
//     WSCH W1 I:S1,T:06:00,R:D,M:90
//     WSCH W2 I:S2,T:05:30,R:W,D:42,M:120    (D:42 = Mon+Wed+Fri bitmask)
//     WSCH G1 I:S1,T:07:00,R:W,D:42,Q:1.1.20-1.2.20-2.2.10
//
//   Keys:  I=id  T=time  R=rec(O/D/W)  D=day_mask  M=duration_min  Q=sequence
//   Steps: node.valve.minutes  separated by -
//   Day bitmask: Sun=1 Mon=2 Tue=4 Wed=8 Thu=16 Fri=32 Sat=64
//     Mon+Wed+Fri = 2+8+32 = 42
//
//   Control:
//     W1 ON / W1 OFF / W1 AUTO / W1 STATUS
//     G1 ON / G1 OFF / G1 STATUS
//
//   Schedule management:
//     PS LIST           — list all pump schedules
//     PS STATUS         — next run times
//     DEL W1:S1         — delete schedule S1 from W1
//     DIS W1:S1         — disable
//     ENA W1:S1         — enable
//
// up = uppercase, raw = original case
String PumpScheduleManager::handleCommand(const String &up, const String &raw) {

  // ── List / Status ────────────────────────────────────────────────────────
  if (up == "WSCH LIST")   return listText();
  if (up == "WSCH STATUS") return statusText();

  // ── Delete / Enable / Disable  DEL W1:S1 / DIS W1:S1 / ENA W1:S1 ────────
  if (up.startsWith("DEL ") || up.startsWith("DIS ") || up.startsWith("ENA ")) {
    String cmd3 = up.substring(0, 3);
    String arg  = up.substring(4); arg.trim();
    // arg is "W1:S1" or just "S1"
    String schedId = (arg.indexOf(':') >= 0) ? arg.substring(arg.indexOf(':') + 1) : arg;
    if (cmd3 == "DEL") return deleteSchedule(schedId) ? "Del " + schedId : "Not found: " + schedId;
    if (cmd3 == "DIS") return enableSchedule(schedId, false) ? "Dis " + schedId : "Not found: " + schedId;
    if (cmd3 == "ENA") return enableSchedule(schedId, true)  ? "Ena " + schedId : "Not found: " + schedId;
  }

  // ── WSCH W1|W2|W3|G1|G2  I:id,T:HH:MM,R:D|W|O[,D:mask][,M:min][,Q:steps] ─
  if (up.startsWith("WSCH ")) {
    String body = raw.substring(3); body.trim();
    // First token = pump/group address
    int sp = body.indexOf(' ');
    if (sp < 0) return "PS: missing fields";
    String addr = body.substring(0, sp); addr.toUpperCase();
    body = body.substring(sp + 1); body.trim();

    // Determine target
    PumpTarget target = addr.startsWith("W") ? PumpTarget::WSP : PumpTarget::IPC;

    // Parse key:value pairs separated by comma
    String schedId, timeStr, recStr;
    char   rec      = 'O';
    uint8_t dayMask = 0;
    uint32_t durMs  = 0;
    std::vector<SeqStep> seq;

    int pos = 0;
    while (pos < (int)body.length()) {
      int comma = body.indexOf(',', pos);
      String tok = (comma < 0) ? body.substring(pos) : body.substring(pos, comma);
      tok.trim();
      int colon = tok.indexOf(':');
      if (colon > 0) {
        String k = tok.substring(0, colon);  k.toUpperCase();
        String v = tok.substring(colon + 1); v.trim();
        if      (k == "I") schedId = v;
        else if (k == "T") timeStr = v;
        else if (k == "R") { recStr = v; recStr.toUpperCase(); rec = recStr.charAt(0); }
        else if (k == "D") dayMask = (uint8_t)v.toInt();
        else if (k == "M") durMs   = (uint32_t)v.toInt() * 60000UL;
        else if (k == "Q") {
          // Parse steps: node.valve.minutes separated by -
          int spos = 0;
          while (spos < (int)v.length()) {
            int dash = v.indexOf('-', spos);
            String step = (dash < 0) ? v.substring(spos) : v.substring(spos, dash);
            int d1 = step.indexOf('.');
            int d2 = (d1 >= 0) ? step.indexOf('.', d1 + 1) : -1;
            if (d1 > 0 && d2 > d1) {
              SeqStep st;
              st.node_id    = (uint8_t)step.substring(0, d1).toInt();
              st.valve_id   = (uint8_t)step.substring(d1 + 1, d2).toInt();
              st.duration_ms= (uint32_t)step.substring(d2 + 1).toInt() * 60000UL;
              seq.push_back(st);
            }
            if (dash < 0) break;
            spos = dash + 1;
          }
        }
      }
      if (comma < 0) break;
      pos = comma + 1;
    }

    if (schedId.length() == 0) return addr + ": I: (schedule id) required";
    if (timeStr.length() == 0) return addr + ": T: (time HH:MM) required";

    // For IPC groups: store sequence steps via scheduleSeqCallback if registered
    PumpSchedule s;
    s.id           = schedId;
    s.pumpId       = addr;
    s.target       = target;
    s.rec          = rec;
    s.timeStr      = timeStr;
    s.durationMs   = durMs;
    s.weekday_mask = dayMask;
    s.enabled      = true;
    computeNextRun(s);

    // Remove any existing schedule with same id
    deleteSchedule(schedId);

    schedules.push_back(s);
    saveSchedule(s);

    // If this is an IPC group schedule with sequence steps, store them via callback
    if (target == PumpTarget::IPC && !seq.empty() && addIrrSchedCallback) {
      addIrrSchedCallback(schedId, addr, s, seq);
    }

    String resp = "OK " + addr + ":" + schedId + " " + timeStr
                + " R:" + String(rec);
    if (dayMask > 0) resp += " D:" + String(dayMask);
    if (durMs   > 0) resp += " M:" + String(durMs / 60000);
    if (!seq.empty()) resp += " Q:" + String(seq.size()) + "steps";
    return resp;
  }

  return "Cmds: WSCH W1|G1 I:id,T:HH:MM,R:D|W|O[,D:mask][,M:min][,Q:steps] | DEL/DIS/ENA W1:id | WSCH LIST|STATUS";
}

// ─── Persistence ─────────────────────────────────────────────────────────────
void PumpScheduleManager::loadSchedules() {
  if (!LittleFS.exists("/pump_schedules")) {
    LittleFS.mkdir("/pump_schedules");
    Serial.println("[PumpSched] Created /pump_schedules directory");
    return;
  }
  File root = LittleFS.open("/pump_schedules");
  if (!root) return;
  schedules.clear();
  File f = root.openNextFile();
  while (f) {
    if (String(f.name()).endsWith(".json")) {
      String json = f.readString();
      DynamicJsonDocument doc(512);
      if (deserializeJson(doc, json) == DeserializationError::Ok) {
        PumpSchedule s;
        s.id           = doc["id"]       | "";
        s.pumpId       = doc["pump"]      | "";
        s.target       = (PumpTarget)(doc["target"] | 1);
        s.rec          = doc["rec"]       | "O";
        s.timeStr      = doc["time"]      | "";
        s.durationMs   = doc["duration"]  | 0;
        s.weekday_mask = doc["weekdays"]  | 0;
        s.enabled      = doc["enabled"]   | true;
        s.next_run     = doc["next_run"]  | 0;
        if (s.id.length() > 0) {
          computeNextRun(s);  // recompute in case time drifted
          schedules.push_back(s);
          Serial.println("[PumpSched] Loaded: " + s.id);
        }
      }
    }
    f.close();
    f = root.openNextFile();
  }
  root.close();
  Serial.printf("[PumpSched] %d schedule(s) loaded\n", (int)schedules.size());
}

void PumpScheduleManager::saveSchedule(const PumpSchedule &s) {
  DynamicJsonDocument doc(512);
  doc["id"]       = s.id;
  doc["pump"]     = s.pumpId;
  doc["target"]   = (uint8_t)s.target;
  doc["rec"]      = String(s.rec);
  doc["time"]     = s.timeStr;
  doc["duration"] = s.durationMs;
  doc["weekdays"] = s.weekday_mask;
  doc["enabled"]  = s.enabled;
  doc["next_run"] = (long long)s.next_run;
  String json; serializeJson(doc, json);
  String path = "/pump_schedules/" + s.id + ".json";
  File f = LittleFS.open(path, "w");
  if (f) { f.print(json); f.close(); }
}

void PumpScheduleManager::deleteScheduleFile(const String &id) {
  String path = "/pump_schedules/" + id + ".json";
  if (LittleFS.exists(path)) LittleFS.remove(path);
}
