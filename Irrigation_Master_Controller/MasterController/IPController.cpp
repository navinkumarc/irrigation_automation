// IPController.cpp  —  Irrigation Pump Controller
#include "IPController.h"

// ─── setOpenValveCount() ──────────────────────────────────────────────────────
void IPController::setOpenValveCount(int count) {
  int prev = _openValveCount;
  _openValveCount = max(0, count);
  if (prev != _openValveCount)
    Serial.printf("[IPC] Open valves: %d\n", _openValveCount);
}

// ─── start() ─────────────────────────────────────────────────────────────────
bool IPController::start(const String &reason) {
  if (_state == PumpState::RUNNING) return true;

  if (_state == PumpState::FAULT) {
    sendAlert("Cannot start — pump in FAULT state", SEV_WARNING);
    return false;
  }

  // ── Valve guard ───────────────────────────────────────────────────────────
  if (_openValveCount < _minOpenValves) {
    sendAlert("Irrigation pump blocked — need " + String(_minOpenValves)
              + " open valve(s), have " + String(_openValveCount), SEV_WARNING);
    return false;
  }

  drivePin(true);
  _state     = PumpState::RUNNING;
  _startedAt = millis();

  String msg = "Irrigation pump ON";
  if (reason.length()) msg += " (" + reason + ")";
  msg += " [valves open: " + String(_openValveCount) + "]";
  sendAlert(msg, SEV_INFO);
  return true;
}

// ─── stop() ──────────────────────────────────────────────────────────────────
void IPController::stop(const String &reason) {
  if (_state == PumpState::OFF) return;
  drivePin(false);
  _state = PumpState::OFF;

  String msg = "Irrigation pump OFF";
  if (reason.length()) msg += " (" + reason + ")";
  sendAlert(msg, SEV_INFO);
}

// ─── process() ───────────────────────────────────────────────────────────────
void IPController::process() {
  if (_state != PumpState::RUNNING) return;

  // Max-run safety
  if (_maxRunMs > 0 && millis() - _startedAt >= _maxRunMs) {
    sendAlert("Max run time reached — stopping irrigation pump", SEV_WARNING);
    stop("max-run-timeout");
    return;
  }

  // Valve guard while running
  if (millis() - _lastValveCheck >= _valveCheckMs) {
    _lastValveCheck = millis();
    if (_openValveCount < _minOpenValves) {
      sendAlert("Irrigation pump stopped — valve count dropped to "
                + String(_openValveCount) + " (min: " + String(_minOpenValves) + ")",
                SEV_WARNING);
      stop("valve-count-too-low");
    }
  }
}

// ─── statusString() ──────────────────────────────────────────────────────────
String IPController::statusString() const {
  String base = IPumpController::statusString();
  base += " valves:" + String(_openValveCount) + "/" + String(_minOpenValves);
  if (_state == PumpState::RUNNING) {
    unsigned long sec = (millis() - _startedAt) / 1000;
    base += " " + String(sec) + "s";
  }
  return base;
}
