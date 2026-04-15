// MasterController.ino - Application layer only
//
// This file handles:
//   • Hardware system init: storage, preferences, config, display, RTC
//   • Application objects: ScheduleManager, TimeManager
//   • Calling commMgr for all communication (begin / process / notify)
//
// This file does NOT:
//   • Include any transport module headers (BLEComm, MQTTComm, etc.)
//   • Create or manage message queues, node communication, or user communication
//   • Know about channels, adapters, or message formats
//   • Reference userComm, nodeComm, incomingQueue, or loraComm directly

#include "Config.h"
#include "Utils.h"
#include "StorageManager.h"
#include "TimeManager.h"
#include "ScheduleManager.h"
#include "CommManager.h"       // Single include for all communication
#include "WSPController.h"     // Water Source Pump Controller
#include "IPController.h"      // Irrigation Pump Controller
#include "PumpScheduleManager.h" // Schedule-based pump control
#include "IrrigationSequencer.h" // Irrigation sequence execution engine

#if ENABLE_DISPLAY
  #include "DisplayManager.h"
#endif
#if ENABLE_RTC
  #include <RTClib.h>
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

// ─── Application module instances ─────────────────────────────────────────────
// Only application-layer objects live here.
// All communication objects live inside CommManager.
Preferences     prefs;
StorageManager  storage;
TimeManager     timeManager;
ScheduleManager scheduleMgr;
// Well pumps W1/W2/W3 (add pins for W2/W3 in Config.h as needed)
WSPController   wspCtrl;         // W1 — Well pump 1  (pin WSP_PIN)
// WSPController wspCtrl2("W2", WSP2_PIN); // W2 — uncomment when hardware present
// WSPController wspCtrl3("W3", WSP3_PIN); // W3

// Irrigation groups G1/G2 (each has own pump + nodes)
IPController    ipcCtrl;         // G1 — Irrigation group 1 (pin IPC_PIN)
// IPController ipcCtrl2("G2", IPC2_PIN); // G2 — uncomment when hardware present
PumpScheduleManager pumpSched;   // Pump schedule manager
IrrigationSequencer irrigSeq;    // Irrigation sequence engine
CommManager     commMgr;        // The only communication object in this file

#if ENABLE_DISPLAY
DisplayManager  displayMgr;
#endif

#if ENABLE_RTC
  TwoWire    WireRTC     = TwoWire(1);
  RTC_DS3231 rtc;
  bool       rtcAvailable = false;
#endif

// ─── Node command callback ────────────────────────────────────────────────────
// CommManager calls this when a user sends "NODE <id> <cmd>".
// Routes to ScheduleManager / hardware — stays in the application layer.
bool handleNodeCommand(int nodeId, const String &command) {
  Serial.printf("[MAIN] Node cmd: id=%d cmd=%s\n", nodeId, command.c_str());
  // CommManager's nodeComm.sendCommand is called via the callback registered
  // inside CommManager; this callback is the application's hook to add
  // schedule-aware logic before the node command is sent.
  return commMgr.sendNodeCommand(nodeId, command);
}

