// PowerMonitor.h  —  Power source and battery monitoring
//
// Hardware: Heltec WiFi LoRa 32 V3 (ESP32-S3)
//
// ── What the hardware provides ─────────────────────────────────────────────
//   GPIO1  (ADC1_CH0) — VBAT voltage via 100kΩ + 390kΩ divider to GND
//                       VBAT_actual = ADC_V * (100+390)/100 = ADC_V * 4.9
//   GPIO37 (ADC_Ctrl) — Must be HIGH before reading ADC; enables the
//                       internal ADC power circuit (active HIGH)
//   USB VBUS          — Not on any ESP32 GPIO. Inferred from voltage:
//                         voltage > 4.25V or sustained rise → USB present
//   CHRG pin (TP4054) — Connected to orange LED only, NOT to any GPIO
//
// ── What PowerMonitor derives ──────────────────────────────────────────────
//   • Raw voltage       — averaged over multiple ADC samples
//   • Battery %         — from LiPo discharge curve
//   • Power source      — USB (charging/full) or BATTERY
//   • Charge state      — CHARGING / FULL / DISCHARGING / CRITICAL / UNKNOWN
//   • Health estimate   — tracks min voltage ever seen + time below 3.5V
//
// ── Battery voltage → % curve (LiPo 3.7V nominal) ────────────────────────
//   4.20V = 100%    4.10V = 90%    4.00V = 80%    3.90V = 70%
//   3.80V = 60%     3.70V = 50%    3.60V = 40%    3.50V = 25%
//   3.40V = 15%     3.30V = 5%     3.20V = 0%
//
// ── Commands (all channels) ────────────────────────────────────────────────
//   POWER STATUS   — voltage, %, source, charge state, health
//   BAT STATUS     — same (alias)

#ifndef POWER_MONITOR_H
#define POWER_MONITOR_H

#include <Arduino.h>
#include <functional>
#include "MessageFormats.h"

// ── Hardware pins (Heltec V3 — do not change) ─────────────────────────────
#define PM_VBAT_PIN        1    // GPIO1  — ADC1_CH0 — battery voltage divider
#define PM_ADC_CTRL_PIN    37   // GPIO37 — HIGH = enable ADC circuit

// ── ADC voltage divider ────────────────────────────────────────────────────
// R_top = 390kΩ, R_bot = 100kΩ, Vref = 3.3V, ADC 12-bit (0-4095)
// VBAT = ADC_raw / 4095.0 * 3.3 * (390+100)/100
#define PM_ADC_REF_V       3.3f
#define PM_ADC_RESOLUTION  4095.0f
#define PM_DIVIDER_RATIO   4.9f   // (390+100)/100

// ── Battery thresholds (LiPo 3.7V) ────────────────────────────────────────
#define PM_VOLT_FULL       4.20f
#define PM_VOLT_CHARGING   4.25f  // above this → USB + charging
#define PM_VOLT_GOOD       3.80f
#define PM_VOLT_LOW        3.50f
#define PM_VOLT_CRITICAL   3.30f
#define PM_VOLT_EMPTY      3.20f

// ── Power source ───────────────────────────────────────────────────────────
enum class PowerSource {
  UNKNOWN,
  USB_CHARGING,   // USB connected, battery charging (voltage > 4.25V or rising)
  USB_FULL,       // USB connected, battery full (voltage >= 4.20V, stable)
  BATTERY         // Running on battery (voltage <= 4.20V or falling)
};

// ── Charge state ───────────────────────────────────────────────────────────
enum class ChargeState {
  UNKNOWN,
  CHARGING,       // Voltage actively rising (USB connected)
  FULL,           // Voltage at or above VOLT_FULL (USB connected)
  DISCHARGING,    // Voltage falling or stable below VOLT_FULL
  BATT_LOW,            // Below PM_VOLT_LOW — alert sent
  BATT_CRITICAL        // Below PM_VOLT_CRITICAL — action required
};

// ── PowerMonitor ───────────────────────────────────────────────────────────
class PowerMonitor {
  // ── ADC sampling ────────────────────────────────────────────────────────
  static const int  SAMPLE_COUNT   = 16;    // average over 16 readings
  static const int  HISTORY_SIZE   = 10;    // voltage history for trend
  float   _history[HISTORY_SIZE]   = {};
  int     _histIdx                 = 0;
  bool    _histFull                = false;

  // ── Measurements ────────────────────────────────────────────────────────
  float         _voltage           = 0.0f;
  int           _percent           = 0;
  PowerSource   _source            = PowerSource::UNKNOWN;
  ChargeState   _chargeState       = ChargeState::UNKNOWN;

  // ── Health tracking ─────────────────────────────────────────────────────
  float         _minVoltSeen       = 5.0f;  // lowest voltage observed
  float         _maxVoltSeen       = 0.0f;  // highest voltage observed
  unsigned long _timeBelowLowMs    = 0;     // cumulative ms below VOLT_LOW
  unsigned long _lowStartMs        = 0;
  bool          _wasLow            = false;
  bool          _lowAlertSent      = false;
  bool          _criticalAlertSent = false;

  // ── Poll interval ────────────────────────────────────────────────────────
  unsigned long _pollMs            = 30000; // 30s default
  unsigned long _lastPollMs        = 0;
  bool          _firstRead         = true;

  using AlertCb = std::function<void(const String&, const String&)>;
  AlertCb _alert;

  // ── Private helpers ──────────────────────────────────────────────────────
  float   readVoltage();
  int     voltToPercent(float v) const;
  float   trendV() const;           // positive = rising, negative = falling
  void    updateState();
  void    updateHealth(float v);
  void    sendAlert(const String &msg, const String &sev = SEV_INFO);

public:
  PowerMonitor() = default;

  // ── Setup ────────────────────────────────────────────────────────────────
  void begin();
  void setAlertCallback(AlertCb cb) { _alert = cb; }
  void setPollInterval(unsigned long ms) { _pollMs = ms; }

  // ── Background — call every loop() ───────────────────────────────────────
  void process();

  // ── Accessors ────────────────────────────────────────────────────────────
  float       voltage()     const { return _voltage; }
  int         percent()     const { return _percent; }
  PowerSource source()      const { return _source; }
  ChargeState chargeState() const { return _chargeState; }
  float       minVoltage()  const { return _minVoltSeen; }
  bool        isOnUSB()     const {
    return _source == PowerSource::USB_CHARGING ||
           _source == PowerSource::USB_FULL;
  }
  bool        isLow()       const { return _percent <= 15; }
  bool        isCritical()  const { return _percent <= 5;  }

  // ── Status string for commands ────────────────────────────────────────────
  String statusString()  const;
  String healthString()  const;
};

#endif // POWER_MONITOR_H
