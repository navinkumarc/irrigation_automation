// StorageManager.cpp
#include "StorageManager.h"
#include "ProcessConfig.h"
#include <ArduinoJson.h>

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
    so["node_id"]    = st.node_id;
    so["valve_id"]   = st.valve_id;
    so["duration_ms"]= st.duration_ms;
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
      st.node_id    = v["node_id"].as<int>();
      st.valve_id   = v["valve_id"].as<int>();
      st.duration_ms= v["duration_ms"].as<uint32_t>();
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
// ─── CommConfig persistence ───────────────────────────────────────────────────
// CommConfig is stored as a single JSON file at /commconfig.json in LittleFS.
// This keeps all comm settings in one readable, inspectable file alongside
// the schedule files, under the same StorageManager ownership model.

static const char* COMM_CONFIG_FILE = "/commconfig.json";

void StorageManager::loadCommConfig(CommConfig &cfg) {
  if (!LittleFS.exists(COMM_CONFIG_FILE)) {
    Serial.println("[Storage] No commconfig.json — using firmware defaults");
    return;
  }
  String json = loadString(COMM_CONFIG_FILE);
  if (json.length() == 0) {
    Serial.println("[Storage] commconfig.json empty — using firmware defaults");
    return;
  }
  DynamicJsonDocument doc(2048);
  if (deserializeJson(doc, json) != DeserializationError::Ok) {
    Serial.println("[Storage] \u274c commconfig.json parse error — using firmware defaults");
    return;
  }
  if (doc.containsKey("activeChannel"))
    cfg.activeChannel = (ActiveChannel)doc["activeChannel"].as<uint8_t>();
  if (doc.containsKey("chBluetooth"))  cfg.chBluetooth   = doc["chBluetooth"].as<bool>();
  if (doc.containsKey("chLoRa"))       cfg.chLoRa        = doc["chLoRa"].as<bool>();
  if (doc.containsKey("enablePPPoS"))  cfg.enablePPPoS   = doc["enablePPPoS"].as<bool>();
  if (doc.containsKey("enableWiFi"))   cfg.enableWiFi    = doc["enableWiFi"].as<bool>();
  if (doc.containsKey("wifiSSID"))     cfg.wifiSSID      = doc["wifiSSID"].as<String>();
  if (doc.containsKey("wifiPass"))     cfg.wifiPass      = doc["wifiPass"].as<String>();
  if (doc.containsKey("apn"))          cfg.cellularAPN   = doc["apn"].as<String>();
  if (doc.containsKey("mqttBroker"))   cfg.mqttBroker    = doc["mqttBroker"].as<String>();
  if (doc.containsKey("mqttPort"))     cfg.mqttPort      = doc["mqttPort"].as<uint16_t>();
  if (doc.containsKey("mqttUser"))     cfg.mqttUser      = doc["mqttUser"].as<String>();
  if (doc.containsKey("mqttPass"))     cfg.mqttPass      = doc["mqttPass"].as<String>();
  if (doc.containsKey("mqttClientId")) cfg.mqttClientId  = doc["mqttClientId"].as<String>();
  if (doc.containsKey("mqttTLS"))      cfg.mqttTLS       = doc["mqttTLS"].as<bool>();
  if (doc.containsKey("smsPhone1"))    cfg.smsPhone1     = doc["smsPhone1"].as<String>();
  if (doc.containsKey("smsPhone2"))    cfg.smsPhone2     = doc["smsPhone2"].as<String>();
  if (doc.containsKey("bleName"))      cfg.bleName       = doc["bleName"].as<String>();
  if (doc.containsKey("loraFreq"))     cfg.loraFrequencyHz = doc["loraFreq"].as<uint32_t>();
  if (doc.containsKey("httpPort"))     cfg.httpPort      = doc["httpPort"].as<uint16_t>();
  Serial.println("[Storage] \u2713 CommConfig loaded from /commconfig.json");
}

