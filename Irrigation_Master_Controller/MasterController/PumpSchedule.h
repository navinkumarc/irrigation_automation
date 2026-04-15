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
  WSP = 1,   // Water Source Pump (well pump)   — addressed as W1/W2/W3
  IPC = 2    // Irrigation Pump + node group    — addressed as G1/G2
};

// Recurrence
//   'O' = one-time
//   'D' = daily
//   'W' = weekly (day_mask bitmask: Mon=2 Tue=4 Wed=8 Thu=16 Fri=32 Sat=64 Sun=1)
struct PumpSchedule {
  String    id;              // Schedule ID (e.g. "S1")
  String    pumpId;          // Pump/group address: W1/W2/W3 or G1/G2
  PumpTarget target;         // WSP or IPC
  char      rec       = 'O'; // 'O' | 'D' | 'W'
  String    timeStr;         // "HH:MM"
  uint32_t  durationMs= 0;   // How long to run (0 = manual stop only)
  uint8_t   weekday_mask = 0;// Bitmask (bit 0 = Sun … bit 6 = Sat)
  bool      enabled   = true;
  time_t    next_run  = 0;   // Computed epoch — when to fire next
};

#endif // PUMP_SCHEDULE_H
