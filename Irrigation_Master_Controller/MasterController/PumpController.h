// PumpController.h  —  Abstract base for pump controllers
//
// Two concrete implementations:
//   WSPController  — Water Source Pump (well/borewell → storage tank)
//   IPController   — Irrigation Pump  (tank → pipes/valves → fields)
//
// Each pump is self-contained: holds its own pin, mode, state, and
// drives its own GPIO. Schedules are owned by ScheduleManager, which
// calls the pump's start()/stop() methods.

#ifndef PUMP_CONTROLLER_H
#define PUMP_CONTROLLER_H

#include <Arduino.h>
#include <functional>
#include "MessageFormats.h"

// ── Pump mode ─────────────────────────────────────────────────────────────────
enum class PumpMode {
  MANUAL,    // ON/OFF directly via command
  SCHEDULE,  // Driven by ScheduleManager
  AUTO       // Sensor-driven (WSPController only)
};

// ── Pump state ────────────────────────────────────────────────────────────────
enum class PumpState {
  OFF,
  RUNNING,
  FAULT      // Failed to start / safety interlock triggered
};

// ── Alert callback — pump reports events to CommManager / UserCommunication ───
using PumpAlertCallback = std::function<void(const String &msg, const String &severity)>;

// ── IPumpController — abstract base ──────────────────────────────────────────
class IPumpController {
protected:
  const char   *_name;
  uint8_t       _pin;
  bool          _activeHigh;
  PumpMode      _mode    = PumpMode::MANUAL;
  PumpState     _state   = PumpState::OFF;
  PumpAlertCallback _alert;

  void drivePin(bool on) {
    digitalWrite(_pin, (_activeHigh ? on : !on) ? HIGH : LOW);
  }

  void sendAlert(const String &msg, const String &sev = SEV_INFO) {
    Serial.printf("[%s] %s\n", _name, msg.c_str());
    if (_alert) _alert(msg, sev);
  }

public:
  IPumpController(const char *name, uint8_t pin, bool activeHigh)
    : _name(name), _pin(pin), _activeHigh(activeHigh) {}

  virtual ~IPumpController() {}

  // ── Setup ─────────────────────────────────────────────────────────────────
  virtual void begin() {
    pinMode(_pin, OUTPUT);
    drivePin(false);
    Serial.printf("[%s] Initialized (pin %d, active %s)\n",
                  _name, _pin, _activeHigh ? "HIGH" : "LOW");
  }

  void setAlertCallback(PumpAlertCallback cb) { _alert = cb; }

  // ── Mode ──────────────────────────────────────────────────────────────────
  void        setMode(PumpMode m)  { _mode = m; }
  PumpMode    getMode()     const  { return _mode; }
  PumpState   getState()    const  { return _state; }
  bool        isRunning()   const  { return _state == PumpState::RUNNING; }
  const char* name()        const  { return _name; }

  // ── Control — override in subclass to add guards ──────────────────────────
  virtual bool start(const String &reason = "") = 0;
  virtual void stop (const String &reason = "") = 0;

  // ── Background — call every loop() ───────────────────────────────────────
  virtual void process() {}

  // ── Status string ─────────────────────────────────────────────────────────
  virtual String statusString() const {
    const char *m = (_mode == PumpMode::MANUAL)   ? "MANUAL"   :
                    (_mode == PumpMode::SCHEDULE)  ? "SCHEDULE" : "AUTO";
    const char *s = (_state == PumpState::RUNNING) ? "RUNNING" :
                    (_state == PumpState::FAULT)   ? "FAULT"   : "OFF";
    return String(_name) + ":" + s + "(" + m + ")";
  }
};

#endif // PUMP_CONTROLLER_H
