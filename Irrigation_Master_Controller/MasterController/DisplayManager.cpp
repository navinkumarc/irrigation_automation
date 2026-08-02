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


// ─── drawBatteryIcon() ────────────────────────────────────────────────────────
// Draws a mobile-style battery icon in the top-right corner of the 128×64 display.
//
// Layout (top-right, starting at x=101, y=0):
//
//   ┌──────────────┐╗
//   │████████░░░░░░│║  ← battery body 20×8px, tip 2×4px
//   └──────────────┘╝
//
//   Fill width proportional to percent (0-100%).
//   Percentage text drawn to the left of the icon.
//   Charging bolt drawn inside body when USB connected.
//
// Screen: 128×64 pixels.  Top-right = x:101..127, y:0..8
//
//   x=101  ←  percentage text (right-aligned)
//   x=104  ←  battery body left edge
//   x=123  ←  battery body right edge  (19px wide)
//   x=124  ←  tip left edge
//   x=126  ←  tip right edge
//   y=1    ←  body top
//   y=7    ←  body bottom  (6px tall)
//   y=3    ←  tip top
//   y=5    ←  tip bottom

void DisplayManager::drawBatteryIcon(int pct, bool onUSB) {
  if (!display) return;

  // ── Dimensions ──────────────────────────────────────────────────────────
  const int BX   = 104;  // body left
  const int BY   = 1;    // body top
  const int BW   = 19;   // body width
  const int BH   = 7;    // body height
  const int TW   = 3;    // tip width
  const int TH   = 3;    // tip height

  // ── Battery outline ──────────────────────────────────────────────────────
  display->drawRect(BX, BY, BW, BH);          // outer body
  display->drawRect(BX + BW, BY + 2, TW, TH); // positive terminal tip

  // ── Fill level ───────────────────────────────────────────────────────────
  // Inner fill area: 1px inset from outline
  int maxFill   = BW - 2;                      // 17px maximum fill
  int fillWidth = (pct * maxFill) / 100;
  if (fillWidth > maxFill) fillWidth = maxFill;

  if (fillWidth > 0) {
    // Colour the fill: solid when good, sparse when low
    if (pct > 20) {
      // Solid fill
      display->fillRect(BX + 1, BY + 1, fillWidth, BH - 2);
    } else {
      // Low battery — striped fill (every other column)
      for (int x = 0; x < fillWidth; x += 2)
        display->drawLine(BX + 1 + x, BY + 1, BX + 1 + x, BY + BH - 2);
    }
  }

  // ── Charging bolt (⚡) when on USB ────────────────────────────────────────
  if (onUSB) {
    // Draw a lightning bolt inside the battery body using lines
    // Bolt: diagonal line top-right → centre, then centre → bottom-left
    int cx = BX + BW / 2;
    int cy = BY + BH / 2;
    // Top segment: (cx+2, BY+1) → (cx-1, cy)
    display->drawLine(cx + 2, BY + 1, cx - 1, cy);
    // Bottom segment: (cx - 1, cy) → (cx - 3, BY + BH - 2)
    display->drawLine(cx - 1, cy, cx - 3, BY + BH - 2);
    // Short horizontal tip at midpoint to complete bolt look
    display->drawLine(cx - 1, cy, cx + 1, cy);
  }

  // ── Percentage text (right-aligned, left of icon) ─────────────────────────
  String pctStr = String(pct) + "%";
  display->setTextAlignment(TEXT_ALIGN_RIGHT);
  display->setFont(ArialMT_Plain_10);
  display->drawString(BX - 2, BY - 1, pctStr);
  display->setTextAlignment(TEXT_ALIGN_LEFT);   // restore default
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

  // ── Battery icon — top right corner ───────────────────────────────
  if (_power) {
    drawBatteryIcon(_power->percent(), _power->isOnUSB());
  }

  // Line 1: Title (leave right side clear for battery icon)
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
