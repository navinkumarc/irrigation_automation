// WaterFillGroup.cpp  —  Water filling process group
#include "WaterFillGroup.h"
#include "WSPController.h"

// ─── init() ──────────────────────────────────────────────────────────────────
void WaterFillGroup::init(WSPController *pump, TankManager *tank) {
  _pump = pump;
  _tank = tank;

  if (!pump) { Serial.printf("[%s] ❌ No pump assigned\n", _id); return; }

  // Wire tank callbacks into this group (not into WSPController directly)
  if (tank) {
    tank->setOnEmptyCallback([this]() {
      if (_mode == FillMode::AUTO && _state == FillState::IDLE)
        doStart("auto-tank-empty");
    });
    tank->setOnFullCallback([this]() {
      if (_state == FillState::RUNNING)
        doStop("auto-tank-full");
    });
  }

  Serial.printf("[%s] Init — pump:%s tank:%s\n",
    _id,
    pump ? pump->pumpId() : "none",
    tank ? tank->tankId() : "none");
}

// ─── start() ─────────────────────────────────────────────────────────────────
bool WaterFillGroup::start(const String &reason) {
  if (!_pump) {
    sendAlert("Cannot start — no pump configured", SEV_WARNING);
    return false;
  }
  if (_state == FillState::RUNNING) return true;
  if (_state == FillState::FAULT) {
    sendAlert("Cannot start — group in FAULT state. Send FG " + String(_id) + " CLEAR", SEV_WARNING);
    return false;
  }
  // In AUTO mode: don't start if tank already full
  if (_mode == FillMode::AUTO && _tank && _tank->isFull()) {
    sendAlert("Tank already full — start skipped", SEV_INFO);
    return false;
  }
  doStart(reason);
  return _state == FillState::RUNNING;
}

// ─── doStart() ───────────────────────────────────────────────────────────────
void WaterFillGroup::doStart(const String &reason) {
  if (!_pump->start(reason)) {
    _state = FillState::FAULT;
    sendAlert("Pump start failed", SEV_ERROR);
    return;
  }
  _state     = FillState::RUNNING;
  _startedAt = millis();
  if (_tank) _tank->setFilling();
  sendAlert("[INFO] Fill group ON — pump:" + String(_pump->pumpId())
            + " tank:" + (_tank ? _tank->tankId() : "none")
            + " reason:" + reason);
}

// ─── stop() ──────────────────────────────────────────────────────────────────
void WaterFillGroup::stop(const String &reason) {
  if (_state == FillState::IDLE) return;
  doStop(reason);
}

// ─── doStop() ────────────────────────────────────────────────────────────────
void WaterFillGroup::doStop(const String &reason) {
  if (_pump) _pump->stop(reason);
  _state = (_tank && _tank->isFull()) ? FillState::FULL : FillState::IDLE;
  sendAlert("[INFO] Fill group OFF — reason:" + reason
            + " tank:" + (_tank ? _tank->statusString() : "none"));
}

// ─── setMode() ───────────────────────────────────────────────────────────────
void WaterFillGroup::setMode(FillMode m) {
  _mode = m;
  const char *mname = (m == FillMode::AUTO)     ? "AUTO"     :
                      (m == FillMode::SCHEDULE)  ? "SCHEDULE" : "MANUAL";
  sendAlert(String("[INFO] ") + _id + " mode → " + mname);

  // Entering AUTO: if tank is empty right now, start immediately
  if (m == FillMode::AUTO && _tank && _tank->isEmpty() && _state == FillState::IDLE)
    doStart("auto-enter");
}

// ─── process() ───────────────────────────────────────────────────────────────
void WaterFillGroup::process() {
  // Drive tank sensor polling
  if (_tank) _tank->process();

  if (_state != FillState::RUNNING) {
    // In FULL state: re-check if tank dropped (sensor transitioned to FILLING/EMPTY)
    if (_state == FillState::FULL && _mode == FillMode::AUTO
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
    _state = FillState::FAULT;
    if (_pump) _pump->stop("dry-run");
    sendAlert("DRY RUN detected — no water rise after "
              + String(_dryRunMs / 1000) + "s. Check source!", SEV_ERROR);
    return;
  }

  // AUTO: if full sensor fired via TankManager callback, state already changed
  // MANUAL/SCHEDULE: run until explicit stop()
}

// ─── statusString() ──────────────────────────────────────────────────────────
String WaterFillGroup::statusString() const {
  const char *mode =
    (_mode == FillMode::AUTO)     ? "AUTO"     :
    (_mode == FillMode::SCHEDULE) ? "SCHED"    : "MAN";
  const char *state =
    (_state == FillState::RUNNING)  ? "RUNNING" :
    (_state == FillState::FULL)     ? "FULL"    :
    (_state == FillState::FAULT)    ? "FAULT"   :
    (_state == FillState::STOPPING) ? "STOPPING": "IDLE";

  String s = String(_id) + ":" + state + "(" + mode + ")";
  if (_tank)  s += " " + _tank->statusString();
  if (_pump && _state == FillState::RUNNING) {
    unsigned long sec = (millis() - _startedAt) / 1000;
    s += " " + String(sec) + "s";
  }
  return s;
}
