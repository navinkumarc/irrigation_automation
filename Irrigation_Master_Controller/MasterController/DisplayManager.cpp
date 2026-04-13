// DisplayManager.cpp
#include "DisplayManager.h"
#include "heltec.h"
#include "Config.h"

// External references to global variables from main .ino file
extern bool scheduleRunning;
extern String currentScheduleId;
extern std::vector<SeqStep> seq;
extern int currentStepIndex;
extern unsigned long stepStartMillis;

DisplayManager::DisplayManager() : display(nullptr), lastUpdate(0), lastScheduleId(""), lastRunningState(false), lastNodeId(-1) {}

bool DisplayManager::init() {
  // Turn on Vext for OLED power
  VextON();
  delay(50);
  
  // Create display object (0x3c address, 500kHz I2C, using Heltec default pins)
  display = new SSD1306Wire(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED);
  
  if (!display) {
    Serial.println("❌ Display creation failed");
    return false;
  }
  
  display->init();
  display->setFont(ArialMT_Plain_10);
  display->clear();
  display->setTextAlignment(TEXT_ALIGN_LEFT);
  
  // Show boot screen
  display->drawString(0, 0, "Irrigation");
  display->drawString(0, 12, "Controller");
  display->drawString(0, 26, "Booting...");
  display->display();
  
  Serial.println("✓ Display initialized");
  return true;
}

void DisplayManager::update() {
  unsigned long nowMs = millis();
  
  // Refresh only at specified interval
  if (nowMs - lastUpdate < DISPLAY_REFRESH_MS) {
    return;
  }
  
  lastUpdate = nowMs;
  
  if (!display) return;
  
  display->clear();
  display->setFont(ArialMT_Plain_10);
  display->setTextAlignment(TEXT_ALIGN_LEFT);
  
  // Line 1: Title
  display->drawString(0, 0, "Irrigation");
  
  // Line 2: Time and Status
  String timeLine = "Time:" + formatTimeShort() + " " + (scheduleRunning ? "RUN" : "IDLE");
  display->drawString(0, 12, timeLine);
  
  // Line 3: Schedule ID
  String schedLine = "SCH:" + (currentScheduleId.length() ? currentScheduleId : "NONE");
  if (schedLine.length() > 21) {
    schedLine = schedLine.substring(0, 21);
  }
  display->drawString(0, 26, schedLine);
  
  // Line 4: Current Node
  String nodeLine = "Node:";
  if (currentStepIndex >= 0 && currentStepIndex < (int)seq.size()) {
    nodeLine += String(seq[currentStepIndex].node_id);
    
    // Show remaining time if running
    if (scheduleRunning) {
      unsigned long elapsed = millis() - stepStartMillis;
      unsigned long remaining = 0;
      if (seq[currentStepIndex].duration_ms > elapsed) {
        remaining = (seq[currentStepIndex].duration_ms - elapsed) / 1000;
      }
      nodeLine += " (" + String(remaining) + "s)";
    }
  } else {
    nodeLine += "N/A";
  }
  display->drawString(0, 40, nodeLine);
  
  // Line 5: Connection status indicators
  String connLine = "";
  #if ENABLE_LORA
  connLine += "L";  // LoRa
  #endif
  #if ENABLE_BLE
  connLine += "B";  // BLE
  #endif
  #if ENABLE_MODEM
  connLine += "M";  // Modem
  #endif
  if (connLine.length() > 0) {
    display->drawString(0, 52, "Conn:" + connLine);
  }
  
  display->display();
}

void DisplayManager::showStatus(const String &schedId, bool running, int nodeId) {
  lastScheduleId = schedId;
  lastRunningState = running;
  lastNodeId = nodeId;
  
  // Force immediate update
  lastUpdate = 0;
  update();
}

void DisplayManager::showMessage(const String &line1, const String &line2, const String &line3, const String &line4) {
  if (!display) return;
  
  display->clear();
  display->setFont(ArialMT_Plain_10);
  display->setTextAlignment(TEXT_ALIGN_LEFT);
  
  if (line1.length()) display->drawString(0, 0, line1);
  if (line2.length()) display->drawString(0, 15, line2);
  if (line3.length()) display->drawString(0, 30, line3);
  if (line4.length()) display->drawString(0, 45, line4);
  
  display->display();
}
