// StorageManager.cpp
#include "StorageManager.h"

// Add extern declaration
extern Preferences prefs;

StorageManager::StorageManager() {}

bool StorageManager::init() {
  if (!LittleFS.begin(true)) {
    Serial.println("❌ LittleFS mount failed");
    return false;
  }
  Serial.println("✓ LittleFS mounted");
  
  if (!LittleFS.exists("/schedules")) {
    LittleFS.mkdir("/schedules");
    Serial.println("✓ Created /schedules directory");
  }
  
  return true;
}

bool StorageManager::saveString(const String &path, const String &content) {
  File f = LittleFS.open(path, "w");
  if (!f) {
    Serial.println("❌ Failed to open file for writing: " + path);
    return false;
  }
  f.print(content);
  f.close();
  return true;
}

String StorageManager::loadString(const String &path) {
  if (!LittleFS.exists(path)) {
    return String("");
  }
  File f = LittleFS.open(path, "r");
  if (!f) {
    return String("");
  }
  String content = f.readString();
  f.close();
  return content;
}

bool StorageManager::fileExists(const String &path) {
  return LittleFS.exists(path);
}

bool StorageManager::deleteFile(const String &path) {
  if (LittleFS.exists(path)) {
    return LittleFS.remove(path);
  }
  return true;
}

bool StorageManager::saveSchedule(const Schedule &s) {
  DynamicJsonDocument doc(4096);
  
  doc["schedule_id"] = s.id;
  doc["recurrence"] = (s.rec == 'D' ? "daily" : (s.rec == 'W' ? "weekly" : "onetime"));
  doc["start_time"] = s.timeStr;
  doc["start_epoch"] = (long long)s.start_epoch;
  doc["pump_on_before_ms"] = s.pump_on_before_ms;
  doc["pump_off_after_ms"] = s.pump_off_after_ms;
  doc["enabled"] = s.enabled;
  doc["next_run_epoch"] = (long long)s.next_run_epoch;
  doc["ts"] = s.ts;
  doc["weekday_mask"] = s.weekday_mask;
  
  JsonArray arr = doc.createNestedArray("sequence");
  for (auto &st : s.seq) {
    JsonObject so = arr.createNestedObject();
    so["node_id"] = st.node_id;
    so["duration_ms"] = st.duration_ms;
  }
  
  String output;
  serializeJson(doc, output);
  
  String path = String("/schedules/") + s.id + String(".json");
  return saveString(path, output);
}

bool StorageManager::deleteSchedule(const String &id) {
  String path = String("/schedules/") + id + String(".json");
  return deleteFile(path);
}

Schedule StorageManager::scheduleFromJson(const String &json) {
  Schedule s;
  s.seq.clear();
  s.id = "";
  s.rec = 'O';
  s.start_epoch = 0;
  s.timeStr = "";
  s.weekday_mask = 0;
  s.pump_on_before_ms = PUMP_ON_LEAD_DEFAULT_MS;
  s.pump_off_after_ms = PUMP_OFF_DELAY_DEFAULT_MS;
  s.enabled = true;
  s.next_run_epoch = 0;
  s.ts = 0;
  
  StaticJsonDocument<4096> doc;
  DeserializationError err = deserializeJson(doc, json);
  if (err) {
    Serial.printf("❌ JSON parse error: %s\n", err.c_str());
    return s;
  }
  
  s.id = String((const char *)(doc["schedule_id"] | doc["id"] | ""));
  String recurrence = String((const char *)(doc["recurrence"] | doc["rec"] | ""));
  
  if (recurrence.startsWith("d") || recurrence.startsWith("D")) s.rec = 'D';
  else if (recurrence.startsWith("w") || recurrence.startsWith("W")) s.rec = 'W';
  else s.rec = 'O';
  
  s.timeStr = String((const char *)(doc["start_time"] | doc["time"] | ""));
  s.start_epoch = (time_t)(doc["start_epoch"].as<long long>() ? doc["start_epoch"].as<long long>() : 0);
  s.pump_on_before_ms = doc["pump_on_before_ms"] | PUMP_ON_LEAD_DEFAULT_MS;
  s.pump_off_after_ms = doc["pump_off_after_ms"] | PUMP_OFF_DELAY_DEFAULT_MS;
  s.enabled = doc["enabled"] | true;
  s.next_run_epoch = doc["next_run_epoch"] | 0;
  s.ts = doc["ts"] | 0;
  s.weekday_mask = doc["weekday_mask"] | 0;
  
  if (doc.containsKey("sequence") && doc["sequence"].is<JsonArray>()) {
    for (JsonVariant v : doc["sequence"].as<JsonArray>()) {
      SeqStep st;
      st.node_id = v["node_id"].as<int>();
      st.duration_ms = v["duration_ms"].as<uint32_t>();
      s.seq.push_back(st);
    }
  }
  
  return s;
}