void StorageManager::saveCommConfig(const CommConfig &cfg) {
  DynamicJsonDocument doc(2048);
  doc["activeChannel"] = (uint8_t)cfg.activeChannel;
  doc["chBluetooth"]   = cfg.chBluetooth;
  doc["chLoRa"]        = cfg.chLoRa;
  doc["enablePPPoS"]   = cfg.enablePPPoS;
  doc["enableWiFi"]    = cfg.enableWiFi;
  doc["wifiSSID"]      = cfg.wifiSSID;
  doc["wifiPass"]      = cfg.wifiPass;
  doc["apn"]           = cfg.cellularAPN;
  doc["mqttBroker"]    = cfg.mqttBroker;
  doc["mqttPort"]      = cfg.mqttPort;
  doc["mqttUser"]      = cfg.mqttUser;
  doc["mqttPass"]      = cfg.mqttPass;
  doc["mqttClientId"]  = cfg.mqttClientId;
  doc["mqttTLS"]       = cfg.mqttTLS;
  doc["smsPhone1"]     = cfg.smsPhone1;
  doc["smsPhone2"]     = cfg.smsPhone2;
  doc["bleName"]       = cfg.bleName;
  doc["loraFreq"]      = cfg.loraFrequencyHz;
  doc["httpPort"]      = cfg.httpPort;
  String json;
  serializeJsonPretty(doc, json);
  if (saveString(COMM_CONFIG_FILE, json))
    Serial.println("[Storage] \u2713 CommConfig saved to /commconfig.json");
  else
    Serial.println("[Storage] \u274c Failed to save CommConfig");
}

void StorageManager::resetCommConfig(CommConfig &cfg) {
  cfg = CommConfig();   // rebuild from Config.h defaults
  saveCommConfig(cfg);
  Serial.println("[Storage] ✓ CommConfig reset to firmware defaults");
}

// ─── Process group config ─────────────────────────────────────────────────────

bool StorageManager::saveWTTConfig(const WTTGroupConfig &cfg) {
  if (!LittleFS.exists("/process")) LittleFS.mkdir("/process");
  StaticJsonDocument<256> doc;
  doc["id"]    = cfg.id;
  doc["pump"]  = cfg.pumpId;
  doc["tank"]  = cfg.tankId;
  doc["type"]  = "WTT";
  String json; serializeJson(doc, json);
  String path = "/process/wtt_" + cfg.id + ".json";
  return saveString(path, json);
}

bool StorageManager::saveIrrConfig(const IrrGroupConfig &cfg) {
  if (!LittleFS.exists("/process")) LittleFS.mkdir("/process");
  StaticJsonDocument<256> doc;
  doc["id"]        = cfg.id;
  doc["pump"]      = cfg.pumpId;
  doc["minValves"] = cfg.minValves;
  doc["maxNodes"]  = cfg.maxNodes;
  doc["maxValves"] = cfg.maxValves;
  doc["type"]      = "IRR";
  String json; serializeJson(doc, json);
  String path = "/process/irr_" + cfg.id + ".json";
  return saveString(path, json);
}

bool StorageManager::deleteProcessConfig(const String &id) {
  bool ok = false;
  String p1 = "/process/wtt_" + id + ".json";
  String p2 = "/process/irr_" + id + ".json";
  if (LittleFS.exists(p1)) { LittleFS.remove(p1); ok = true; }
  if (LittleFS.exists(p2)) { LittleFS.remove(p2); ok = true; }
  return ok;
}

void StorageManager::loadWTTConfigs(WTTGroupConfig out[], int maxCount, int &count) {
  count = 0;
  if (!LittleFS.exists("/process")) return;
  File root = LittleFS.open("/process");
  if (!root) return;
  File f = root.openNextFile();
  while (f && count < maxCount) {
    String name = f.name();
    if (name.indexOf("wtt_") >= 0 && name.endsWith(".json")) {
      StaticJsonDocument<256> doc;
      if (deserializeJson(doc, f) == DeserializationError::Ok) {
        out[count].id         = doc["id"]   | "";
        out[count].pumpId     = doc["pump"] | "";
        out[count].tankId     = doc["tank"] | "";
        out[count].configured = out[count].isValid();
        if (out[count].configured) {
          Serial.println("[Storage] Loaded WTT config: " + out[count].id);
          count++;
        }
      }
    }
    f.close(); f = root.openNextFile();
  }
  root.close();
}

void StorageManager::loadIrrConfigs(IrrGroupConfig out[], int maxCount, int &count) {
  count = 0;
  if (!LittleFS.exists("/process")) return;
  File root = LittleFS.open("/process");
  if (!root) return;
  File f = root.openNextFile();
  while (f && count < maxCount) {
    String name = f.name();
    if (name.indexOf("irr_") >= 0 && name.endsWith(".json")) {
      StaticJsonDocument<256> doc;
      if (deserializeJson(doc, f) == DeserializationError::Ok) {
        out[count].id         = doc["id"]        | "";
        out[count].pumpId     = doc["pump"]      | "";
        out[count].minValves  = doc["minValves"] | 1;
        out[count].maxNodes   = doc["maxNodes"]  | 15;
        out[count].maxValves  = doc["maxValves"] | 4;
        out[count].configured = out[count].isValid();
        if (out[count].configured) {
          Serial.println("[Storage] Loaded IRR config: " + out[count].id);
          count++;
        }
      }
    }
    f.close(); f = root.openNextFile();
  }
  root.close();
}
