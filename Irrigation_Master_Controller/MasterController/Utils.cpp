// Utils.cpp - Implementation of utility functions
#include "Utils.h"
#include "heltec.h"

// ========== Phone Number Utilities ==========
String normalizePhone(const String &in) {
  String s = in;
  s.trim();
  s.replace(" ", "");
  if (s.length() > 0 && s.charAt(0) == '0') s = s.substring(1);
  // Use configurable country code instead of hardcoded +91
  if (s.length() == 10 && s.charAt(0) != '+') s = String(DEFAULT_COUNTRY_CODE) + s;
  return s;
}

std::vector<String> adminPhoneList() {
  std::vector<String> out;
  String s = sysConfig.adminPhones;
  s.trim();
  if (s.length() == 0) return out;
  
  int p = 0;
  while (p < (int)s.length()) {
    int c = s.indexOf(',', p);
    String part = (c == -1) ? s.substring(p) : s.substring(p, c);
    part.trim();
    if (part.length()) out.push_back(part);
    if (c == -1) break;
    p = c + 1;
  }
  return out;
}

bool isAdminNumber(const String &num) {
  String n = normalizePhone(num);
  auto list = adminPhoneList();
  for (auto &p : list)
    if (normalizePhone(p) == n) return true;
  return false;
}

// ========== Token & Authentication ==========
String extractSrc(const String &payload) {
  int p = payload.indexOf("SRC=");
  if (p < 0) return String("UNKNOWN");
  String s = payload.substring(p + 4);
  int c = s.indexOf(',');
  if (c >= 0) s = s.substring(0, c);
  s.trim();
  return s;
}

String extractKeyVal(const String &payload, const String &key) {
  int p = payload.indexOf(key + "=");
  if (p < 0) return String("");
  String s = payload.substring(p + key.length() + 1);
  int c = s.indexOf(',');
  if (c >= 0) s = s.substring(0, c);
  s.trim();
  return s;
}

bool verifyTokenForSrc(const String &payload, const String &fromNumber) {
  String src = extractSrc(payload);
  
  if (src == "SMS") {
    if (fromNumber.length()) {
      if (isAdminNumber(fromNumber)) return true;
      String rec = extractKeyVal(payload, "RECOV");
      if (rec.length() && rec == sysConfig.recoveryTok) {
        Serial.println("Recovery token accepted for SMS from " + fromNumber);
        return true;
      }
      return false;
    }
    return false;
  }
  
  String tok = extractKeyVal(payload, "TOK");
  if (tok.length() && tok == sysConfig.sharedTok) return true;
  
  if (src == "BT") {
    String t2 = extractKeyVal(payload, "TOK_BT");
    if (t2.length() && t2 == prefs.getString("tok_bt", "")) return true;
  }
  if (src == "LORA") {
    String t2 = extractKeyVal(payload, "TOK_LORA");
    if (t2.length() && t2 == prefs.getString("tok_lora", "")) return true;
  }
  if (src == "MQTT") {
    String t2 = extractKeyVal(payload, "TOK_MQ");
    if (t2.length() && t2 == prefs.getString("tok_mq", "")) return true;
  }
  
  return false;
}

// ========== Time Utilities ==========
String nowISO8601() {
  struct tm timeinfo;
  time_t t = time(nullptr);
  gmtime_r(&t, &timeinfo);
  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
  return String(buf);
}

String formatTimeShort() {
  time_t now = time(nullptr);
  struct tm tmnow;
  localtime_r(&now, &tmnow);
  char buf[6];
  snprintf(buf, sizeof(buf), "%02d:%02d", tmnow.tm_hour, tmnow.tm_min);
  return String(buf);
}

bool parseTimeHHMM(const String &t, int &hour, int &minute) {
  hour = 0;
  minute = 0;
  int res = sscanf(t.c_str(), "%d:%d", &hour, &minute);
  return res == 2;
}

time_t nextWeekdayOccurrence(time_t now, uint8_t weekday_mask, int hour, int minute) {
  struct tm tmnow;
  localtime_r(&now, &tmnow);
  int today = tmnow.tm_wday;
  
  for (int d = 0; d < 14; ++d) {
    int day = (today + d) % 7;
    if (weekday_mask & (1 << day)) {
      struct tm tmCandidate = tmnow;
      tmCandidate.tm_mday += d;
      tmCandidate.tm_hour = hour;
      tmCandidate.tm_min = minute;
      tmCandidate.tm_sec = 0;
      time_t cand = mktime(&tmCandidate);
      if (cand > now) return cand;
    }
  }
  return 0;
}

// ========== Message ID ==========
uint32_t getNextMsgId() {
  uint32_t mid = prefs.getUInt("msg_counter", 0);
  mid++;
  prefs.putUInt("msg_counter", mid);
  return mid;
}

// ========== Debugging ==========
void debugPrint(const String &s) {
  Serial.println(s);
}

// ========== Power Control (Heltec) ==========
void VextON() {
  pinMode(Vext, OUTPUT);
  digitalWrite(Vext, LOW);
}

void VextOFF() {
  pinMode(Vext, OUTPUT);
  digitalWrite(Vext, HIGH);
}