// ─── Setup ────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(1000);

  Serial.println("\n==========================================");
  Serial.println("  IRRIGATION CONTROLLER v3.0");
  Serial.println("==========================================\n");

  // ── System init ──────────────────────────────────────────────────────────
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

  // ── Communication init ───────────────────────────────────────────────────
  // One call initializes every transport, network router, node comm,
  // user comm, and channel adapters. The .ino knows nothing about internals.
  // Load runtime comm config from LittleFS (via StorageManager)
  storage.loadCommConfig(commCfg);

  auto commStatus = commMgr.begin(&schedules, &scheduleRunning, &scheduleLoaded);

  if (commStatus.successfulModules < commStatus.totalModules) {
    Serial.printf("⚠ CommManager: %d/%d modules initialized\n",
      commStatus.successfulModules, commStatus.totalModules);
  }

  // ── Application wiring ───────────────────────────────────────────────────
  // Register the node command callback so user "NODE x cmd" reaches hardware
  commMgr.setNodeCommandCallback(handleNodeCommand);

  // Schedule ADD/DEL command handler
  commMgr.getUserComm()->setScheduleCommandCallback(
    [](const String &raw) -> CommandResult {
      String up = raw; up.trim(); up.toUpperCase();

      // DEL SCHED <id>
      if (up.startsWith("DEL SCHED ") || up.startsWith("ISDL ")) {
        String id = up.startsWith("ISDL ") ? raw.substring(6) : raw.substring(10);
        id.trim();
        if (storage.deleteSchedule(id)) {
          // Remove from in-memory list
          schedules.erase(std::remove_if(schedules.begin(), schedules.end(),
            [&id](const Schedule &s){ return s.id == id; }), schedules.end());
          return CommandResult(true, "SCHED", "Deleted: " + id);
        }
        return CommandResult(false, "SCHED", "Not found: " + id);
      }

      // ADD SCHED <compact>
      if (up.startsWith("ISCH ")) {
        String compact = raw.substring(7); compact.trim();
        // ISCH is identical to ADD SCHED — falls through to same logic
        return scheduleCommandCallback(compact);
      }
      if (up.startsWith("ADD SCHED ")) {
        String compact = raw.substring(10); compact.trim();
        Schedule s;
        if (!scheduleMgr.parseCompact(compact, s))
          return CommandResult(false, "SCHED", "Parse error in compact format");
        if (s.id.length() == 0)
          return CommandResult(false, "SCHED", "Missing ID= field");
        if (s.seq.empty())
          return CommandResult(false, "SCHED", "Missing SEQ= field");
        // Compute next_run from timeStr + rec
        if (s.timeStr.length() > 0) {
          int hour = 0, min = 0;
          if (parseTimeHHMM(s.timeStr, hour, min)) {
            time_t now = time(nullptr);
            if (s.rec == 'W') {
              s.next_run_epoch = nextWeekdayOccurrence(now, s.weekday_mask, hour, min);
            } else if (s.rec == 'D') {
              struct tm t; localtime_r(&now, &t);
              t.tm_hour = hour; t.tm_min = min; t.tm_sec = 0;
              time_t candidate = mktime(&t);
              s.next_run_epoch = (candidate > now) ? candidate : candidate + 86400;
            } else {
              struct tm t; localtime_r(&now, &t);
              t.tm_hour = hour; t.tm_min = min; t.tm_sec = 0;
              time_t candidate = mktime(&t);
              s.next_run_epoch = (candidate > now) ? candidate : candidate + 86400;
            }
          }
        }
        s.enabled = true;
        storage.saveSchedule(s);
        schedules.push_back(s);
        String resp = "Added " + s.id + " rec:" + String(s.rec)
                    + " time:" + s.timeStr
                    + " steps:" + String(s.seq.size());
        return CommandResult(true, "SCHED", resp);
      }

      return CommandResult(false, "SCHED",
        "Usage: ADD SCHED <compact>  |  DEL SCHED <id>");
    });

  // Register pump command handler for WSP/IPC commands from any channel
  commMgr.getUserComm()->setPumpCommandCallback(
    [](const String &raw) -> CommandResult {
      String up = raw; up.trim(); up.toUpperCase();
      // WSP commands
      // W1/W2/W3 — well pump commands
      if (up=="W1 ON"||up=="WSP ON")  { wspCtrl.setMode(PumpMode::MANUAL); wspCtrl.start("cmd"); return CommandResult(true,"W1",wspCtrl.statusString()); }
      if (up=="W1 OFF"||up=="WSP OFF"){ wspCtrl.stop("cmd");               return CommandResult(true,"W1",wspCtrl.statusString()); }
      if (up=="W1 AUTO"||up=="WSP AUTO"){ wspCtrl.setMode(PumpMode::AUTO); return CommandResult(true,"W1","W1→AUTO"); }
      if (up=="W1 STATUS"||up=="WSP STATUS"){                               return CommandResult(true,"W1",wspCtrl.statusString()); }
      // G1/G2 — irrigation pump commands
      if (up=="G1 ON"||up=="IPC ON")  { ipcCtrl.setMode(PumpMode::MANUAL); ipcCtrl.start("cmd"); return CommandResult(true,"G1",ipcCtrl.statusString()); }
      if (up=="G1 OFF"||up=="IPC OFF"){ ipcCtrl.stop("cmd");               return CommandResult(true,"G1",ipcCtrl.statusString()); }
      if (up=="G1 STATUS"||up=="IPC STATUS"){                               return CommandResult(true,"G1",ipcCtrl.statusString()); }
      // Combined status
      if (up == "PUMP STATUS") {
        return CommandResult(true, "PUMP",
          String("W1:") + wspCtrl.statusString() + " | G1:" + ipcCtrl.statusString());
      }
      // Pump schedule commands
      // Short pump schedule commands: WSCH W1/G1..., DEL W1:id, DIS, ENA, PS LIST/STATUS
      if (up.startsWith("WSCH ")   || up.startsWith("DEL W") || up.startsWith("DEL G")
       || up.startsWith("DIS W") || up.startsWith("DIS G")
       || up.startsWith("ENA W") || up.startsWith("ENA G")
       || up == "WSCH LIST" || up == "WSCH STATUS"
       || up.startsWith("WSCH ")) {   // legacy compat
        String resp = pumpSched.handleCommand(up, raw);
        return CommandResult(true, "PS", resp);
      }
      return CommandResult(false, "PUMP",
        "W1 ON|OFF|AUTO|STATUS  G1 ON|OFF|STATUS  PUMP STATUS\n"
        "WSCH W1 I:id,T:HH:MM,R:D|W|O[,D:mask][,M:min]\n"
        "WSCH G1 I:id,T:HH:MM,R:W,D:42,Q:n.v.min-n.v.min\n"
        "DEL/DIS/ENA W1:id | WSCH LIST|STATUS");
    });

  // ScheduleManager needs userComm access for sending notifications.
  // CommManager exposes a thin pointer for this purpose.
  scheduleMgr.init(commMgr.getUserComm(), commMgr.getNodeComm(), &ipcCtrl, &irrigSeq);

  // ── Pump controllers ──────────────────────────────────────────────────
  wspCtrl.begin();
  wspCtrl.setAlertCallback([](const String &m, const String &s) {
    commMgr.sendAlert(m, s); });
