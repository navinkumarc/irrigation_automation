// PowerMonitor.h  —  Power source and battery monitoring
//
// Hardware: Heltec WiFi LoRa 32 V3 (ESP32-S3)
//
// ── What the hardware provides ─────────────────────────────────────────────
//   GPIO1  (ADC1_CH0) — VBAT voltage via 100kΩ + 390kΩ divider to GND
//                       VBAT_actual = ADC_V * (100+390)/100 = ADC_V * 4.9
//   GPIO37 (ADC_Ctrl) — LOW enables VBAT read circuit, HIGH disables
//                       Enable only during read, disable after (save power)
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

// ── Hardware pins — confirmed from official HTIT-WB32_V3 datasheet ─────────
// J2 pin 18: GPIO1,  ADC1_CH0, Read VBAT Voltage
// J3 pin 11: GPIO37, ADC_Ctrl (Pull Up/Down shown in pin map)
#define PM_VBAT_PIN        1    // GPIO1  — ADC1_CH0 — VBAT read
#define PM_ADC_CTRL_PIN    37   // GPIO37 — ADC_Ctrl (pull-up default)

// ── VBAT formula (datasheet footnote 3) ──────────────────────────────────
// VBAT = 100/(100+390) × VADC_IN1
// ∴ VBAT = VADC × 4.9   (R_top=390kΩ, R_bot=100kΩ)
// VADC range: 0.653V (3.2V bat) to 0.857V (4.2V bat) — within 0dB range
#define PM_DIVIDER_RATIO   4.9f

// ── GPIO37 polarity — VERIFIED ON HARDWARE via POWER RAW ─────────────────
//   GPIO37=HIGH  raw=848  <-- only state that reads the divider
//   GPIO37=LOW   raw=0
//   GPIO37=FLOAT raw=0
// HIGH enables the VBAT read circuit on this board.
#define PM_ADC_CTRL_ACTIVE HIGH

// ── ADC attenuation — set explicitly, do not rely on the core default ────
// The Arduino ESP32 core defaults analogRead() to 11dB. We set it per-pin
// so the scale factor below is always correct regardless of core version.
// 11dB on ESP32-S3 → full scale ≈ 3.9V.
// Sanity check with raw=848: 848/4095 x 3.9 x 4.9 = 3.96V (battery on USB).
#define PM_ADC_FULLSCALE   3.9f

// ── Battery thresholds (LiPo 3.7V nominal) ─────────────────────────────
#define PM_VOLT_FULL       4.20f  // 100% — also USB_FULL threshold
#define PM_VOLT_CHARGING   4.25f  // above this = USB charging (above cell max)
#define PM_VOLT_GOOD       3.80f  // comfortable operating range
#define PM_VOLT_LOW        3.50f  // send low-battery warning
#define PM_VOLT_CRITICAL   3.30f  // send critical alert
#define PM_VOLT_EMPTY      3.20f  // 0%

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
  unsigned long _pollMs            = 30000;
  unsigned long _lastPollMs        = 0;
  bool          _firstRead         = true;
  float         _calScale          = 0.0f;  // 0 = use default scale
  bool          _lastMains         = false; // previous mains state
  bool          _mainsKnownOnce    = false; // seen at least one valid reading

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
  // Calibrate with measured real voltage (multimeter reading)
  void calibrate(float realVoltage);
  // Test both GPIO37 polarities, report raw ADC for each
  String diagnose();

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

  // ── Mains power inference ─────────────────────────────────────────────
  // The USB adapter is powered from AC mains. If the battery is charging
  // or held full on USB, the adapter has power → mains is ON.
  // If the battery is discharging, the adapter is dead → mains is OFF.
  //
  // Limitation: with no battery fitted, VBAT floats and the state is
  // unreliable — mainsKnown() returns false in that case.
  bool isMainsOn()   const { return isOnUSB(); }
  bool mainsKnown()  const { return _source != PowerSource::UNKNOWN
                                    && _voltage > 2.0f; }
  const char* mainsString() const {
    if (!mainsKnown()) return "UNKNOWN";
    return isMainsOn() ? "ON" : "OFF";
  }

  // ── Status string for commands ────────────────────────────────────────────
  String statusString()  const;
  String healthString()  const;
};

#endif // POWER_MONITOR_H
