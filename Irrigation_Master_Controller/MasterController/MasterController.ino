// IrrigationController.ino - Main sketch
// All optional communication modules are guarded by their ENABLE_ flag in Config.h.
// To disable a module: set its flag to 0 (and optionally remove its .h/.cpp files).

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

// Optional module headers — only included when their flag is enabled.
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
#if ENABLE_MQTT
  #include "MQTTComm.h"
#endif
#if ENABLE_HTTP
  #include "HTTPComm.h"
#endif

// ─── Global Variable Definitions ─────────────────────────────────────────────
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
uint32_t              SYNC_CHECK_INTERVAL_MS = 3600000UL;
bool                  ENABLE_SMS_BROADCAST  = true;

// ─── Module Instances — compiled only when their flag is enabled ──────────────
Preferences      prefs;
MessageQueue     incomingQueue;
StorageManager   storage;
TimeManager      timeManager;
ScheduleManager  scheduleMgr;
NodeCommunication nodeComm;
UserCommunication userComm;
CommSetup        commSetup;

#if ENABLE_DISPLAY
DisplayManager   displayMgr;
#endif
#if ENABLE_LORA
LoRaComm         loraComm;
bool             loraInitialized = false;
#else
bool             loraInitialized = false;  // Always defined for UserCommunication
#endif
#if ENABLE_BLE
BLEComm          bleComm;
#endif
#if ENABLE_WIFI
WiFiComm         wifiComm;
#endif
// ModemBase instance is defined in ModemBase.cpp (extern modemBase)
#if ENABLE_SMS
// ModemSMS instance is defined in ModemSMS.cpp (extern modemSMS)
#endif
#if ENABLE_MQTT
MQTTComm         mqtt;
#endif
#if ENABLE_HTTP
HTTPComm         httpComm;
#endif

// RTC is always present when ENABLE_RTC is set
#if ENABLE_RTC
  #include <RTClib.h>
  TwoWire    WireRTC    = TwoWire(1);
  RTC_DS3231 rtc;
  bool       rtcAvailable = false;
#endif

CommSetupStatus commStatus;

// ─── Callbacks ────────────────────────────────────────────────────────────────

void handleBLECommand(int node, String command) {
  Serial.printf("[MAIN] BLE cmd: node=%d cmd=%s\n", node, command.c_str());
  userComm.processBLECommand(node, command);
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
  Serial.println("  IRRIGATION CONTROLLER v2.0");
  Serial.println("==========================================\n");

  // ── Core systems ────────────────────────────────────────────────────────────
  Serial.println("[1/5] Storage...");
  if (storage.init()) Serial.println("      ✓ Storage ready");

  Serial.println("[2/5] Preferences...");
  prefs.begin("irrig", false);
  Serial.println("      ✓ Preferences ready");

  Serial.println("[3/5] Configuration...");
  storage.loadSystemConfig(sysConfig);
  storage.loadAllSchedules(schedules);
  Serial.println("      ✓ Config loaded");

#if ENABLE_DISPLAY
  Serial.println("[4/5] Display...");
  if (displayMgr.init()) {
    displayMgr.showMessage("Irrigation", "v2.0", "Init...", "");
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

  // ── Communication modules ───────────────────────────────────────────────────
  // initializeAll() only calls init functions for modules whose ENABLE_ flag is set.
  Serial.println("\n[SETUP] Initializing communication modules...");
  commStatus = commSetup.initializeAll();

  if (commStatus.successfulModules < commStatus.totalModules) {
    Serial.printf("⚠ %d/%d modules initialized\n",
      commStatus.successfulModules, commStatus.totalModules);
  }

  // ── BLE callback ────────────────────────────────────────────────────────────
#if ENABLE_BLE
  bleComm.setCommandCallback(handleBLECommand);
  Serial.println("[SETUP] ✓ BLE callback registered");
#endif

  // ── LoRa initialized flag ───────────────────────────────────────────────────
#if ENABLE_LORA
  loraInitialized = commStatus.loraOk;
#endif

  // ── Application modules ─────────────────────────────────────────────────────
  userComm.setNodeCommandCallback(handleNodeCommand);
  scheduleMgr.init(&userComm);

#if ENABLE_LORA
  nodeComm.init(&loraComm);
#endif

  Serial.println("\n==========================================");
  Serial.println("✓ SYSTEM READY");
  Serial.println("==========================================\n");

  userComm.printBriefStatus(&schedules, scheduleRunning);
}

// ─── Main Loop ────────────────────────────────────────────────────────────────

void loop() {
  // Background tasks — each compiled only when its module is enabled.
#if ENABLE_WIFI
  wifiComm.processBackground();
#endif

  // NetworkRouter: feeds PPP stack + detects dropped bearers + auto-reconnect
  networkRouter.processBackground();

#if ENABLE_MQTT
  mqtt.processBackground();
#endif

#if ENABLE_HTTP
  httpComm.processBackground();
#endif

#if ENABLE_LORA
  loraComm.processIncoming();
  nodeComm.processIncoming();
#endif

#if ENABLE_BLE
  static unsigned long lastBLECheck = 0;
  if (millis() - lastBLECheck > 30000) {
    lastBLECheck = millis();
    if (bleComm.isConnected()) Serial.println("[MAIN] BLE connected");
  }
#endif

  // Message queue
  String msg;
  if (incomingQueue.dequeue(msg)) {
    Serial.println("[MAIN] Queue: " + msg);
  }

  // Process all enabled communication channels (SMS, MQTT, HTTP, BLE, LoRa).
  userComm.processAllChannels(&schedules, &scheduleRunning, &scheduleLoaded,
                              &ENABLE_SMS_BROADCAST);

  // Periodic health check (every 60 s)
  static unsigned long lastHealth = 0;
  if (millis() - lastHealth > 60000) {
    lastHealth = millis();
    if (!userComm.isSystemHealthy()) {
      userComm.sendAlert(userComm.getHealthStatus(), "WARNING");
    }
  }

  // Periodic status report (every 5 min)
  static unsigned long lastStatus = 0;
  if (millis() - lastStatus > 300000) {
    lastStatus = millis();
    userComm.printBriefStatus(&schedules, scheduleRunning);
  }

#if ENABLE_DISPLAY
  displayMgr.update();
#endif

  vTaskDelay(pdMS_TO_TICKS(10));
}

// ─── Diagnostic Helpers ───────────────────────────────────────────────────────

void printFullSystemDiagnostics() {
  Serial.println("\n==========================================");
  Serial.println("  FULL SYSTEM DIAGNOSTIC");
  Serial.println("==========================================\n");
  userComm.printSystemStatus(&schedules, scheduleRunning);
  userComm.printCommStatus();
  userComm.printSystemDiagnostics();
  Serial.println("==========================================\n");
}

String getSystemStatusJSON() {
  return userComm.getStatusJSON(&schedules, scheduleRunning);
}

void startSchedule(String scheduleId) {
  currentScheduleId = scheduleId;
  scheduleRunning   = true;
  userComm.onScheduleStarted(scheduleId);
}

void stopAllSchedules() {
  scheduleRunning = false;
  if (currentScheduleId.length() > 0) userComm.onScheduleCompleted(currentScheduleId);
}

void notifyValveAction(int nodeId, String valve, String action) {
  userComm.onValveAction(nodeId, valve, action);
}

void notifySystemError(String errorMessage)     { userComm.onSystemError(errorMessage);   }
void notifySystemWarning(String warningMessage) { userComm.onSystemWarning(warningMessage); }
