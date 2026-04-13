// TimeManager.h
#ifndef TIME_MANAGER_H
#define TIME_MANAGER_H

#include <RTClib.h>
#include <Wire.h>
#include <WiFi.h>
#include "Config.h"

class TimeManager {
private:
  RTC_DS3231 rtc;
  TwoWire *wireRTC;
  bool rtcAvailable;
  unsigned long lastSyncCheck;
  
  bool connectWiFi();
  void disconnectWiFi();
  bool syncViaWiFiNTP();

public:
  TimeManager();
  bool init(TwoWire *wire);
  bool syncNTP();
  void checkDrift();
  bool isRTCAvailable();
  DateTime getRTCTime();
  time_t getRTCEpoch();
};

extern TimeManager timeManager;

#endif