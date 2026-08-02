// PowerMonitor.cpp  —  Power source and battery monitoring
// Heltec WiFi LoRa 32 V3 (ESP32-S3)
//
// ── Hardware facts (from schematic) ──────────────────────────────────────────
//   GPIO1  = VBAT_READ via 100kΩ(top) + 390kΩ(bottom) divider
//            VBAT_actual = ADC_V × (100+390)/100 = ADC_V × 4.9
//
//   GPIO37 = ADC_Ctrl — drives gate of AO7801 MOSFET (active LOW)
//            LOW  = MOSFET ON  → divider connected → valid ADC reading
//            HIGH = MOSFET OFF → divider floating  → ADC reads garbage
//            *** Previous code had this BACKWARDS (HIGH to enable) ***
//
//   TP4054 charger:
//     - When USB present + battery charging: VBAT rises toward 4.2V
//     - When USB present + no battery:       VBAT line floats ~3.7-3.9V
//       (charger output idles near CV threshold) — looks like 50% battery!
//     - CHRG pin drives LED only — not accessible to MCU
//
// ── USB detection strategy ────────────────────────────────────────────────────
//   We CANNOT reliably detect USB without a battery from voltage alone.
//   Best we can do:
//     v > 4.20V  → definitely USB + charging (battery above full threshold)
//     v rising   → USB charging (regardless of absolute voltage)
//     v stable at 4.20V → USB + full
//     v falling or stable below 4.20V → assume battery
//   If no battery fitted, readings will be unreliable (~3.7V floating).
//   The display will show the ADC reading honestly with a note.

#include "PowerMonitor.h"
#include <esp_adc_cal.h>

static esp_adc_cal_characteristics_t _adcChars;

// ─── begin() ─────────────────────────────────────────────────────────────────
void PowerMonitor::begin() {
  // ADC_Ctrl (GPIO37): active LOW — pull LOW to connect the voltage divider
  // Previous code had HIGH which disconnected the divider (wrong!)
  pinMode(PM_ADC_CTRL_PIN, OUTPUT);
  digitalWrite(PM_ADC_CTRL_PIN, LOW);   // ← CORRECTED: LOW = enable ADC circuit
  delay(10);  // let divider settle

  // Configure ADC: 12-bit, 11dB attenuation (0-3.9V input range)
  analogSetAttenuation(ADC_11db);
  analogSetWidth(12);

  // Calibrate ADC using eFuse values baked into ESP32-S3
  esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11,
                            ADC_WIDTH_BIT_12, 1100, &_adcChars);

  // First reading
  delay(50);
  _voltage = readVoltage();
  if (_voltage > 0.5f) {
    _minVoltSeen = _voltage;
    _maxVoltSeen = _voltage;
    _history[0]  = _voltage;
    _histIdx     = 1;
    _percent     = voltToPercent(_voltage);
    updateState();
    _firstRead   = false;
  }

  Serial.printf("[PowerMon] Init: %.3fV %d%% | %s | %s\n",
    _voltage, _percent,
    isOnUSB() ? "USB" : "Battery",
    _chargeState == ChargeState::CHARGING   ? "CHARGING"    :
    _chargeState == ChargeState::FULL        ? "FULL"        :
    _chargeState == ChargeState::DISCHARGING ? "DISCHARGING" :
    _chargeState == ChargeState::BATT_LOW    ? "LOW"         :
    _chargeState == ChargeState::BATT_CRITICAL ? "CRITICAL"  : "UNKNOWN");
}

// ─── readVoltage() ────────────────────────────────────────────────────────────
float PowerMonitor::readVoltage() {
  // Use esp_adc_cal for accurate millivolt reading
  uint32_t sumMv = 0;
  for (int i = 0; i < SAMPLE_COUNT; i++) {
    uint32_t raw = analogRead(PM_VBAT_PIN);
    sumMv += esp_adc_cal_raw_to_voltage(raw, &_adcChars);
    delayMicroseconds(200);
  }
  float adcMv  = (float)sumMv / SAMPLE_COUNT;
  float vbat   = (adcMv / 1000.0f) * PM_DIVIDER_RATIO;
  return vbat;
}

