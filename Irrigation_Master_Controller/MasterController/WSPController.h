// WSPController.h  —  Water Source Pump Controller
//
// Manages the well/borewell pump that fills the storage tank.
// Operates INDEPENDENTLY — no connection to node controllers or valves.
//
// ── Modes ──────────────────────────────────────────────────────────────────
//   MANUAL    ON/OFF via command (start() / stop())
//   SCHEDULE  Driven by ScheduleManager (calls start/stop at scheduled times)
//   AUTO      Sensor-driven:
//               • Tank empty  (sensor LOW)  → start pump
//               • Tank full / overflow      → stop pump
//               • Dry-run protection: stops pump if empty sensor still
//                 reads empty after DRY_RUN_TIMEOUT_MS (pump not lifting water)
//
// ── Tank sensors (optional) ───────────────────────────────────────────────
//   Register via setSensorCallback() — called every process() to read sensor.
//   Using callbacks keeps this class free of direct digitalRead() calls,
//   making it testable and board-agnostic.
//
//   If no sensor registered: AUTO mode falls back to MANUAL behaviour.

#ifndef WSP_CONTROLLER_H
#define WSP_CONTROLLER_H

#include "PumpController.h"
#include "Config.h"

class WSPController : public IPumpController {
  // Sensor callbacks — return true when condition is detected
  std::function<bool()> _sensorTankEmpty;    // true = tank needs filling
  std::function<bool()> _sensorTankFull;     // true = tank full / overflow

  // Timing
  unsigned long _startedAt      = 0;
  unsigned long _maxRunMs       = 0;          // 0 = unlimited
  unsigned long _dryRunTimeoutMs = 30000;     // 30 s default
  unsigned long _autoCheckMs    = 5000;       // sensor poll interval

  unsigned long _lastAutoCheck  = 0;

  // Internal
  void autoProcess();

public:
  // pumpId: "W1", "W2", or "W3"
  WSPController(const char *pumpId = "W1",
                uint8_t pin = WSP_PIN, bool activeHigh = WSP_ACTIVE_HIGH)
    : IPumpController(pumpId, pin, activeHigh) {}
  const char* pumpId() const { return _name; }

  // ── Sensor registration ────────────────────────────────────────────────
  void setTankEmptyCallback   (std::function<bool()> cb) { _sensorTankEmpty = cb; }
  void setTankFullCallback    (std::function<bool()> cb) { _sensorTankFull  = cb; }

  // ── Runtime config ─────────────────────────────────────────────────────
  void setMaxRunTime     (unsigned long ms) { _maxRunMs       = ms; }
  void setDryRunTimeout  (unsigned long ms) { _dryRunTimeoutMs= ms; }
  void setAutoCheckInterval(unsigned long ms){ _autoCheckMs   = ms; }

  // ── Control ────────────────────────────────────────────────────────────
  bool start(const String &reason = "") override;
  void stop (const String &reason = "") override;

  // ── Background ─────────────────────────────────────────────────────────
  void process() override;

  String statusString() const override;
};

#endif // WSP_CONTROLLER_H
