// TankManager.h  —  Tank level monitoring
//
// Manages one physical storage tank with up to two float switch sensors:
//   EMPTY sensor — fires when water drops below the low probe
//   FULL  sensor — fires when water reaches the high probe
//
// TankManager is purely a sensor reader and state tracker.
// It has no pump knowledge — it fires callbacks that WaterToTankController uses
// to start or stop the pump.
//
// ── Tank states ────────────────────────────────────────────────────────────
//   UNKNOWN   sensors not yet read
//   EMPTY     empty sensor active (below low probe)
//   FILLING   between probes (pump running, level rising)
//   FULL      full sensor active (at or above high probe)
//
// ── Config ─────────────────────────────────────────────────────────────────
//   Max 2 tanks: T1, T2
//   Sensor pins in Config.h or set at runtime via setSensorCallbacks()
//   Set emptyPin / fullPin = 0 to disable that sensor

#ifndef TANK_MANAGER_H
#define TANK_MANAGER_H

#include <Arduino.h>
#include <functional>
#include "MessageFormats.h"

// ─── Tank state ───────────────────────────────────────────────────────────────
enum class TankState {
  UNKNOWN,   // Sensors not read yet
  EMPTY,     // Below low probe — needs filling
  FILLING,   // Between probes — pump running
  FULL       // At or above high probe — stop pump
};

// ─── TankManager ─────────────────────────────────────────────────────────────
class TankManager {
  const char   *_id;                  // "T1" or "T2"
  TankState     _state    = TankState::UNKNOWN;
  unsigned long _lastRead = 0;
  unsigned long _pollMs   = 3000;     // sensor poll interval

  // Sensor callbacks — return true when condition is met
  std::function<bool()> _sensorEmpty; // true = tank empty (low probe open)
  std::function<bool()> _sensorFull;  // true = tank full  (high probe open)

  // Event callbacks — fired by WaterToTankController
  std::function<void()> _onEmpty;     // tank just became empty
  std::function<void()> _onFull;      // tank just became full

  // Alert
  using AlertCb = std::function<void(const String&, const String&)>;
  AlertCb _alert;

  void sendAlert(const String &msg, const String &sev = SEV_INFO) {
    Serial.printf("[Tank %s] %s\n", _id, msg.c_str());
    if (_alert) _alert(msg, sev);
  }

public:
  explicit TankManager(const char *id = "T1") : _id(id) {}

  // ── Setup ────────────────────────────────────────────────────────────────
  const char* tankId() const { return _id; }

  void setSensorCallbacks(std::function<bool()> emptyFn,
                          std::function<bool()> fullFn) {
    _sensorEmpty = emptyFn;
    _sensorFull  = fullFn;
  }

  void setOnEmptyCallback(std::function<void()> cb) { _onEmpty = cb; }
  void setOnFullCallback (std::function<void()> cb) { _onFull  = cb; }
  void setAlertCallback  (AlertCb cb)                { _alert   = cb; }
  void setPollInterval   (unsigned long ms)           { _pollMs  = ms; }

  // ── State access ─────────────────────────────────────────────────────────
  TankState   getState()   const { return _state; }
  bool        isEmpty()    const { return _state == TankState::EMPTY; }
  bool        isFull()     const { return _state == TankState::FULL;  }
  bool        isFilling()  const { return _state == TankState::FILLING; }
  bool        hasEmptySensor() const { return (bool)_sensorEmpty; }
  bool        hasFullSensor()  const { return (bool)_sensorFull;  }

  // Force state (called by WaterToTankController when pump starts/stops)
  void setFilling() {
    if (_state != TankState::FILLING) {
      _state = TankState::FILLING;
      sendAlert("Tank filling started");
    }
  }

  // ── Background — call every loop() ───────────────────────────────────────
  void process() {
    if (millis() - _lastRead < _pollMs) return;
    _lastRead = millis();

    bool empty = _sensorEmpty && _sensorEmpty();
    bool full  = _sensorFull  && _sensorFull();

    TankState prev = _state;

    if (full) {
      _state = TankState::FULL;
    } else if (empty) {
      _state = TankState::EMPTY;
    } else {
      // Between probes
      if (_state != TankState::FILLING)
        _state = TankState::FILLING;
    }

    // Fire transition callbacks
    if (prev != _state) {
      sendAlert("Tank state: " + statusString());
      if (_state == TankState::EMPTY && _onEmpty) _onEmpty();
      if (_state == TankState::FULL  && _onFull)  _onFull();
    }
  }

  // ── Status ────────────────────────────────────────────────────────────────
  String statusString() const {
    switch (_state) {
      case TankState::EMPTY:   return String(_id) + ":EMPTY";
      case TankState::FILLING: return String(_id) + ":FILLING";
      case TankState::FULL:    return String(_id) + ":FULL";
      default:                 return String(_id) + ":UNKNOWN";
    }
  }
};

#endif // TANK_MANAGER_H
