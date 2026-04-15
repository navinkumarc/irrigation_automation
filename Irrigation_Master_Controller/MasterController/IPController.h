// IPController.h  —  Irrigation Pump Controller
//
// Manages the pump that draws water from the storage tank and distributes
// it through pipes and valves to irrigation nodes.
//
// ── Modes ──────────────────────────────────────────────────────────────────
//   MANUAL    ON/OFF via command
//   SCHEDULE  Driven by ScheduleManager
//   (No AUTO mode — valve state is the trigger, not sensors)
//
// ── Valve guard (critical safety rule) ────────────────────────────────────
//   The pump must NOT start unless at least minOpenValves are open.
//   If the minimum is not met, start() returns false with a clear error.
//
//   ScheduleManager calls setOpenValveCount(n) whenever valves open/close.
//   IPController checks this count in start() and process().
//
//   If running valves drop below minimum (e.g. all valves closed unexpectedly),
//   the pump is stopped immediately with a warning.
//
// ── Integration with ScheduleManager ──────────────────────────────────────
//   Schedule-based start/stop is entirely owned by ScheduleManager.
//   IPController only enforces hardware constraints (valve guard, max run).

#ifndef IP_CONTROLLER_H
#define IP_CONTROLLER_H

#include "PumpController.h"
#include "Config.h"

class IPController : public IPumpController {
  int           _openValveCount  = 0;     // Updated by ScheduleManager / NodeComm
  int           _minOpenValves   = 1;     // Minimum valves required to run pump
  unsigned long _startedAt       = 0;
  unsigned long _maxRunMs        = 0;     // 0 = unlimited
  unsigned long _valveCheckMs    = 2000;  // How often to re-check valve count
  unsigned long _lastValveCheck  = 0;

public:
  // groupId: "G1" or "G2"
  IPController(const char *groupId = "G1",
               uint8_t pin = IPC_PIN, bool activeHigh = IPC_ACTIVE_HIGH)
    : IPumpController(groupId, pin, activeHigh) {}
  const char* groupId() const { return _name; }

  // ── Valve state reporting ──────────────────────────────────────────────
  // ScheduleManager calls this whenever a valve opens or closes.
  void setOpenValveCount(int count);
  int  getOpenValveCount()  const { return _openValveCount; }

  // ── Config ────────────────────────────────────────────────────────────
  void setMinOpenValves (int n)           { _minOpenValves = max(1, n); }
  int  getMinOpenValves ()          const  { return _minOpenValves; }
  void setMaxRunTime    (unsigned long ms) { _maxRunMs = ms; }

  // ── Control ────────────────────────────────────────────────────────────
  // start() enforces the valve guard: returns false if valve count < min.
  bool start(const String &reason = "") override;
  void stop (const String &reason = "") override;

  // ── Background ─────────────────────────────────────────────────────────
  // Monitors valve count while running; stops pump if count drops below min.
  void process() override;

  String statusString() const override;
};

#endif // IP_CONTROLLER_H
