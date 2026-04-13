// StorageManager.h
#ifndef STORAGE_MANAGER_H
#define STORAGE_MANAGER_H

#include <LittleFS.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include "Config.h"
#include "CommConfig.h"

class StorageManager {
public:
  StorageManager();
  bool init();
  
  // File operations
  bool saveString(const String &path, const String &content);
  String loadString(const String &path);
  bool fileExists(const String &path);
  bool deleteFile(const String &path);
  
  // Schedule operations
  bool saveSchedule(const Schedule &s);
  bool deleteSchedule(const String &id);
  void loadAllSchedules(std::vector<Schedule> &schedules);
  Schedule scheduleFromJson(const String &json);
  
  // System config operations
  void loadSystemConfig(SystemConfig &config);
  void saveSystemConfig(const SystemConfig &config);

  // Comm config — runtime channel/credential config stored in LittleFS
  void loadCommConfig (CommConfig &cfg);
  void saveCommConfig (const CommConfig &cfg);
  void resetCommConfig(CommConfig &cfg);
};

extern StorageManager storage;

#endif