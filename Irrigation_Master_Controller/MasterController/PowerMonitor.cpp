// PowerMonitor.cpp  —  Power source and battery monitoring
//
// ── Official datasheet facts (HTIT-WB32_V3 / HTIT-WB32LA_V3) ────────────────
//   GPIO1  = ADC1_CH0 = VBAT Read  (Header J2, pin 18)
//   GPIO37 = ADC_Ctrl              (Header J3, pin 11, has onboard pull-up)
//
//   VBAT formula (datasheet footnote 3):
//     VBAT = 100/(100+390) × VADC_IN1
//     ∴ VADC_IN1 = VBAT × (100+390)/100 = VBAT × 4.9
//     ∴ VBAT     = VADC_IN1 × 4.9              ← our conversion direction
//
//   GPIO37 pull-up (yellow arrow in pin map = Pull Up/Down):
//     Default HIGH (pull-up) = ADC circuit DISABLED
//     Drive LOW              = ADC circuit ENABLED
//     Drive HIGH after read  = disable (saves power)
//
//   ADC attenuation: NONE (0dB, default)
//     VADC_IN1 range: 0.653V (3.2V bat) to 0.857V (4.2V bat)
//     ESP32-S3 0dB input range: 0 to ~950mV  ← fits perfectly, no attenuation needed
//     analogSetPinAttenuation NOT called — would break the scale factor
//
//   Scale factor:
//     VBAT = raw × (Vref / 4095) × 4.9
//     Vref ≈ 1.1V on ESP32-S3 at 0dB (use POWER CAL to tune)
//     Default: VBAT = raw × 1.1 × 4.9 / 4095 = raw × 0.001316

#include "PowerMonitor.h"

// VBAT = raw x (PM_ADC_FULLSCALE / 4095) x PM_DIVIDER_RATIO
// PM_ADC_FULLSCALE is defined in PowerMonitor.h (3.9V for 11dB).
// Tune with POWER CAL <multimeter_reading> if it differs from reality.
#define PM_SCALE      (PM_ADC_FULLSCALE * PM_DIVIDER_RATIO / 4095.0f)

// ─── begin() ─────────────────────────────────────────────────────────────────
void PowerMonitor::begin() {
  // GPIO37: HIGH enables the VBAT read circuit (verified on hardware).
  pinMode(PM_ADC_CTRL_PIN, OUTPUT);
  digitalWrite(PM_ADC_CTRL_PIN, PM_ADC_CTRL_ACTIVE);

  // GPIO1: set 11dB explicitly on this pin only, so the scale factor
  // holds regardless of what the core default happens to be.
  analogSetPinAttenuation(PM_VBAT_PIN, ADC_11db);

#if MAINS_SENSE_PIN != 0
  // 5V-rail sense: divider already defines the level, no pull needed
  pinMode(MAINS_SENSE_PIN, INPUT);
#endif
  delay(20);   // let divider settle

  // First reading — always store it, even if implausible, so the
  // reported value reflects what the ADC actually measured.
  float v = readVoltage();
  _voltage     = v;
  _minVoltSeen = v;
  _maxVoltSeen = v;
  _history[0]  = v;
  _histIdx     = 1;
  _percent     = voltToPercent(v);
  updateState();
  updateMains();
  _firstRead   = false;

  // Diagnostic — raw value for calibration verification
  uint32_t rawSum = 0;
  for (int i = 0; i < 8; i++) { rawSum += analogRead(PM_VBAT_PIN); delayMicroseconds(200); }
  uint32_t rawAvg = rawSum / 8;
  float    adcV   = rawAvg / 4095.0f * PM_ADC_FULLSCALE;
  Serial.printf("[PowerMon] Init: GPIO37=HIGH raw=%u adcV=%.3fV vbat=%.3fV %d%% | %s\n",
    rawAvg, adcV, _voltage, _percent, statusString().c_str());
}

// ─── readVoltage() ────────────────────────────────────────────────────────────
float PowerMonitor::readVoltage() {
  float scale = (_calScale > 0) ? _calScale : PM_SCALE;

  digitalWrite(PM_ADC_CTRL_PIN, PM_ADC_CTRL_ACTIVE);
  delayMicroseconds(500);

  uint32_t sum = 0;
  for (int i = 0; i < SAMPLE_COUNT; i++) {
    sum += analogRead(PM_VBAT_PIN);
    delayMicroseconds(200);
  }
  return (float)sum / SAMPLE_COUNT * scale;
}

