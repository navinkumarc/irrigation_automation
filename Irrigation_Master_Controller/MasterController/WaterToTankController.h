// WaterToTankController.h  —  Water filling process group
//
// Combines one WSPController (well pump) + one TankManager (storage tank)
// into a single operational unit. This is the primary interface for all
// water-source filling operations.
//
// ── What WaterToTankController owns ───────────────────────────────────────────────
//   • Group identity: FG1 or FG2
//   • Pointer to WSPController (pump hardware)
//   • Pointer to TankManager (tank sensors)
//   • Fill logic: AUTO mode wires tank callbacks to pump start/stop
//   • Dry-run protection: stops pump if tank stays EMPTY after timeout
//   • Max-run safety cutoff
//
// ── What moves OUT of WSPController ───────────────────────────────────────
//   • Sensor callbacks (tank empty/full) — now owned by TankManager
//   • AUTO mode logic — now owned by WaterToTankController
//   • WSPController becomes pure pump GPIO (start/stop/process)
//
// ── Modes ─────────────────────────────────────────────────────────────────
//   MANUAL    FG1 ON / FG1 OFF
//   AUTO      FG1 AUTO — pump driven by tank sensor state
//   SCHEDULE  Driven by WaterFillSchedule (via PumpScheduleManager)
//
// ── Commands (any channel) ─────────────────────────────────────────────────
//   FG1 ON        start pump (MANUAL)
//   FG1 OFF       stop pump
//   FG1 AUTO      switch to sensor-driven AUTO mode
//   FG1 STATUS    show pump state + tank state
//
// ── Config commands ────────────────────────────────────────────────────────
//   FG ADD FG1,W:W1,T:T1   create fill group FG1 = pump W1 + tank T1
//   FG LIST                list all fill groups

#ifndef WATER_TO_TANK_CONTROLLER_H
#define WATER_TO_TANK_CONTROLLER_H

#include <Arduino.h>
#include <functional>
#include "PumpController.h"
#include "TankManager.h"
#include "MessageFormats.h"

// Forward declarations
class WSPController;

// ─── Fill group operating mode ────────────────────────────────────────────────
enum class WTTMode {
  MANUAL,    // Direct ON/OFF command
  AUTO,      // Driven by tank sensors
  SCHEDULE   // Driven by PumpScheduleManager
};

// ─── Fill group state ─────────────────────────────────────────────────────────
enum class WTTState {
  IDLE,      // Pump off, waiting
  RUNNING,   // Pump on, filling tank
  FULL,      // Tank reached full — pump stopped
  FAULT,     // Dry-run or other fault
  STOPPING   // Pump stopping sequence
};

// ─── WaterToTankController ───────────────────────────────────────────────────────────
class WaterToTankController {
  const char    *_id;                    // "FG1" or "FG2"
  WSPController *_pump    = nullptr;     // Well pump controller
  TankManager   *_tank    = nullptr;     // Storage tank

  WTTMode  _mode          = WTTMode::MANUAL;
  WTTState _state         = WTTState::IDLE;

  unsigned long _startedAt     = 0;
  unsigned long _maxRunMs      = 0;      // 0 = unlimited
  unsigned long _dryRunMs      = 60000;  // 60s default dry-run timeout
  unsigned long _autoCheckMs   = 5000;

  using AlertCb = std::function<void(const String&, const String&)>;
  AlertCb _alert;

  void sendAlert(const String &msg, const String &sev = SEV_INFO) {
    Serial.printf("[%s] %s\n", _id, msg.c_str());
    if (_alert) _alert(msg, sev);
  }

  void doStart(const String &reason);
  void doStop (const String &reason);
  void autoProcess();

public:
  WaterToTankController(const char *id = "FG1") : _id(id) {}

  // ── Setup ────────────────────────────────────────────────────────────────
  // Call once after creating pump and tank objects
  void init(WSPController *pump, TankManager *tank);

  const char* groupId()    const { return _id; }
  WTTMode    getMode()    const { return _mode; }
  WTTState   getState()   const { return _state; }
  bool        isRunning()  const { return _state == WTTState::RUNNING; }
  WSPController* pump()    const { return _pump; }
  TankManager*   tank()    const { return _tank; }

  // ── Config ────────────────────────────────────────────────────────────────
  void setMaxRunTime   (unsigned long ms) { _maxRunMs   = ms; }
  void setDryRunTimeout(unsigned long ms) { _dryRunMs   = ms; }
  void setAlertCallback(AlertCb cb)       { _alert      = cb; }

  // ── Control ───────────────────────────────────────────────────────────────
  bool start(const String &reason = "manual");
  void stop (const String &reason = "manual");
  void setMode(WTTMode m);

  // ── Background — call every loop() ────────────────────────────────────────
  void process();

  // ── Status ────────────────────────────────────────────────────────────────
  String statusString() const;
};

#endif // WATER_TO_TANK_CONTROLLER_H
