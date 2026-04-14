// PumpSchedule.h  —  Pump schedule definitions
//
// Shared by PumpScheduleManager and StorageManager.
// Kept separate so StorageManager.h does not need to include
// pump controller headers.

#ifndef PUMP_SCHEDULE_H
#define PUMP_SCHEDULE_H

#include <Arduino.h>
#include <ctime>

// Which physical pump a PumpSchedule targets
enum class PumpTarget : uint8_t {
  WSP = 1,   // Water Source Pump (well pump)
  IPC = 2    // Irrigation Pump
};

// Recurrence  — mirrors the irrigation Schedule convention
//   'O' = one-time
//   'D' = daily
//   'W' = weekly (weekday_mask controls which days)
struct PumpSchedule {
  String    id;              // Unique ID (e.g. "WSCHED001")
  PumpTarget target;         // WSP or IPC
  char      rec       = 'O'; // 'O' | 'D' | 'W'
  String    timeStr;         // "HH:MM"
  uint32_t  durationMs= 0;   // How long to run (0 = manual stop only)
  uint8_t   weekday_mask = 0;// Bitmask (bit 0 = Sun … bit 6 = Sat)
  bool      enabled   = true;
  time_t    next_run  = 0;   // Computed epoch — when to fire next
};

#endif // PUMP_SCHEDULE_H
