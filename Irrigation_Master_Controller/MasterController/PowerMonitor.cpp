// PowerMonitor.cpp  —  Power source and battery monitoring
// Heltec WiFi LoRa 32 V3 (ESP32-S3)
//
// ── Confirmed correct procedure (from heltec_unofficial library) ──────────────
//   VBAT_CTRL = GPIO37  → digital output, LOW = enable read, HIGH = disable
//   VBAT_ADC  = GPIO1   → ADC1_CH0, needs 11dB attenuation (per-pin only)
//
//   GPIO37 LOW  → MOSFET ON  → VBAT divider connected → valid reading
//   GPIO37 HIGH → MOSFET OFF → divider disconnected   → ADC floats (~1V noise)
//
// ── Why 11dB attenuation on GPIO1 only ───────────────────────────────────────
//   ESP32-S3 ADC at 0dB: Vref ≈ 950mV, max input ≈ 950mV
//   VBAT divider output for 3.7V battery: 3.7/4.9 = 0.755V  ← close to limit
//   VBAT divider output for 4.2V battery: 4.2/4.9 = 0.857V  ← OVER 0dB range!
//   At 11dB: max input ≈ 3.9V → 0.857V is well within range ✓
//   analogSetPinAttenuation(1, ADC_11db) changes ONLY GPIO1, not LoRa/sensors
//
// ── Scale factor at 11dB ─────────────────────────────────────────────────────
//   ADC_V = raw × 3.9 / 4095      (3.9V full scale at 11dB)
//   VBAT  = ADC_V × 4.9           (resistor divider ratio)
//   VBAT  = raw × 3.9 × 4.9 / 4095 = raw × 0.004664
//   Calibration constant PM_ADC_VREF can be tuned if readings are off.

#include "PowerMonitor.h"

// Scale factor: VBAT = raw × PM_ADC_FULLSCALE × PM_DIVIDER_RATIO / 4095
// PM_ADC_FULLSCALE for 11dB = 3.9V (nominal; real value varies ±5%)
// Tune PM_ADC_FULLSCALE if measured voltage differs from POWER RAW output
#define PM_ADC_FULLSCALE  3.9f

// ─── begin() ─────────────────────────────────────────────────────────────────
void PowerMonitor::begin() {
  // GPIO37: LOW enables the VBAT read circuit
  pinMode(PM_ADC_CTRL_PIN, OUTPUT);
  digitalWrite(PM_ADC_CTRL_PIN, LOW);   // enable
  delay(20);

  // Set 11dB attenuation on GPIO1 ONLY (not global — won't affect LoRa/sensors)
  analogSetPinAttenuation(PM_VBAT_PIN, ADC_11db);

  float v = readVoltage();
  if (v > 2.0f) {   // sanity: real battery always >2V
    _voltage     = v;
    _minVoltSeen = v;
    _maxVoltSeen = v;
    _history[0]  = v;
    _histIdx     = 1;
    _percent     = voltToPercent(v);
    updateState();
    _firstRead   = false;
  }

  // Diagnostic print — shows raw value for calibration
  uint32_t rawSum = 0;
  for (int i = 0; i < 8; i++) rawSum += analogRead(PM_VBAT_PIN);
  uint32_t rawAvg = rawSum / 8;
  float adcV  = rawAvg / 4095.0f * PM_ADC_FULLSCALE;
  float vbat  = adcV  * PM_DIVIDER_RATIO;
  Serial.printf("[PowerMon] Init: GPIO37=LOW GPIO1 raw=%u adcV=%.3fV vbat=%.3fV %d%%\n",
                rawAvg, adcV, vbat, _percent);
  Serial.printf("[PowerMon] Status: %s\n", statusString().c_str());

  // Disable after reading to save power (re-enabled in readVoltage each time)
  digitalWrite(PM_ADC_CTRL_PIN, HIGH);
}

