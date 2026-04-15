// WSPController.h  —  Water Source Pump hardware controller
//
// Pure pump GPIO controller. Handles MANUAL and SCHEDULE modes only.
// AUTO mode and sensor logic are owned by WaterToTankController, which
// wraps this controller together with a TankManager.
//
// WSPController is responsible for:
//   • Driving the relay output pin (start/stop)
//   • Max-run safety cutoff
//   • Reporting RUNNING / OFF / FAULT state
//
// It does NOT know about tanks, sensors, or fill logic.

#ifndef WSP_CONTROLLER_H
#define WSP_CONTROLLER_H

#include "PumpController.h"
#include "Config.h"

class WSPController : public IPumpController {
  unsigned long _startedAt  = 0;
  unsigned long _maxRunMs   = 0;   // 0 = unlimited

public:
  WSPController(const char *pumpId = "W1",
                uint8_t pin = WSP_PIN, bool activeHigh = WSP_ACTIVE_HIGH)
    : IPumpController(pumpId, pin, activeHigh) {}

  const char* pumpId() const { return _name; }

  void setMaxRunTime(unsigned long ms) { _maxRunMs = ms; }

  bool start(const String &reason = "") override;
  void stop (const String &reason = "") override;
  void process() override;

  String statusString() const override;
};

#endif // WSP_CONTROLLER_H