// ─── voltToPercent() — LiPo 3.7V nominal ─────────────────────────────────────
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
    int ni = (_histIdx-1-i         +HISTORY_SIZE)%HISTORY_SIZE;
    int oi = (_histIdx-1-(half+i)  +HISTORY_SIZE)%HISTORY_SIZE;
    newAvg += _history[ni];
    oldAvg += _history[oi];
  }
  return (newAvg - oldAvg) / half;
}

// ─── updateState() ───────────────────────────────────────────────────────────
void PowerMonitor::updateState() {
  float v = _voltage, trend = trendV();
  if      (v > PM_VOLT_CHARGING)                      _source = PowerSource::USB_CHARGING;
  else if (v >= PM_VOLT_FULL && trend <= 0.002f)      _source = PowerSource::USB_FULL;
  else if (trend > 0.008f)                            _source = PowerSource::USB_CHARGING;
  else                                                 _source = PowerSource::BATTERY;

  if      (_source == PowerSource::USB_FULL)           _chargeState = ChargeState::FULL;
  else if (_source == PowerSource::USB_CHARGING)       _chargeState = ChargeState::CHARGING;
  else if (v <= PM_VOLT_CRITICAL)                      _chargeState = ChargeState::BATT_CRITICAL;
  else if (v <= PM_VOLT_LOW)                           _chargeState = ChargeState::BATT_LOW;
  else                                                 _chargeState = ChargeState::DISCHARGING;
}


// ─── updateMains() ───────────────────────────────────────────────────────────
// Preferred: read the 5V-rail sense pin — direct and unambiguous.
// Fallback:  infer from resting battery voltage with hysteresis.
//
// Why not use the voltage trend: a LiPo in the constant-current phase rises
// about 0.0001 V/s, and once it reaches the constant-voltage phase it stops
// rising altogether. A threshold low enough to catch that is indistinguishable
// from ADC noise, so trend alone cannot decide this.
void PowerMonitor::updateMains() {
#if MAINS_SENSE_PIN != 0
  bool level = (digitalRead(MAINS_SENSE_PIN) == HIGH);
  _mainsOn        = MAINS_SENSE_HIGH ? level : !level;
  _mainsKnownOnce = true;
  return;
#else
  if (_voltage < 2.0f) return;            // no usable reading yet

  if (_voltage >= PM_MAINS_USB_CERTAIN) {
    _mainsOn        = true;               // only a charger holds it this high
    _mainsKnownOnce = true;
  } else if (_voltage <= PM_MAINS_BATT_CERTAIN) {
    _mainsOn        = false;              // a charger would not let it sag here
    _mainsKnownOnce = true;
  } else if (trendV() > 0.004f) {
    _mainsOn        = true;               // rising within the ambiguous band
    _mainsKnownOnce = true;
  }
  // else: ambiguous band, no clear trend -> keep the previous determination
#endif
}