void StorageManager::loadAllSchedules(std::vector<Schedule> &schedules) {
  schedules.clear();

  if (!LittleFS.exists("/schedules")) {
    LittleFS.mkdir("/schedules");
    return;
  }

  File root = LittleFS.open("/schedules");
  if (!root) {
    Serial.println("❌ Failed to open schedules directory");
    return;
  }

  File file = root.openNextFile();

  while (file) {
    String name = file.name();
    if (name.endsWith(".json")) {
      String content = file.readString();
      Schedule s = scheduleFromJson(content);
      if (s.id.length() > 0) {
        schedules.push_back(s);
        Serial.println("✓ Loaded schedule: " + s.id);
      }
    }
    file.close();  // Close individual file
    file = root.openNextFile();
  }

  root.close();  // Close directory handle - FIX FOR FILE LEAK

  Serial.printf("✓ Loaded %d schedules\n", schedules.size());
}

void StorageManager::loadSystemConfig(SystemConfig &config) {
  config.mqttServer = prefs.getString("mqtt_server", DEFAULT_MQTT_SERVER);
  config.mqttPort = prefs.getInt("mqtt_port", DEFAULT_MQTT_PORT);
  config.mqttUser = prefs.getString("mqtt_user", DEFAULT_MQTT_USER);
  config.mqttPass = prefs.getString("mqtt_pass", DEFAULT_MQTT_PASS);
  config.adminPhones = prefs.getString("admin_phones", DEFAULT_ADMIN_PHONE);
  config.simApn = prefs.getString("sim_apn", DEFAULT_SIM_APN);
  config.sharedTok = prefs.getString("shared_tok", "MYTOK");
  config.recoveryTok = prefs.getString("recovery_tok", DEFAULT_RECOV_TOK);
  
  LAST_CLOSE_DELAY_MS = prefs.getULong("last_close_delay_ms", LAST_CLOSE_DELAY_MS_DEFAULT);
  DRIFT_THRESHOLD_S = prefs.getUInt("drift_s", 300);
  uint32_t sync_h = prefs.getUInt("sync_h", 1);
  SYNC_CHECK_INTERVAL_MS = sync_h * 3600UL * 1000UL;
  
  Serial.println("✓ Loaded system config");
}

void StorageManager::saveSystemConfig(const SystemConfig &config) {
  prefs.putString("mqtt_server", config.mqttServer);
  prefs.putInt("mqtt_port", config.mqttPort);
  prefs.putString("mqtt_user", config.mqttUser);
  prefs.putString("mqtt_pass", config.mqttPass);
  prefs.putString("admin_phones", config.adminPhones);
  prefs.putString("sim_apn", config.simApn);
  prefs.putString("shared_tok", config.sharedTok);
  prefs.putString("recovery_tok", config.recoveryTok);
  
  prefs.putULong("last_close_delay_ms", LAST_CLOSE_DELAY_MS);
  prefs.putUInt("drift_s", DRIFT_THRESHOLD_S);
  prefs.putUInt("sync_h", (uint32_t)(SYNC_CHECK_INTERVAL_MS / 3600000UL));
  
  Serial.println("✓ Saved system config");
}