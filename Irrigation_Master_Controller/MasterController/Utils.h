// Utils.h - Common utility functions
#ifndef UTILS_H
#define UTILS_H

#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include "Config.h"

// ========== Extern References ==========
extern Preferences prefs;

// ========== Phone Number Utilities ==========
String normalizePhone(const String &in);
std::vector<String> adminPhoneList();
bool isAdminNumber(const String &num);

// ========== Token & Authentication ==========
String extractSrc(const String &payload);
String extractKeyVal(const String &payload, const String &key);
bool verifyTokenForSrc(const String &payload, const String &fromNumber = "");

// ========== Time Utilities ==========
String nowISO8601();
String formatTimeShort();
bool parseTimeHHMM(const String &t, int &hour, int &minute);
time_t nextWeekdayOccurrence(time_t now, uint8_t weekday_mask, int hour, int minute);

// ========== Message ID ==========
uint32_t getNextMsgId();

// ========== Debugging ==========
void debugPrint(const String &s);

// ========== Power Control (Heltec) ==========
void VextON();
void VextOFF();

#endif // UTILS_H