// ─── updateHealth() ──────────────────────────────────────────────────────────
void PowerMonitor::updateHealth(float v) {
  if (v < 2.0f) return;   // implausible — do not pollute health stats
  if (v < _minVoltSeen) _minVoltSeen = v;
  if (v > _maxVoltSeen) _maxVoltSeen = v;
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
  if (!_firstRead && millis()-_lastPollMs < _pollMs) return;
  _lastPollMs = millis();

  float v = readVoltage();

  _voltage = v;
  _percent = voltToPercent(v);

  _history[_histIdx % HISTORY_SIZE] = v;
  _histIdx++;
  if (_histIdx >= HISTORY_SIZE) _histFull = true;
  _histIdx %= HISTORY_SIZE;

  updateHealth(v);
  updateState();
  updateMains();

  Serial.printf("[PowerMon] %.3fV %d%% | %s | trend:%+.4f\n",
    _voltage, _percent, isOnUSB() ? "USB" : "Battery", trendV());

  // ── Mains transition alert — pumps cannot run without mains ────────────
  if (_mainsKnownOnce && _mainsOn != _lastMains) {
    if (_mainsOn)
      sendAlert("[INFO] Mains power RESTORED — pumps available", SEV_WARNING);
    else
      sendAlert("[WARNING] Mains power LOST — pumps unavailable, on battery",
                SEV_WARNING);
  }
  _lastMains = _mainsOn;

  if (_chargeState == ChargeState::BATT_LOW && !_lowAlertSent) {
    _lowAlertSent = true;
    sendAlert("[WARNING] Battery low: " + String(_percent) + "% ("
              + String(_voltage,2) + "V)", SEV_WARNING);
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


// ─── diagnose() — test both GPIO37 polarities and report raw ADC ─────────────
// Run this when readings look wrong. Whichever polarity gives a raw value in
// the plausible band (roughly 2400-3600 for a 3.2-4.2V battery) is correct.
String PowerMonitor::diagnose() {
  auto sample = [&]() -> uint32_t {
    uint32_t s = 0;
    for (int i = 0; i < 16; i++) { s += analogRead(PM_VBAT_PIN); delayMicroseconds(200); }
    return s / 16;
  };

  pinMode(PM_ADC_CTRL_PIN, OUTPUT);

  digitalWrite(PM_ADC_CTRL_PIN, LOW);
  delay(50);
  uint32_t rawLow = sample();

  digitalWrite(PM_ADC_CTRL_PIN, HIGH);
  delay(50);
  uint32_t rawHigh = sample();

  // Also try leaving the pin as a floating input (let the board pull-up decide)
  pinMode(PM_ADC_CTRL_PIN, INPUT);
  delay(50);
  uint32_t rawFloat = sample();

  // Restore the configured active level
  pinMode(PM_ADC_CTRL_PIN, OUTPUT);
  digitalWrite(PM_ADC_CTRL_PIN, PM_ADC_CTRL_ACTIVE);

  float scale = (_calScale > 0) ? _calScale : PM_SCALE;

  char buf[240];
  snprintf(buf, sizeof(buf),
    "GPIO37=LOW   raw=%4u -> %.2fV\n"
    "GPIO37=HIGH  raw=%4u -> %.2fV\n"
    "GPIO37=FLOAT raw=%4u -> %.2fV\n"
    "Pick the line matching your real battery voltage,\n"
    "then set PM_ADC_CTRL_ACTIVE in PowerMonitor.h to match.",
    rawLow,   rawLow   * scale,
    rawHigh,  rawHigh  * scale,
    rawFloat, rawFloat * scale);
  Serial.println(buf);
  return String(buf);
}

// ─── calibrate() ─────────────────────────────────────────────────────────────
void PowerMonitor::calibrate(float realVoltage) {
  digitalWrite(PM_ADC_CTRL_PIN, PM_ADC_CTRL_ACTIVE); delay(10);
  float rawAvg = 0;
  for (int i = 0; i < 32; i++) { rawAvg += analogRead(PM_VBAT_PIN); delayMicroseconds(200); }
  rawAvg /= 32;
  _calScale = realVoltage / rawAvg;
  float newFS = _calScale * 4095.0f / PM_DIVIDER_RATIO;
  Serial.printf("[PowerMon] CAL: raw=%.1f → realV=%.3fV scale=%.7f\n",
                rawAvg, realVoltage, _calScale);
  Serial.printf("[PowerMon] Set PM_ADC_FULLSCALE = %.4f in PowerMonitor.h\n", newFS);
}

// ─── statusString() ──────────────────────────────────────────────────────────
String PowerMonitor::statusString() const {
  const char *src =
    (_source == PowerSource::USB_CHARGING) ? "USB+Charging" :
    (_source == PowerSource::USB_FULL)     ? "USB+Full"     :
    (_source == PowerSource::BATTERY)      ? "Battery"      : "Unknown";
  const char *st =
    (_chargeState == ChargeState::CHARGING)     ? "CHARGING"    :
    (_chargeState == ChargeState::FULL)          ? "FULL"        :
    (_chargeState == ChargeState::DISCHARGING)   ? "DISCHARGING" :
    (_chargeState == ChargeState::BATT_LOW)      ? "LOW"         :
    (_chargeState == ChargeState::BATT_CRITICAL) ? "CRITICAL"    : "UNKNOWN";
  char buf[128];
  snprintf(buf, sizeof(buf), "Mains:%s | %.2fV %d%% | %s | %s",
           mainsString(), _voltage, _percent, src, st);
  return String(buf);
}

// ─── healthString() ──────────────────────────────────────────────────────────
String PowerMonitor::healthString() const {
  // No plausible reading yet → do not claim a health grade
  if (_voltage < 2.0f) return String("Health:UNKNOWN | no valid ADC reading");
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
