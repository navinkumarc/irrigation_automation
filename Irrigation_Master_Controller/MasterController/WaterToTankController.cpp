// WaterToTankController.cpp  —  Water filling process group
#include "WaterToTankController.h"
#include "WSPController.h"

// ─── init() ──────────────────────────────────────────────────────────────────
void WaterToTankController::init(WSPController *pump, TankManager *tank) {
  _pump = pump;
  _tank = tank;

  if (!pump) { Serial.printf("[%s] ❌ No pump assigned\n", _id); return; }

  // Wire tank callbacks into this group (not into WSPController directly)
  if (tank) {
    tank->setOnEmptyCallback([this]() {
      if (_mode == WTTMode::AUTO && _state == WTTState::IDLE)
        doStart("auto-tank-empty");
    });
    tank->setOnFullCallback([this]() {
      if (_state == WTTState::RUNNING)
        doStop("auto-tank-full");
    });
  }

  Serial.printf("[%s] Init — pump:%s tank:%s\n",
    _id,
    pump ? pump->pumpId() : "none",
    tank ? tank->tankId() : "none");
}

// ─── start() ─────────────────────────────────────────────────────────────────
bool WaterToTankController::start(const String &reason) {
  if (!_pump) {
    sendAlert("Cannot start — no pump configured", SEV_WARNING);
    return false;
  }
  if (_state == WTTState::RUNNING) return true;
  if (_state == WTTState::FAULT) {
    sendAlert("Cannot start — group in FAULT state. Send FG " + String(_id) + " CLEAR", SEV_WARNING);
    return false;
  }
  // In AUTO mode: don't start if tank already full
  if (_mode == WTTMode::AUTO && _tank && _tank->isFull()) {
    sendAlert("Tank already full — start skipped", SEV_INFO);
    return false;
  }
  doStart(reason);
  return _state == WTTState::RUNNING;
}

// ─── doStart() ───────────────────────────────────────────────────────────────
void WaterToTankController::doStart(const String &reason) {
  if (!_pump->start(reason)) {
    _state = WTTState::FAULT;
    sendAlert("Pump start failed", SEV_ERROR);
    return;
  }
  _state     = WTTState::RUNNING;
  _startedAt = millis();
  if (_tank) _tank->setFilling();
  sendAlert("[INFO] Fill group ON — pump:" + String(_pump->pumpId())
            + " tank:" + (_tank ? _tank->tankId() : "none")
            + " reason:" + reason);
}

// ─── stop() ──────────────────────────────────────────────────────────────────
void WaterToTankController::stop(const String &reason) {
  if (_state == WTTState::IDLE) return;
  doStop(reason);
}

// ─── doStop() ────────────────────────────────────────────────────────────────
void WaterToTankController::doStop(const String &reason) {
  if (_pump) _pump->stop(reason);
  _state = (_tank && _tank->isFull()) ? WTTState::FULL : WTTState::IDLE;
  sendAlert("[INFO] Fill group OFF — reason:" + reason
            + " tank:" + (_tank ? _tank->statusString() : "none"));
}

// ─── setMode() ───────────────────────────────────────────────────────────────
void WaterToTankController::setMode(WTTMode m) {
  _mode = m;
  const char *mname = (m == WTTMode::AUTO)     ? "AUTO"     :
                      (m == WTTMode::SCHEDULE)  ? "SCHEDULE" : "MANUAL";
  sendAlert(String("[INFO] ") + _id + " mode → " + mname);

  // Entering AUTO: if tank is empty right now, start immediately
  if (m == WTTMode::AUTO && _tank && _tank->isEmpty() && _state == WTTState::IDLE)
    doStart("auto-enter");
}

// ─── process() ───────────────────────────────────────────────────────────────
void WaterToTankController::process() {
  // Drive tank sensor polling
  if (_tank) _tank->process();

  if (_state != WTTState::RUNNING) {
    // In FULL state: re-check if tank dropped (sensor transitioned to FILLING/EMPTY)
    if (_state == WTTState::FULL && _mode == WTTMode::AUTO
        && _tank && _tank->isEmpty())
      doStart("auto-refill");
    return;
  }

  // ── Pump is running ───────────────────────────────────────────────────────

  unsigned long elapsed = millis() - _startedAt;

  // Max-run safety
  if (_maxRunMs > 0 && elapsed >= _maxRunMs) {
    sendAlert("Max run time reached — stopping", SEV_WARNING);
    doStop("max-run-timeout");
    return;
  }

  // Dry-run protection: if tank still EMPTY after dryRunMs, pump isn't lifting water
  if (_dryRunMs > 0 && elapsed >= _dryRunMs
      && _tank && _tank->isEmpty()) {
    _state = WTTState::FAULT;
    if (_pump) _pump->stop("dry-run");
    sendAlert("DRY RUN detected — no water rise after "
              + String(_dryRunMs / 1000) + "s. Check source!", SEV_ERROR);
    return;
  }

  // AUTO: if full sensor fired via TankManager callback, state already changed
  // MANUAL/SCHEDULE: run until explicit stop()
}

// ─── statusString() ──────────────────────────────────────────────────────────
String WaterToTankController::statusString() const {
  const char *mode =
    (_mode == WTTMode::AUTO)     ? "AUTO"     :
    (_mode == WTTMode::SCHEDULE) ? "SCHED"    : "MAN";
  const char *state =
    (_state == WTTState::RUNNING)  ? "RUNNING" :
    (_state == WTTState::FULL)     ? "FULL"    :
    (_state == WTTState::FAULT)    ? "FAULT"   :
    (_state == WTTState::STOPPING) ? "STOPPING": "IDLE";

  String s = String(_id) + ":" + state + "(" + mode + ")";
  if (_tank)  s += " " + _tank->statusString();
  if (_pump && _state == WTTState::RUNNING) {
    unsigned long sec = (millis() - _startedAt) / 1000;
    s += " " + String(sec) + "s";
  }
  return s;
}
