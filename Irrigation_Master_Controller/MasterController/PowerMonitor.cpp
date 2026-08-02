// PowerMonitor.cpp  —  Power source and battery monitoring
// Heltec WiFi LoRa 32 V3 (ESP32-S3)
//
// ── GPIO37 (ADC_Ctrl / Vext_Ctrl) ────────────────────────────────────────────
//   Controls the p-channel MOSFET (AO7801) that gates the VBAT read circuit.
//   Heltec V3 confirmed behaviour:
//     GPIO37 HIGH → MOSFET ON  → VBAT divider connected → valid ADC read
//     GPIO37 LOW  → MOSFET OFF → divider disconnected   → ADC floats low
//   Note: must be set HIGH only during read, then restored to avoid
//   interfering with Vext / sensor power rail sharing.
//
// ── GPIO1 (ADC1_CH0 / VBAT_Read) ─────────────────────────────────────────────
//   VBAT voltage divider output: R_top=390kΩ, R_bot=100kΩ
//   ADC_V = VBAT × 100/(100+390) = VBAT × 0.2041
//   VBAT  = ADC_V × (100+390)/100 = ADC_V × 4.9
//
// ── Why NOT to use esp_adc_cal or analogSetAttenuation ───────────────────────
//   esp_adc_cal on ESP32-S3 returns wrong values when eFuse data is absent
//   (defaults to a fixed Vref that doesn't match actual hardware).
//   analogSetAttenuation() sets ALL ADC channels globally — breaks LoRa.
//   Solution: use raw analogRead with 0dB (default) attenuation.
//   At 0dB, ADC input range is 0-1.1V. VBAT divider gives 0.65-0.86V
//   for a 3.2-4.2V battery — perfectly within 0dB range. No attenuation needed.
//
// ── Calibration constant ─────────────────────────────────────────────────────
//   ESP32-S3 ADC: 12-bit (0-4095), Vref ≈ 1.1V (typical)
//   ADC_V = raw / 4095.0 × 1.1
//   VBAT  = ADC_V × 4.9 = raw × 1.1 × 4.9 / 4095 = raw × 0.001316
//   PM_SCALE_FACTOR = 1.1 × 4.9 / 4095 = 0.001316

#include "PowerMonitor.h"

// ── ADC scale factor ──────────────────────────────────────────────────────────
// VBAT (V) = raw_adc × PM_SCALE_FACTOR
// Tune PM_ADC_VREF if readings are consistently off:
//   If reading 3.50V but real voltage is 3.70V → increase VREF: 1.1 × (3.70/3.50) = 1.163
#define PM_ADC_VREF       1.1f    // ESP32-S3 internal Vref (typical)
#define PM_SCALE_FACTOR   (PM_ADC_VREF * PM_DIVIDER_RATIO / 4095.0f)

// ─── begin() ─────────────────────────────────────────────────────────────────
void PowerMonitor::begin() {
  // GPIO37: HIGH enables the VBAT read circuit (p-channel MOSFET gate)
  pinMode(PM_ADC_CTRL_PIN, OUTPUT);
  digitalWrite(PM_ADC_CTRL_PIN, HIGH);
  delay(20);  // let divider settle

  // GPIO1: ADC input — no pinMode or attenuation change needed
  // Default 0dB attenuation: 0-1.1V range, correct for VBAT divider output

  // First reading
  float v = readVoltage();
  if (v > 1.0f) {  // sanity: real battery is always >1V
    _voltage     = v;
    _minVoltSeen = v;
    _maxVoltSeen = v;
    _history[0]  = v;
    _histIdx     = 1;
    _percent     = voltToPercent(v);
    updateState();
    _firstRead   = false;
  }

  // Print raw ADC for diagnostic
  uint32_t rawSum = 0;
  for (int i = 0; i < 8; i++) rawSum += analogRead(PM_VBAT_PIN);
  uint32_t rawAvg = rawSum / 8;
  Serial.printf("[PowerMon] Init: raw=%u → %.3fV %d%% | %s\n",
    rawAvg, _voltage, _percent, statusString().c_str());
}

// ─── readVoltage() ────────────────────────────────────────────────────────────
float PowerMonitor::readVoltage() {
  // Ensure ADC_Ctrl is HIGH (may have been changed elsewhere)
  digitalWrite(PM_ADC_CTRL_PIN, HIGH);
  delayMicroseconds(500);

  // Average SAMPLE_COUNT readings to reduce noise
  uint32_t sum = 0;
  for (int i = 0; i < SAMPLE_COUNT; i++) {
    sum += analogRead(PM_VBAT_PIN);
    delayMicroseconds(200);
  }
  float raw  = (float)sum / SAMPLE_COUNT;
  float vbat = raw * PM_SCALE_FACTOR;
  return vbat;
}

// ─── voltToPercent() — LiPo 3.7V nominal discharge curve ─────────────────────
int PowerMonitor::voltToPercent(float v) const {
  if (v >= 4.20f) return 100;
  if (v >= 4.10f) return 90 + (int)((v-4.10f)/0.10f*10.0f);
  if (v >= 4.00f) return 80 + (int)((v-4.00f)/0.10f*10.0f);
  if (v >= 3.90f) return 70 + (int)((v-3.90f)/0.10f*10.0f);
  if (v >= 3.80f) return 60 + (int)((v-3.80f)/0.10f*10.0f);
  if (v >= 3.70f) return 50 + (int)((v-3.70f)/0.10f*10.0f);
  if (v >= 3.60f) return 40 + (int)((v-3.60f)/0.10f*10.0f);
  if (v >= 3.50f) return 25 + (int)((v-3.50f)/0.10f*15.0f);
  if (v >= 3.40f) return 15 + (int)((v-3.40f)/0.10f*10.0f);
  if (v >= 3.30f) return  5 + (int)((v-3.30f)/0.10f*10.0f);
  if (v >= 3.20f) return     (int)((v-3.20f)/0.10f*5.0f);
  return 0;
}

