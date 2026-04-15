// WSPController.cpp  —  Water Source Pump hardware controller
// Pure GPIO — no sensor or AUTO logic (owned by WaterToTankController)
#include "WSPController.h"

bool WSPController::start(const String &reason) {
  if (_state == PumpState::RUNNING) return true;
  if (_state == PumpState::FAULT) {
    sendAlert("Cannot start — FAULT state", SEV_WARNING);
    return false;
  }
  drivePin(true);
  _state     = PumpState::RUNNING;
  _startedAt = millis();
  String msg = String(pumpId()) + " ON";
  if (reason.length()) msg += " (" + reason + ")";
  sendAlert(msg, SEV_INFO);
  return true;
}

void WSPController::stop(const String &reason) {
  if (_state == PumpState::OFF) return;
  drivePin(false);
  _state = PumpState::OFF;
  String msg = String(pumpId()) + " OFF";
  if (reason.length()) msg += " (" + reason + ")";
  sendAlert(msg, SEV_INFO);
}

void WSPController::process() {
  if (_state != PumpState::RUNNING) return;
  if (_maxRunMs > 0 && millis() - _startedAt >= _maxRunMs) {
    sendAlert("Max run time — stopping " + String(pumpId()), SEV_WARNING);
    stop("max-run-timeout");
  }
}

String WSPController::statusString() const {
  const char *s = (_state == PumpState::RUNNING) ? "ON"    :
                  (_state == PumpState::FAULT)   ? "FAULT" : "OFF";
  String out = String(pumpId()) + ":" + s;
  if (_state == PumpState::RUNNING) {
    unsigned long sec = (millis() - _startedAt) / 1000;
    out += " " + String(sec) + "s";
  }
  return out;
}