// ─── voltToPercent() — LiPo discharge curve ──────────────────────────────────
int PowerMonitor::voltToPercent(float v) const {
  // Only valid for battery voltage 3.2-4.2V
  // If v > 4.2V battery is charging via USB
  if (v >= 4.20f) return 100;
  if (v >= 4.10f) return 90  + (int)((v - 4.10f) / 0.10f * 10.0f);
  if (v >= 4.00f) return 80  + (int)((v - 4.00f) / 0.10f * 10.0f);
  if (v >= 3.90f) return 70  + (int)((v - 3.90f) / 0.10f * 10.0f);
  if (v >= 3.80f) return 60  + (int)((v - 3.80f) / 0.10f * 10.0f);
  if (v >= 3.70f) return 50  + (int)((v - 3.70f) / 0.10f * 10.0f);
  if (v >= 3.60f) return 40  + (int)((v - 3.60f) / 0.10f * 10.0f);
  if (v >= 3.50f) return 25  + (int)((v - 3.50f) / 0.10f * 15.0f);
  if (v >= 3.40f) return 15  + (int)((v - 3.40f) / 0.10f * 10.0f);
  if (v >= 3.30f) return 5   + (int)((v - 3.30f) / 0.10f * 10.0f);
  if (v >= 3.20f) return (int)((v - 3.20f) / 0.10f * 5.0f);
  return 0;
}

// ─── trendV() — V/sample change over history window ──────────────────────────
float PowerMonitor::trendV() const {
  int count = _histFull ? HISTORY_SIZE : _histIdx;
  if (count < 4) return 0.0f;
  float newAvg = 0, oldAvg = 0;
  int half = count / 2;
  for (int i = 0; i < half; i++) {
    int ni = (_histIdx - 1 - i           + HISTORY_SIZE) % HISTORY_SIZE;
    int oi = (_histIdx - 1 - (half + i)  + HISTORY_SIZE) % HISTORY_SIZE;
    newAvg += _history[ni];
    oldAvg += _history[oi];
  }
  return (newAvg - oldAvg) / half;
}

// ─── updateState() ───────────────────────────────────────────────────────────
void PowerMonitor::updateState() {
  float v     = _voltage;
  float trend = trendV();

  // ── Power source ────────────────────────────────────────────────────────
  // USB presence detection:
  //   > 4.25V: TP4054 is actively charging — USB definitely present
  //   Rising trend: charging from USB (even if not yet above 4.25V)
  //   Stable at 4.20V+: USB present, battery full
  //   Otherwise: running on battery
  if (v > PM_VOLT_CHARGING) {
    // Clearly above battery max — USB charging
    _source = (v >= PM_VOLT_FULL && trend <= 0.002f)
              ? PowerSource::USB_FULL
              : PowerSource::USB_CHARGING;
  } else if (v >= PM_VOLT_FULL && trend <= 0.002f) {
    // At 4.20V stable — USB full
    _source = PowerSource::USB_FULL;
  } else if (trend > 0.008f) {
    // Voltage clearly rising — USB charging
    _source = PowerSource::USB_CHARGING;
  } else {
    _source = PowerSource::BATTERY;
  }

  // ── Charge state ─────────────────────────────────────────────────────────
  if (_source == PowerSource::USB_FULL) {
    _chargeState = ChargeState::FULL;
  } else if (_source == PowerSource::USB_CHARGING) {
    _chargeState = ChargeState::CHARGING;
  } else if (v <= PM_VOLT_CRITICAL) {
    _chargeState = ChargeState::BATT_CRITICAL;
  } else if (v <= PM_VOLT_LOW) {
    _chargeState = ChargeState::BATT_LOW;
  } else {
    _chargeState = ChargeState::DISCHARGING;
  }
}

// ─── updateHealth() ──────────────────────────────────────────────────────────
void PowerMonitor::updateHealth(float v) {
  if (v < _minVoltSeen && v > 3.0f) _minVoltSeen = v;  // ignore ADC noise
  if (v > _maxVoltSeen)              _maxVoltSeen = v;
  bool lowNow = (v <= PM_VOLT_LOW && _source == PowerSource::BATTERY);
  if (lowNow && !_wasLow) { _lowStartMs = millis(); _wasLow = true;  }
  else if (!lowNow && _wasLow) {
    _timeBelowLowMs += millis() - _lowStartMs; _wasLow = false;
  }
}

