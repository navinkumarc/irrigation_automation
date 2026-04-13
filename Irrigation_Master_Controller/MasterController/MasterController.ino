// MasterController.ino - Main sketch
// Communication modules are initialized and polled by CommSetup.
// UserCommunication is driven only by CommSetup via ChannelMessage.
// This file only calls application-level helpers.

#include "Config.h"
#include "Utils.h"
#include "MessageQueue.h"
#include "StorageManager.h"
#include "TimeManager.h"
#include "ScheduleManager.h"
#include "NodeCommunication.h"
#include "UserCommunication.h"
#include "CommSetup.h"
#include "NetworkRouter.h"
#include "MessageFormats.h"

#if ENABLE_DISPLAY
  #include "DisplayManager.h"
#endif
#if ENABLE_LORA
  #include "LoRaComm.h"
#endif
#if ENABLE_BLE
  #include "BLEComm.h"
#endif
#if ENABLE_WIFI
  #include "WiFiComm.h"
#endif
#if ENABLE_MODEM
  #include "ModemBase.h"
#endif
#if ENABLE_SMS
  #include "ModemSMS.h"
#endif
#if ENABLE_PPPOS
  #include "ModemPPPoS.h"
#endif
#if ENABLE_MQTT
  #include "MQTTComm.h"
#endif
#if ENABLE_HTTP
  #include "HTTPComm.h"
#endif

// ─── Global variable definitions ──────────────────────────────────────────────
SystemConfig          sysConfig;
std::vector<Schedule> schedules;
String                currentScheduleId     = "";
std::vector<SeqStep>  seq;
int                   currentStepIndex      = -1;
unsigned long         stepStartMillis       = 0;
bool                  scheduleLoaded        = false;
bool                  scheduleRunning       = false;
time_t                scheduleStartEpoch    = 0;
uint32_t              pumpOnBeforeMs        = PUMP_ON_LEAD_DEFAULT_MS;
uint32_t              pumpOffAfterMs        = PUMP_OFF_DELAY_DEFAULT_MS;
uint32_t              LAST_CLOSE_DELAY_MS   = LAST_CLOSE_DELAY_MS_DEFAULT;
uint32_t              DRIFT_THRESHOLD_S     = 300;
uint32_t              SYNC_CHECK_INTERVAL_MS= 3600000UL;
bool                  ENABLE_SMS_BROADCAST  = true;

// ─── Module instances ─────────────────────────────────────────────────────────
Preferences       prefs;
MessageQueue      incomingQueue;
StorageManager    storage;
TimeManager       timeManager;
ScheduleManager   scheduleMgr;
NodeCommunication nodeComm;
UserCommunication userComm;
CommSetup         commSetup;
NetworkRouter     networkRouter;

#if ENABLE_DISPLAY
DisplayManager    displayMgr;
#endif
#if ENABLE_LORA
LoRaComm          loraComm;
bool              loraInitialized = false;
#else
bool              loraInitialized = false;
#endif
#if ENABLE_BLE
BLEComm           bleComm;
#endif
#if ENABLE_WIFI
WiFiComm          wifiComm;
#endif
// modemBase defined in ModemBase.cpp
#if ENABLE_SMS
// modemSMS defined in ModemSMS.cpp
#endif
#if ENABLE_PPPOS
// modemPPPoS defined in ModemPPPoS.cpp — needs global instance
ModemPPPoS        modemPPPoS;
#endif
#if ENABLE_MQTT
MQTTComm          mqtt;
#endif
#if ENABLE_HTTP
HTTPComm          httpComm;
#endif

#if ENABLE_RTC
  #include <RTClib.h>
  TwoWire    WireRTC     = TwoWire(1);
  RTC_DS3231 rtc;
  bool       rtcAvailable = false;
#endif

CommSetupStatus commStatus;

// ─── Callbacks ────────────────────────────────────────────────────────────────

// BLE raw command → delivered via CommSetup BLE channel processor
void handleBLECommand(int node, String command) {
  Serial.printf("[MAIN] BLE cmd: node=%d cmd=%s\n", node, command.c_str());
  // Build a ChannelMessage and deliver to userComm
  SystemStatus sys = commSetup.buildSystemStatus(&schedules, scheduleRunning);
  auto reply = [](const String &r) {
#if ENABLE_BLE
    bleComm.notify(r);
#endif
  };
  ChannelMessage msg(command, "BLE_NODE_" + String(node), "BLE", reply);
  userComm.onMessageReceived(msg, &scheduleRunning, &scheduleLoaded, sys);
}

bool handleNodeCommand(int nodeId, const String &command) {
  Serial.printf("[MAIN] Node cmd: id=%d cmd=%s\n", nodeId, command.c_str());
  nodeComm.sendCommand(nodeId, command);
  return true;
}

// ─── Setup ────────────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(1000);

  Serial.println("\n==========================================");
  Serial.println("  IRRIGATION CONTROLLER v3.0");
  Serial.println("==========================================\n");

  Serial.println("[1/5] Storage...");
  if (storage.init()) Serial.println("      ✓ Storage ready");

  Serial.println("[2/5] Preferences...");
  prefs.begin("irrig", false);
  Serial.println("      ✓ Preferences ready");

  Serial.println("[3/5] Configuration...");
  storage.loadSystemConfig(sysConfig);
  storage.loadAllSchedules(schedules);
  Serial.println("      ✓ Config loaded (" + String(schedules.size()) + " schedules)");