#if WSP_TANK_EMPTY_PIN > 0
  wspCtrl.setTankEmptyCallback([] { return digitalRead(WSP_TANK_EMPTY_PIN) == LOW; });
#endif
#if WSP_TANK_FULL_PIN > 0
  wspCtrl.setTankFullCallback ([] { return digitalRead(WSP_TANK_FULL_PIN)  == HIGH; });
#endif

  ipcCtrl.begin();
  ipcCtrl.setMinOpenValves(IPC_MIN_OPEN_VALVES);
  ipcCtrl.setAlertCallback([](const String &m, const String &s) {
    commMgr.sendAlert(m, s); });

  irrigSeq.init(commMgr.getNodeComm(), &ipcCtrl, commMgr.getUserComm());
  irrigSeq.setMinOpenValves(IPC_MIN_OPEN_VALVES);
  commMgr.setAutoCloseCallback([](int nodeId, const String &reason) {
    irrigSeq.onNodeAutoClose(nodeId, reason);
  });
  irrigSeq.setMaxNodes(IPC_MAX_NODES);
  irrigSeq.setMaxValvesPerNode(IPC_MAX_VALVES_PER_NODE);

  pumpSched.init(&wspCtrl, &ipcCtrl, commMgr.getUserComm(), &storage);
  pumpSched.loadSchedules();

  Serial.println("\n==========================================");
  Serial.println("✓ SYSTEM READY");
  Serial.println("==========================================\n");

  commMgr.printBriefStatus();
}

// ─── Main Loop ────────────────────────────────────────────────────────────────
void loop() {
  // Single call drives all communication:
  //   • Channel polling (SMS, MQTT, HTTP)
  //   • LoRa node message processing
  //   • NetworkRouter background (PPP stack feed, reconnect)
  //   • WiFi reconnect
  //   • Inbound queue drain
  commMgr.process();
  wspCtrl.process();
  ipcCtrl.process();
  scheduleMgr.process();   // drives IrrigationSequencer + startIfDue
  pumpSched.process();

  // ── Periodic health check (every 60 s) ───────────────────────────────────
  static unsigned long lastHealth = 0;
  if (millis() - lastHealth > 60000) {
    lastHealth = millis();
    if (!commMgr.isHealthy()) {
      commMgr.sendAlert(commMgr.getHealthStatus(), "WARNING");
    }
  }

  // ── Periodic brief status to Serial (every 5 min) ────────────────────────
  static unsigned long lastStatus = 0;
  if (millis() - lastStatus > 300000) {
    lastStatus = millis();
    commMgr.printBriefStatus();
  }

#if ENABLE_DISPLAY
  displayMgr.update();
#endif

  vTaskDelay(pdMS_TO_TICKS(10));
}

// ─── Application helpers ──────────────────────────────────────────────────────

void startSchedule(const String &scheduleId) {
  currentScheduleId = scheduleId;
  scheduleRunning   = true;
  commMgr.notifyScheduleStarted(scheduleId);
}

void stopAllSchedules() {
  scheduleRunning = false;
  if (currentScheduleId.length() > 0)
    commMgr.notifyScheduleCompleted(currentScheduleId);
}

void notifyValveAction(int nodeId, const String &valve, const String &action) {
  commMgr.notifyValveAction(nodeId, valve, action);
}

void notifySystemError  (const String &msg) { commMgr.notifySystemError(msg);   }
void notifySystemWarning(const String &msg) { commMgr.notifySystemWarning(msg); }

void printFullSystemDiagnostics() {
  Serial.println("\n==========================================");
  Serial.println("  FULL SYSTEM DIAGNOSTIC");
  Serial.println("==========================================\n");
  commMgr.printDiagnostics();
  commMgr.printStatus();
  Serial.println("==========================================\n");
}

String getSystemStatusJSON() {
  return commMgr.getStatusJSON();
}
