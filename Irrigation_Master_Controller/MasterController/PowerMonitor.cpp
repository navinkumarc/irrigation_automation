// PowerMonitor.cpp  —  Power source and battery monitoring
#include "PowerMonitor.h"

// ─── begin() ─────────────────────────────────────────────────────────────────
void PowerMonitor::begin() {
  // ADC_Ctrl (GPIO37) must be HIGH to enable the ADC power circuit
  pinMode(PM_ADC_CTRL_PIN, OUTPUT);
  digitalWrite(PM_ADC_CTRL_PIN, HIGH);

  // GPIO1 as ADC input (no pinMode needed for ADC on ESP32-S3,
  // but set for documentation)
  pinMode(PM_VBAT_PIN, INPUT);

  // Take an immediate first reading so status is valid before first poll
  _voltage = readVoltage();
  if (_voltage > 0.5f) {
    _minVoltSeen  = _voltage;
    _maxVoltSeen  = _voltage;
    _history[0]   = _voltage;
    _histIdx      = 1;
    _percent      = voltToPercent(_voltage);
    updateState();
    _firstRead    = false;
  }

  Serial.printf("[PowerMon] Init: %.2fV %d%% source:%s state:%s\n",
    _voltage, _percent, statusString().c_str(), healthString().c_str());
}

// ─── readVoltage() ────────────────────────────────────────────────────────────
float PowerMonitor::readVoltage() {
  // Average SAMPLE_COUNT ADC readings to reduce noise
  uint32_t sum = 0;
  for (int i = 0; i < SAMPLE_COUNT; i++) {
    sum += analogRead(PM_VBAT_PIN);
    delayMicroseconds(200);
  }
  float raw     = (float)sum / SAMPLE_COUNT;
  float adc_v   = raw / PM_ADC_RESOLUTION * PM_ADC_REF_V;
  float vbat    = adc_v * PM_DIVIDER_RATIO;
  return vbat;
}

// ─── voltToPercent() — LiPo discharge curve ──────────────────────────────────
int PowerMonitor::voltToPercent(float v) const {
  // Piecewise linear approximation of LiPo discharge curve
  if (v >= 4.20f) return 100;
  if (v >= 4.10f) return 90  + (int)((v - 4.10f) / 0.10f * 10);
  if (v >= 4.00f) return 80  + (int)((v - 4.00f) / 0.10f * 10);
  if (v >= 3.90f) return 70  + (int)((v - 3.90f) / 0.10f * 10);
  if (v >= 3.80f) return 60  + (int)((v - 3.80f) / 0.10f * 10);
  if (v >= 3.70f) return 50  + (int)((v - 3.70f) / 0.10f * 10);
  if (v >= 3.60f) return 40  + (int)((v - 3.60f) / 0.10f * 10);
  if (v >= 3.50f) return 25  + (int)((v - 3.50f) / 0.10f * 15);
  if (v >= 3.40f) return 15  + (int)((v - 3.40f) / 0.10f * 10);
  if (v >= 3.30f) return 5   + (int)((v - 3.30f) / 0.10f * 10);
  if (v >= 3.20f) return (int)((v - 3.20f) / 0.10f * 5);
  return 0;
}