// ─── sendAlert() ─────────────────────────────────────────────────────────────
void PowerMonitor::sendAlert(const String &msg, const String &sev) {
  Serial.printf("[PowerMon] %s\n", msg.c_str());
  if (_alert && sev != SEV_INFO) _alert(msg, sev);
}

// ─── process() ───────────────────────────────────────────────────────────────
void PowerMonitor::process() {
  if (!_firstRead && millis() - _lastPollMs < _pollMs) return;
  _lastPollMs = millis();

  float v = readVoltage();
  if (v < 0.5f) return;

  _voltage = v;
  _percent = voltToPercent(v);

  // History ring buffer
  _history[_histIdx % HISTORY_SIZE] = v;
  _histIdx++;
  if (_histIdx >= HISTORY_SIZE) _histFull = true;
  _histIdx %= HISTORY_SIZE;

  updateHealth(v);
  updateState();

  Serial.printf("[PowerMon] %.3fV %d%% | %s | %s | trend:%+.4f\n",
    _voltage, _percent,
    isOnUSB() ? "USB" : "Battery",
    _chargeState == ChargeState::CHARGING    ? "CHARGING"    :
    _chargeState == ChargeState::FULL         ? "FULL"        :
    _chargeState == ChargeState::DISCHARGING  ? "DISCHARGING" :
    _chargeState == ChargeState::BATT_LOW     ? "LOW"         :
    _chargeState == ChargeState::BATT_CRITICAL? "CRITICAL"    : "UNKNOWN",
    trendV());

  // Alerts — only on state change
  if (_chargeState == ChargeState::BATT_LOW && !_lowAlertSent) {
    _lowAlertSent = true;
    sendAlert("[WARNING] Battery low: " + String(_percent) + "% ("
              + String(_voltage, 2) + "V) — connect USB", SEV_WARNING);
  }
  if (_chargeState != ChargeState::BATT_LOW) _lowAlertSent = false;

  if (_chargeState == ChargeState::BATT_CRITICAL && !_criticalAlertSent) {
    _criticalAlertSent = true;
    sendAlert("[ERROR] Battery CRITICAL: " + String(_percent) + "% ("
              + String(_voltage, 2) + "V) — shutting down soon", SEV_ERROR);
  }
  if (_chargeState != ChargeState::BATT_CRITICAL) _criticalAlertSent = false;

  _firstRead = false;
}

// ─── statusString() ──────────────────────────────────────────────────────────
String PowerMonitor::statusString() const {
  const char *src =
    (_source == PowerSource::USB_CHARGING) ? "USB+Charging" :
    (_source == PowerSource::USB_FULL)     ? "USB+Full"     :
    (_source == PowerSource::BATTERY)      ? "Battery"      : "Unknown";

  const char *st =
    (_chargeState == ChargeState::CHARGING)      ? "CHARGING"    :
    (_chargeState == ChargeState::FULL)           ? "FULL"        :
    (_chargeState == ChargeState::DISCHARGING)    ? "DISCHARGING" :
    (_chargeState == ChargeState::BATT_LOW)       ? "LOW"         :
    (_chargeState == ChargeState::BATT_CRITICAL)  ? "CRITICAL"    : "UNKNOWN";

  char buf[96];
  snprintf(buf, sizeof(buf), "%.2fV %d%% | %s | %s", _voltage, _percent, src, st);
  return String(buf);
}

// ─── healthString() ──────────────────────────────────────────────────────────
String PowerMonitor::healthString() const {
  const char *health =
    (_minVoltSeen >= 3.60f) ? "GOOD"     :
    (_minVoltSeen >= 3.40f) ? "FAIR"     :
    (_minVoltSeen >= 3.20f) ? "POOR"     : "DEGRADED";

  unsigned long lowSec = (_timeBelowLowMs +
    (_wasLow ? millis() - _lowStartMs : 0)) / 1000;

  char buf[120];
  snprintf(buf, sizeof(buf),
    "Health:%s | MinV:%.2fV MaxV:%.2fV | TimeLow:%lus",
    health, _minVoltSeen, _maxVoltSeen, lowSec);
  return String(buf);
}