// ─── trendV() — V/sample rate of change ──────────────────────────────────────
float PowerMonitor::trendV() const {
  int count = _histFull ? HISTORY_SIZE : _histIdx;
  if (count < 4) return 0.0f;
  float newAvg = 0, oldAvg = 0;
  int half = count / 2;
  for (int i = 0; i < half; i++) {
    int ni = (_histIdx - 1 - i          + HISTORY_SIZE) % HISTORY_SIZE;
    int oi = (_histIdx - 1 - (half + i) + HISTORY_SIZE) % HISTORY_SIZE;
    newAvg += _history[ni];
    oldAvg += _history[oi];
  }
  return (newAvg - oldAvg) / half;
}

// ─── updateState() ───────────────────────────────────────────────────────────
void PowerMonitor::updateState() {
  float v     = _voltage;
  float trend = trendV();

  // USB detection:
  //   > 4.25V         → TP4054 charging above cell max → USB present
  //   >= 4.20V stable → battery full, USB still connected
  //   rising trend    → charging from USB
  //   otherwise       → battery power
  if (v > PM_VOLT_CHARGING) {
    _source = (v >= PM_VOLT_FULL && trend <= 0.002f)
              ? PowerSource::USB_FULL
              : PowerSource::USB_CHARGING;
  } else if (v >= PM_VOLT_FULL && trend <= 0.002f) {
    _source = PowerSource::USB_FULL;
  } else if (trend > 0.008f) {
    _source = PowerSource::USB_CHARGING;
  } else {
    _source = PowerSource::BATTERY;
  }

  // Charge state
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
  if (v > 3.0f && v < _minVoltSeen) _minVoltSeen = v;
  if (v > _maxVoltSeen)              _maxVoltSeen = v;
  bool lowNow = (v <= PM_VOLT_LOW && _source == PowerSource::BATTERY);
  if ( lowNow && !_wasLow) { _lowStartMs = millis(); _wasLow = true; }
  if (!lowNow &&  _wasLow) { _timeBelowLowMs += millis() - _lowStartMs; _wasLow = false; }
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
  if (v < 1.0f) return;  // below 1V = ADC not ready or hardware issue

  _voltage = v;
  _percent = voltToPercent(v);

  _history[_histIdx % HISTORY_SIZE] = v;
  _histIdx++;
  if (_histIdx >= HISTORY_SIZE) _histFull = true;
  _histIdx %= HISTORY_SIZE;

  updateHealth(v);
  updateState();

  Serial.printf("[PowerMon] %.3fV %d%% | %s | trend:%+.4f\n",
    _voltage, _percent,
    isOnUSB() ? "USB" : "Battery", trendV());

  if (_chargeState == ChargeState::BATT_LOW && !_lowAlertSent) {
    _lowAlertSent = true;
    sendAlert("[WARNING] Battery low: " + String(_percent) + "% ("
              + String(_voltage,2) + "V) — connect USB", SEV_WARNING);
  }
  if (_chargeState != ChargeState::BATT_LOW) _lowAlertSent = false;

  if (_chargeState == ChargeState::BATT_CRITICAL && !_criticalAlertSent) {
    _criticalAlertSent = true;
    sendAlert("[ERROR] Battery CRITICAL: " + String(_percent) + "% ("
              + String(_voltage,2) + "V)", SEV_ERROR);
  }
  if (_chargeState != ChargeState::BATT_CRITICAL) _criticalAlertSent = false;

  _firstRead = false;
}

// ─── calibrate() — call from Serial to find real scale factor ─────────────────
// Measure actual VBAT with a multimeter, then send: POWER CAL x.xx
// This adjusts PM_ADC_VREF so readings match reality.
void PowerMonitor::calibrate(float realVoltage) {
  float rawAvg = 0;
  for (int i = 0; i < 32; i++) { rawAvg += analogRead(PM_VBAT_PIN); delayMicroseconds(200); }
  rawAvg /= 32;
  float measuredScale = realVoltage / rawAvg;
  Serial.printf("[PowerMon] CAL: raw=%.1f realV=%.3f → scale=%.6f\n",
                rawAvg, realVoltage, measuredScale);
  Serial.printf("[PowerMon] Set PM_ADC_VREF = %.4f in PowerMonitor.cpp\n",
                measuredScale * 4095.0f / PM_DIVIDER_RATIO);
  _calScale = measuredScale;
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
  const char *h =
    (_minVoltSeen >= 3.60f) ? "GOOD"     :
    (_minVoltSeen >= 3.40f) ? "FAIR"     :
    (_minVoltSeen >= 3.20f) ? "POOR"     : "DEGRADED";
  unsigned long lowSec = (_timeBelowLowMs +
    (_wasLow ? millis()-_lowStartMs : 0)) / 1000;
  char buf[120];
  snprintf(buf,sizeof(buf),"Health:%s | MinV:%.2fV MaxV:%.2fV | TimeLow:%lus",
    h, _minVoltSeen, _maxVoltSeen, lowSec);
  return String(buf);
}