// ─── trendV() — average rate of voltage change (V/sample) ───────────────────
float PowerMonitor::trendV() const {
  int count = _histFull ? HISTORY_SIZE : _histIdx;
  if (count < 3) return 0.0f;
  // Compare newest half to oldest half
  float newAvg = 0, oldAvg = 0;
  int half = count / 2;
  for (int i = 0; i < half; i++) {
    int ni = (_histIdx - 1 - i + HISTORY_SIZE) % HISTORY_SIZE;
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

  // Determine power source
  if (v >= PM_VOLT_CHARGING) {
    // Above 4.25V — USB must be connected (battery can't self-charge this high)
    _source = (v >= PM_VOLT_FULL && trend <= 0.005f)
              ? PowerSource::USB_FULL
              : PowerSource::USB_CHARGING;
  } else if (trend > 0.01f) {
    // Voltage rising even below 4.25V — still charging from USB
    _source = PowerSource::USB_CHARGING;
  } else {
    _source = PowerSource::BATTERY;
  }

  // Determine charge state
  if (_source == PowerSource::USB_FULL || v >= PM_VOLT_FULL) {
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
  if (v < _minVoltSeen) _minVoltSeen = v;
  if (v > _maxVoltSeen) _maxVoltSeen = v;

  // Track cumulative time below LOW threshold
  bool lowNow = (v <= PM_VOLT_LOW && _source == PowerSource::BATTERY);
  if (lowNow && !_wasLow) {
    _lowStartMs = millis();
    _wasLow = true;
  } else if (!lowNow && _wasLow) {
    _timeBelowLowMs += millis() - _lowStartMs;
    _wasLow = false;
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

  // Read and average
  float v = readVoltage();
  if (v < 0.5f) return;  // ADC not ready / no battery

  _voltage = v;
  _percent = voltToPercent(v);

  // Update history ring buffer
  _history[_histIdx % HISTORY_SIZE] = v;
  _histIdx++;
  if (_histIdx >= HISTORY_SIZE) _histFull = true;
  _histIdx %= HISTORY_SIZE;

  updateHealth(v);
  updateState();

  Serial.printf("[PowerMon] %.2fV %d%% src:%s state:%s trend:%+.3f\n",
    _voltage, _percent,
    isOnUSB() ? "USB" : "BAT",
    chargeState() == ChargeState::CHARGING  ? "CHARGING"    :
    chargeState() == ChargeState::FULL       ? "FULL"        :
    chargeState() == ChargeState::DISCHARGING? "DISCHARGING" :
    chargeState() == ChargeState::BATT_LOW        ? "LOW"         :
    chargeState() == ChargeState::BATT_CRITICAL   ? "CRITICAL"    : "UNKNOWN",
    trendV());

  // ── User alerts for state changes ────────────────────────────────────────
  if (_chargeState == ChargeState::BATT_LOW && !_lowAlertSent) {
    _lowAlertSent = true;
    sendAlert("[WARNING] Battery low: " + String(_percent) + "% ("
              + String(_voltage, 2) + "V) — connect USB power", SEV_WARNING);
  }
  if (_chargeState != ChargeState::BATT_LOW) _lowAlertSent = false;

  if (_chargeState == ChargeState::BATT_CRITICAL && !_criticalAlertSent) {
    _criticalAlertSent = true;
    sendAlert("[ERROR] Battery CRITICAL: " + String(_percent) + "% ("
              + String(_voltage, 2) + "V) — device may shut down soon", SEV_ERROR);
  }
  if (_chargeState != ChargeState::BATT_CRITICAL) _criticalAlertSent = false;

  _firstRead = false;
}

// ─── statusString() ──────────────────────────────────────────────────────────
String PowerMonitor::statusString() const {
  const char *src =
    (_source == PowerSource::USB_CHARGING) ? "USB(charging)" :
    (_source == PowerSource::USB_FULL)     ? "USB(full)"     :
    (_source == PowerSource::BATTERY)      ? "Battery"       : "Unknown";

  const char *state =
    (_chargeState == ChargeState::CHARGING)   ? "CHARGING"    :
    (_chargeState == ChargeState::FULL)        ? "FULL"        :
    (_chargeState == ChargeState::DISCHARGING) ? "DISCHARGING" :
    (_chargeState == ChargeState::BATT_LOW)         ? "LOW"         :
    (_chargeState == ChargeState::BATT_CRITICAL)    ? "CRITICAL"    : "UNKNOWN";

  char buf[80];
  snprintf(buf, sizeof(buf), "%.2fV %d%% | %s | %s",
           _voltage, _percent, src, state);
  return String(buf);
}

// ─── healthString() ──────────────────────────────────────────────────────────
String PowerMonitor::healthString() const {
  // Health estimate based on how far min voltage dropped
  const char *health;
  if      (_minVoltSeen >= 3.50f) health = "GOOD";
  else if (_minVoltSeen >= 3.30f) health = "FAIR";
  else if (_minVoltSeen >= 3.10f) health = "POOR";
  else                             health = "DEGRADED";

  unsigned long lowSec = (_timeBelowLowMs +
    (_wasLow ? millis() - _lowStartMs : 0)) / 1000;

  char buf[120];
  snprintf(buf, sizeof(buf),
    "Health:%s | MinV:%.2fV MaxV:%.2fV | TimeLow:%lus",
    health, _minVoltSeen, _maxVoltSeen, lowSec);
  return String(buf);
}
