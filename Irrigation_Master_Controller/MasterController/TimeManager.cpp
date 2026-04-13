// TimeManager.cpp
#include "TimeManager.h"
#include <Preferences.h>  // MUST INCLUDE THIS

// Add extern declaration
extern Preferences prefs;

TimeManager::TimeManager() : wireRTC(nullptr), rtcAvailable(false), lastSyncCheck(0) {}

bool TimeManager::init(TwoWire *wire) {
  wireRTC = wire;
  
  if (wireRTC) {
    wireRTC->begin(RTC_SDA, RTC_SCL, 100000);
    delay(20);
    
    rtcAvailable = rtc.begin(wireRTC);
    
    if (rtcAvailable) {
      Serial.println("✓ RTC DS3231 detected");
      
      if (rtc.lostPower()) {
        Serial.println("⚠ RTC lost power, setting from compile time");
        rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
      }
      
      DateTime now = rtc.now();
      Serial.printf("  RTC time: %04d-%02d-%02d %02d:%02d:%02d\n",
                    now.year(), now.month(), now.day(),
                    now.hour(), now.minute(), now.second());
      
      // Set system time from RTC
      struct timeval tv;
      tv.tv_sec = now.unixtime();
      tv.tv_usec = 0;
      settimeofday(&tv, NULL);
      
      return true;
    } else {
      Serial.println("⚠ RTC not detected");
      return false;
    }
  }
  
  Serial.println("⚠ RTC Wire not provided");
  return false;
}

bool TimeManager::connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return true;

  Serial.println("[TimeManager] Connecting WiFi for NTP sync...");
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);  // Disable auto-reconnect (managed by NetworkManager)
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
    delay(200);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("[TimeManager] ✓ WiFi connected for NTP");
    return true;
  }

  Serial.println("[TimeManager] ❌ WiFi connection failed");
  return false;
}

void TimeManager::disconnectWiFi() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(100);
  Serial.println("[TimeManager] ✓ WiFi disconnected");
}

bool TimeManager::syncViaWiFiNTP() {
  Serial.println("[NTP] Syncing via WiFi...");
  
  if (!connectWiFi()) {
    return false;
  }
  
  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
  
  struct tm timeinfo;
  unsigned long start = millis();
  bool gotTime = false;
  
  while ((millis() - start) < NTP_TIMEOUT_MS) {
    if (getLocalTime(&timeinfo, 2000)) {
      gotTime = true;
      break;
    }
    delay(200);
  }
  
  if (!gotTime) {
    Serial.println("❌ NTP sync timeout");
    disconnectWiFi();
    return false;
  }
  
  time_t now = time(nullptr);
  Serial.printf("✓ NTP synced: %s", ctime(&now));
  
  // Update RTC if available
  if (rtcAvailable) {
    DateTime dt((uint32_t)now);
    rtc.adjust(dt);
    Serial.println("✓ RTC updated from NTP");
  }
  
  prefs.putULong("last_ntp_sync", (unsigned long)now);
  
  disconnectWiFi();
  return true;
}

bool TimeManager::syncNTP() {
  return syncViaWiFiNTP();
}

void TimeManager::checkDrift() {
  if (millis() - lastSyncCheck < SYNC_CHECK_INTERVAL_MS) {
    return;
  }
  
  lastSyncCheck = millis();
  
  if (!rtcAvailable) {
    Serial.println("[Drift] RTC not available, skipping");
    return;
  }
  
  DateTime rtcTime = rtc.now();
  time_t rtcEpoch = rtcTime.unixtime();
  time_t sysEpoch = time(nullptr);
  
  if (sysEpoch <= 0) {
    Serial.println("[Drift] System time invalid, attempting NTP sync");
    syncNTP();
    return;
  }
  
  long diff = (long)sysEpoch - (long)rtcEpoch;
  long absDrift = (diff >= 0) ? diff : -diff;
  
  Serial.printf("[Drift] Check: System=%ld RTC=%ld Diff=%ld sec\n", 
                (long)sysEpoch, (long)rtcEpoch, diff);
  
  if ((uint32_t)absDrift > DRIFT_THRESHOLD_S) {
    Serial.printf("⚠ Drift exceeds threshold (%ld > %ld), syncing NTP\n", 
                  absDrift, (long)DRIFT_THRESHOLD_S);
    if (syncNTP()) {
      Serial.println("✓ NTP sync successful");
    } else {
      Serial.println("❌ NTP sync failed");
    }
  } else {
    Serial.println("✓ Drift within threshold");
  }
}

bool TimeManager::isRTCAvailable() {
  return rtcAvailable;
}

DateTime TimeManager::getRTCTime() {
  if (rtcAvailable) {
    return rtc.now();
  }
  // Return epoch time (January 1, 2000)
  return DateTime((uint32_t)0);
}

time_t TimeManager::getRTCEpoch() {
  if (rtcAvailable) {
    return rtc.now().unixtime();
  }
  return 0;
}