// ─── readVoltage() ────────────────────────────────────────────────────────────
float PowerMonitor::readVoltage() {
  // Enable read circuit
  digitalWrite(PM_ADC_CTRL_PIN, LOW);
  delayMicroseconds(500);

  float scale = (_calScale > 0) ? _calScale
                                 : (PM_ADC_FULLSCALE * PM_DIVIDER_RATIO / 4095.0f);

  uint32_t sum = 0;
  for (int i = 0; i < SAMPLE_COUNT; i++) {
    sum += analogRead(PM_VBAT_PIN);
    delayMicroseconds(200);
  }
  float raw  = (float)sum / SAMPLE_COUNT;
  float vbat = raw * scale;

  // Disable after reading
  digitalWrite(PM_ADC_CTRL_PIN, HIGH);
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
  if (v >= 3.20f) return     (int)((v-3.20f)/0.10f* 5.0f);
  return 0;
}

// ─── trendV() ────────────────────────────────────────────────────────────────
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

  if (v > PM_VOLT_CHARGING) {
    _source = (v >= PM_VOLT_FULL && trend <= 0.002f)
              ? PowerSource::USB_FULL : PowerSource::USB_CHARGING;
  } else if (v >= PM_VOLT_FULL && trend <= 0.002f) {
    _source = PowerSource::USB_FULL;
  } else if (trend > 0.008f) {
    _source = PowerSource::USB_CHARGING;
  } else {
    _source = PowerSource::BATTERY;
  }

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
  if (!lowNow &&  _wasLow) { _timeBelowLowMs += millis()-_lowStartMs; _wasLow = false; }
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
  if (v < 2.0f) return;

  _voltage = v;
  _percent = voltToPercent(v);

  _history[_histIdx % HISTORY_SIZE] = v;
  _histIdx++;
  if (_histIdx >= HISTORY_SIZE) _histFull = true;
  _histIdx %= HISTORY_SIZE;

  updateHealth(v);
  updateState();

  Serial.printf("[PowerMon] %.3fV %d%% | %s | trend:%+.4f\n",
    _voltage, _percent, isOnUSB() ? "USB" : "Battery", trendV());

  if (_chargeState == ChargeState::BATT_LOW && !_lowAlertSent) {
    _lowAlertSent = true;
    sendAlert("[WARNING] Battery low: " + String(_percent) + "% (" +
              String(_voltage,2) + "V) — connect USB", SEV_WARNING);
  }
  if (_chargeState != ChargeState::BATT_LOW) _lowAlertSent = false;

  if (_chargeState == ChargeState::BATT_CRITICAL && !_criticalAlertSent) {
    _criticalAlertSent = true;
    sendAlert("[ERROR] Battery CRITICAL: " + String(_percent) + "% (" +
              String(_voltage,2) + "V)", SEV_ERROR);
  }
  if (_chargeState != ChargeState::BATT_CRITICAL) _criticalAlertSent = false;

  _firstRead = false;
}

// ─── calibrate() ─────────────────────────────────────────────────────────────
void PowerMonitor::calibrate(float realVoltage) {
  digitalWrite(PM_ADC_CTRL_PIN, LOW);
  delay(10);
  float rawAvg = 0;
  for (int i = 0; i < 32; i++) { rawAvg += analogRead(PM_VBAT_PIN); delayMicroseconds(200); }
  rawAvg /= 32;
  digitalWrite(PM_ADC_CTRL_PIN, HIGH);
  float newScale = realVoltage / rawAvg;
  _calScale = newScale;
  Serial.printf("[PowerMon] CAL: raw=%.1f realV=%.3fV → scale=%.7f\n",rawAvg,realVoltage,newScale);
  Serial.printf("[PowerMon] Set PM_ADC_FULLSCALE = %.4f in PowerMonitor.cpp\n",
                newScale * 4095.0f / PM_DIVIDER_RATIO);
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
  snprintf(buf, sizeof(buf), "Health:%s | MinV:%.2fV MaxV:%.2fV | TimeLow:%lus",
    h, _minVoltSeen, _maxVoltSeen, lowSec);
  return String(buf);
}