#if ENABLE_DISPLAY
  Serial.println("[4/5] Display...");
  if (displayMgr.init()) {
    displayMgr.showMessage("Irrigation", "v3.0", "Init...", "");
    Serial.println("      ✓ Display ready");
  }
#else
  Serial.println("[4/5] Display: disabled");
#endif

#if ENABLE_RTC
  Serial.println("[5/5] RTC...");
  WireRTC.begin(RTC_SDA, RTC_SCL);
  if (rtc.begin(&WireRTC)) {
    rtcAvailable = true;
    Serial.println("      ✓ RTC ready");
  } else {
    Serial.println("      ⚠ RTC not found");
  }
#else
  Serial.println("[5/5] RTC: disabled");
#endif

  // ── Communication modules ─────────────────────────────────────────────────
  Serial.println("\n[SETUP] Initializing communication modules...");
  commStatus = commSetup.initializeAll();

  if (commStatus.successfulModules < commStatus.totalModules) {
    Serial.printf("⚠ %d/%d modules initialized\n",
      commStatus.successfulModules, commStatus.totalModules);
  }

  // ── BLE command callback ──────────────────────────────────────────────────
#if ENABLE_BLE
  bleComm.setCommandCallback(handleBLECommand);
  Serial.println("[SETUP] ✓ BLE callback registered");
#endif

#if ENABLE_LORA
  loraInitialized = commStatus.loraOk;
#endif

  // ── Application wiring ────────────────────────────────────────────────────
  userComm.setNodeCommandCallback(handleNodeCommand);
  scheduleMgr.init(&userComm);

#if ENABLE_LORA
  nodeComm.init(&loraComm);
#endif

  Serial.println("\n==========================================");
  Serial.println("✓ SYSTEM READY");
  Serial.println("==========================================\n");

  // Build initial status and print
  SystemStatus initSys = commSetup.buildSystemStatus(&schedules, scheduleRunning);
  userComm.printBriefStatus(initSys);
}

// ─── Main Loop ────────────────────────────────────────────────────────────────

void loop() {
  // CommSetup drives all channel polling, background work, and
  // delivers ChannelMessages to UserCommunication
  commSetup.processChannels(&schedules, &scheduleRunning, &scheduleLoaded);

  // LoRa node communication
#if ENABLE_LORA
  loraComm.processIncoming();
  nodeComm.processIncoming();
#endif

  // BLE liveness check
#if ENABLE_BLE
  static unsigned long lastBLECheck = 0;
  if (millis() - lastBLECheck > 30000) {
    lastBLECheck = millis();
    if (bleComm.isConnected()) Serial.println("[MAIN] BLE connected");
  }
#endif

  // Inbound message queue (LoRa / general)
  String qMsg;
  if (incomingQueue.dequeue(qMsg)) {
    Serial.println("[MAIN] Queue: " + qMsg);
  }

  // Periodic health check (every 60 s)
  static unsigned long lastHealth = 0;
  if (millis() - lastHealth > 60000) {
    lastHealth = millis();
    if (!userComm.isSystemHealthy()) {
      userComm.sendAlert(userComm.getHealthStatus(), "WARNING");
    }
  }

  // Periodic brief status to Serial (every 5 min)
  static unsigned long lastStatus = 0;
  if (millis() - lastStatus > 300000) {
    lastStatus = millis();
    SystemStatus sys = commSetup.buildSystemStatus(&schedules, scheduleRunning);
    userComm.printBriefStatus(sys);
  }

#if ENABLE_DISPLAY
  displayMgr.update();
#endif

  vTaskDelay(pdMS_TO_TICKS(10));
}

// ─── Application helpers ──────────────────────────────────────────────────────

SystemStatus getCurrentStatus() {
  return commSetup.buildSystemStatus(&schedules, scheduleRunning);
}

void printFullSystemDiagnostics() {
  Serial.println("\n==========================================");
  Serial.println("  FULL SYSTEM DIAGNOSTIC");
  Serial.println("==========================================\n");
  SystemStatus sys = getCurrentStatus();
  userComm.printSystemStatus(sys);
  userComm.printSystemDiagnostics();
  commSetup.printStatus();
  Serial.println("==========================================\n");
}

String getSystemStatusJSON() {
  return userComm.getStatusJSON(getCurrentStatus());
}

void startSchedule(String scheduleId) {
  currentScheduleId = scheduleId;
  scheduleRunning   = true;
  userComm.onScheduleStarted(scheduleId);
}

void stopAllSchedules() {
  scheduleRunning = false;
  if (currentScheduleId.length() > 0)
    userComm.onScheduleCompleted(currentScheduleId);
}

void notifyValveAction  (int nodeId, String valve, String action) { userComm.onValveAction(nodeId, valve, action); }
void notifySystemError  (String msg) { userComm.onSystemError(msg);   }
void notifySystemWarning(String msg) { userComm.onSystemWarning(msg); }
