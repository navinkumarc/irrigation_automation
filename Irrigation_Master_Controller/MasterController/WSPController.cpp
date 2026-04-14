// WSPController.cpp  —  Water Source Pump Controller
#include "WSPController.h"

// ─── start() ─────────────────────────────────────────────────────────────────
bool WSPController::start(const String &reason) {
  if (_state == PumpState::RUNNING) {
    Serial.printf("[WSPC] Already running\n");
    return true;
  }
  if (_state == PumpState::FAULT) {
    sendAlert("Cannot start — pump in FAULT state. Clear fault first.", SEV_WARNING);
    return false;
  }

  // In AUTO mode: check overflow before starting
  if (_mode == PumpMode::AUTO && _sensorTankFull && _sensorTankFull()) {
    sendAlert("AUTO start blocked — tank full / overflow sensor active", SEV_WARNING);
    return false;
  }

  drivePin(true);
  _state     = PumpState::RUNNING;
  _startedAt = millis();

  String msg = "Well pump ON";
  if (reason.length()) msg += " (" + reason + ")";
  sendAlert(msg, SEV_INFO);
  return true;
}

// ─── stop() ──────────────────────────────────────────────────────────────────
void WSPController::stop(const String &reason) {
  if (_state == PumpState::OFF) return;
  drivePin(false);
  _state = PumpState::OFF;

  String msg = "Well pump OFF";
  if (reason.length()) msg += " (" + reason + ")";
  sendAlert(msg, SEV_INFO);
}

// ─── process() ───────────────────────────────────────────────────────────────
void WSPController::process() {
  if (_mode == PumpMode::AUTO) autoProcess();

  // Max-run safety cutoff (applies in any mode)
  if (_state == PumpState::RUNNING && _maxRunMs > 0 &&
      millis() - _startedAt >= _maxRunMs) {
    sendAlert("Max run time reached — stopping well pump", SEV_WARNING);
    stop("max-run-timeout");
  }
}

// ─── autoProcess() ────────────────────────────────────────────────────────────
void WSPController::autoProcess() {
  if (millis() - _lastAutoCheck < _autoCheckMs) return;
  _lastAutoCheck = millis();

  bool tankFull  = _sensorTankFull  && _sensorTankFull();
  bool tankEmpty = _sensorTankEmpty && _sensorTankEmpty();

  if (_state == PumpState::RUNNING) {
    // Stop conditions
    if (tankFull) {
      sendAlert("Tank full — stopping well pump", SEV_INFO);
      stop("tank-full");
      return;
    }
    // Dry-run check: if tank is still empty after timeout, pump isn't lifting water
    if (tankEmpty && _dryRunTimeoutMs > 0 &&
        millis() - _startedAt >= _dryRunTimeoutMs) {
      _state = PumpState::FAULT;
      drivePin(false);
      sendAlert("DRY RUN detected — well pump stopped. Check water source!", SEV_ERROR);
    }
  } else if (_state == PumpState::OFF) {
    // Start condition: tank needs filling
    if (tankEmpty && !tankFull) {
      sendAlert("Tank empty — starting well pump", SEV_INFO);
      start("auto-tank-empty");
    }
  }
  // FAULT: no automatic restart — operator must call start() explicitly
}

// ─── statusString() ──────────────────────────────────────────────────────────
String WSPController::statusString() const {
  String base = IPumpController::statusString();
  if (_state == PumpState::RUNNING) {
    unsigned long sec = (millis() - _startedAt) / 1000;
    base += " " + String(sec) + "s";
  }
  return base;
}
