// PumpScheduleManager.h  —  Schedule-based pump control
//
// Manages timed start/stop for both pump controllers.
// Schedules are loaded from LittleFS (/pump_schedules/*.json) at boot
// and persisted via StorageManager.
//
// ── Schedule recurrence ────────────────────────────────────────────────────
//   'O' one-time  — fires once, then auto-disables
//   'D' daily     — fires every day at timeStr
//   'W' weekly    — fires on selected weekdays (weekday_mask)
//
// ── Integration ────────────────────────────────────────────────────────────
//   PumpScheduleManager holds pointers to WSPController and IPController.
//   It calls start(reason) / stop(reason) on the appropriate controller.
//   The controllers enforce their own hardware rules (valve guard, sensors).
//
// ── Commands (via UserCommunication → pumpCommandCallback) ─────────────────
//   PSCHED LIST                — list all pump schedules
//   PSCHED ADD WSP|IPC HH:MM D|W|O [duration_min] [WD=MON,WED,FRI]
//   PSCHED DEL <id>            — delete a schedule
//   PSCHED ENABLE  <id>        — enable
//   PSCHED DISABLE <id>        — disable
//   PSCHED STATUS              — next scheduled runs

#ifndef PUMP_SCHEDULE_MANAGER_H
#define PUMP_SCHEDULE_MANAGER_H

#include <Arduino.h>
#include <vector>
#include "PumpSchedule.h"

// Forward declarations — keep headers clean
class WSPController;
class IPController;
class UserCommunication;
class StorageManager;

class PumpScheduleManager {
  std::vector<PumpSchedule> schedules;

  WSPController     *wsp     = nullptr;
  IPController      *ipc     = nullptr;
  UserCommunication *userComm= nullptr;
  StorageManager    *storage = nullptr;

  // Running pump tracking (for duration-based auto-stop)
  PumpTarget  runningTarget  = PumpTarget::WSP;
  String      runningSchedId;
  unsigned long runStartMs   = 0;
  uint32_t    runDurationMs  = 0;    // 0 = no auto-stop
  bool        pumpRunning    = false;

  // Internal helpers
  void computeNextRun(PumpSchedule &s);
  void fireSchedule  (PumpSchedule &s);
  void stopRunning   (const String &reason);
  String generateId  (PumpTarget t);
  uint8_t parseWeekdays(const String &wd);
  void sendAlert     (const String &msg);

public:
  PumpScheduleManager() = default;

  // ── Setup ────────────────────────────────────────────────────────────────
  void init(WSPController *w, IPController *i,
            UserCommunication *uc, StorageManager *st);

  // ── Persistence ──────────────────────────────────────────────────────────
  void loadSchedules();
  void saveSchedule  (const PumpSchedule &s);
  void deleteScheduleFile(const String &id);

  // ── Background — call every loop() ───────────────────────────────────────
  void process();

  // ── CRUD ─────────────────────────────────────────────────────────────────
  String addSchedule   (PumpTarget t, const String &timeStr, char rec,
                        uint32_t durationMs, uint8_t weekdays = 0);
  bool   deleteSchedule(const String &id);
  bool   enableSchedule(const String &id, bool en);

  // ── Query ─────────────────────────────────────────────────────────────────
  String listText()   const;
  String statusText() const;

  // ── Command parser (called from pumpCommandCallback) ──────────────────────
  // Returns a response string. Input already upper-cased.
  String handleCommand(const String &up, const String &raw);
};

#endif // PUMP_SCHEDULE_MANAGER_H
