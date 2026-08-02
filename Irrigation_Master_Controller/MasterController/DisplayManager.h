// DisplayManager.h
#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include "HT_SSD1306Wire.h"
#include "Config.h"
#include "Utils.h"
#include "PowerMonitor.h"

class DisplayManager {
private:
  SSD1306Wire *display;
  unsigned long lastUpdate;
  String lastScheduleId;
  bool lastRunningState;
  int lastNodeId;
  PowerMonitor *_power = nullptr;

  // Battery icon drawing helpers
  void drawBatteryIcon(int pct, bool onUSB);

public:
  DisplayManager();
  bool init();
  void update();
  void showStatus(const String &schedId, bool running, int nodeId);
  void showMessage(const String &line1, const String &line2 = "", const String &line3 = "", const String &line4 = "");
  void setPowerMonitor(PowerMonitor *pm) { _power = pm; }
};

extern DisplayManager displayMgr;

#endif