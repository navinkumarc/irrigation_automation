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
#include "WSPController.h"     // Water Source Pump — pure GPIO
#include "IPController.h"      // Irrigation Pump Controller
#include "TankManager.h"       // Tank level sensor manager
#include "WaterToTankController.h"    // WaterToTankController = WSP pump + tank
#include "ProcessConfig.h"     // Process group config structs
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
// Well pumps W1/W2/W3 — GPIO 7/38/39 on Heltec V3 (ESP32-S3)
// ── Water source pumps (pure GPIO) ───────────────────────────────────────
// J3 side: W1(relay=7)  W2(relay=3)
WSPController   wspCtrl ("W1", WSP_PIN,  WSP_ACTIVE_HIGH);  // W1 relay → J3-18 GPIO7
WSPController   wspCtrl2("W2", WSP2_PIN, WSP2_ACTIVE_HIGH); // W2 relay → J3-14 GPIO3

// ── Storage tanks (sensor monitors) ──────────────────────────────────────
// Sensors: W1(empty=GPIO6 J3-17, full=GPIO5 J3-16)  W2(empty=GPIO2 J3-13, full=GPIO38 J3-11)
TankManager     tank1("T1");  // Tank 1 — serves fill group FG1
TankManager     tank2("T2");  // Tank 2 — serves fill group FG2

// ── Water fill groups (WSP pump + tank combined) ──────────────────────────
WaterToTankController  wttCtrl1("FG1");  // FG1 = W1 pump + T1 tank
WaterToTankController  wttCtrl2("FG2");  // FG2 = W2 pump + T2 tank

// ── Irrigation groups (IPC pump + nodes) ─────────────────────────────────
// J2 side: G1(relay=47)  G2(relay=48)
IPController    ipcCtrl ("G1", IPC_PIN,  IPC_ACTIVE_HIGH);  // G1 relay → J2-13 GPIO47
IPController    ipcCtrl2("G2", IPC2_PIN, IPC2_ACTIVE_HIGH); // G2 relay → J2-14 GPIO48
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

// ─── applyWTTConfig() — wire a loaded WTT config to live objects ──────────────
String applyWTTConfig(const WTTGroupConfig &cfg) {
  WaterToTankController *wtt = nullptr;
  WSPController         *pump= nullptr;
  TankManager           *tank= nullptr;

  if      (cfg.id == "FG1") wtt = &wttCtrl1;
  else if (cfg.id == "FG2") wtt = &wttCtrl2;
  else return "Unknown WTT id: " + cfg.id;

  if      (cfg.pumpId == "W1") pump = &wspCtrl;
  else if (cfg.pumpId == "W2") pump = &wspCtrl2;
  else return "Unknown pump: " + cfg.pumpId;

  if      (cfg.tankId == "T1") tank = &tank1;
  else if (cfg.tankId == "T2") tank = &tank2;
  else return "Unknown tank: " + cfg.tankId;

  wtt->init(pump, tank);
  return "WTT " + cfg.id + " active: pump=" + cfg.pumpId + " tank=" + cfg.tankId;
}

// ─── applyIrrConfig() — wire a loaded IRR config to live objects ──────────────
String applyIrrConfig(const IrrGroupConfig &cfg) {
  IPController *ipc = nullptr;
  if      (cfg.id == "IG1" || cfg.pumpId == "G1") ipc = &ipcCtrl;
  else if (cfg.id == "IG2" || cfg.pumpId == "G2") ipc = &ipcCtrl2;
  else return "Unknown IRR pump: " + cfg.pumpId;

  ipc->setMinOpenValves(cfg.minValves);
  irrigSeq.setMinOpenValves(cfg.minValves);
  // Apply node/valve limits from Config.h defaults
  irrigSeq.setMaxNodes(IPC_MAX_NODES);
  irrigSeq.setMaxValvesPerNode(IPC_MAX_VALVES_PER_NODE);
  return "IRR " + cfg.id + " active: pump=" + cfg.pumpId
         + " nodes=" + String(cfg.nodeCount)
         + " minValves=" + String(cfg.minValves);
}

// ─── setupShowAll() — list configured groups ──────────────────────────────────
String setupShowAll() {
  String out = "=== Configured Process Groups ===\n";
  WTTGroupConfig wttCfgs[MAX_WTT_GROUPS];
  IrrGroupConfig irrCfgs[MAX_IRR_GROUPS];
  int wc = 0, ic = 0;
  storage.loadWTTConfigs(wttCfgs, MAX_WTT_GROUPS, wc);
  storage.loadIrrConfigs(irrCfgs, MAX_IRR_GROUPS, ic);
  if (wc == 0 && ic == 0) return out + "None configured.";
  for (int i = 0; i < wc; i++)
    out += "WTT " + wttCfgs[i].id + ": pump=" + wttCfgs[i].pumpId
         + " tank=" + wttCfgs[i].tankId + "\n";
  for (int i = 0; i < ic; i++) {
    out += "IRR " + irrCfgs[i].id + ": pump=" + irrCfgs[i].pumpId
         + " min=" + irrCfgs[i].minValves
         + " nodes=" + irrCfgs[i].nodeCount + "\n";
    for (int n = 0; n < irrCfgs[i].nodeCount; n++) {
      out += "  Node " + String(irrCfgs[i].nodes[n].nodeId) + " valves=[";
      for (int v = 0; v < irrCfgs[i].nodes[n].valveCount; v++) {
        if (v) out += ",";
        out += irrCfgs[i].nodes[n].valves[v];
      }
      out += "]\n";
    }
  }
  out += "=================================";
  return out;
}

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
        // Route through UserCommunication schedule command handler
        String fwd = "ADD SCHED " + raw.substring(5);
        return commMgr.getUserComm()->dispatchCommand(
          fwd, &scheduleRunning, &scheduleLoaded, commMgr.getStatus());
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
      // ── Fill group commands: FG1/FG2 (WSP pump + tank) ──────────────────────
      if (up=="FG1 ON")    { wttCtrl1.setMode(WTTMode::MANUAL); wttCtrl1.start("cmd");  return CommandResult(true,"FG1",wttCtrl1.statusString()); }
      if (up=="FG1 OFF")   { wttCtrl1.stop("cmd");                                       return CommandResult(true,"FG1",wttCtrl1.statusString()); }
      if (up=="FG1 AUTO")  { wttCtrl1.setMode(WTTMode::AUTO);                           return CommandResult(true,"FG1",wttCtrl1.statusString()); }
      if (up=="FG1 STATUS"){                                                               return CommandResult(true,"FG1",wttCtrl1.statusString()); }
      if (up=="FG2 ON")    { wttCtrl2.setMode(WTTMode::MANUAL); wttCtrl2.start("cmd");  return CommandResult(true,"FG2",wttCtrl2.statusString()); }
      if (up=="FG2 OFF")   { wttCtrl2.stop("cmd");                                       return CommandResult(true,"FG2",wttCtrl2.statusString()); }
      if (up=="FG2 AUTO")  { wttCtrl2.setMode(WTTMode::AUTO);                           return CommandResult(true,"FG2",wttCtrl2.statusString()); }
      if (up=="FG2 STATUS"){                                                               return CommandResult(true,"FG2",wttCtrl2.statusString()); }
      // Tank status
      if (up=="T1 STATUS") return CommandResult(true,"T1",tank1.statusString());
      if (up=="T2 STATUS") return CommandResult(true,"T2",tank2.statusString());
      // G1/G2 — irrigation pump commands
      if (up=="G1 ON")    { ipcCtrl.setMode(PumpMode::MANUAL);  ipcCtrl.start("cmd");  return CommandResult(true,"G1",ipcCtrl.statusString()); }
      if (up=="G1 OFF")   { ipcCtrl.stop("cmd");                               return CommandResult(true,"G1",ipcCtrl.statusString()); }
      if (up=="G1 STATUS"){                                                     return CommandResult(true,"G1",ipcCtrl.statusString()); }
      if (up=="G2 ON")    { ipcCtrl2.setMode(PumpMode::MANUAL); ipcCtrl2.start("cmd"); return CommandResult(true,"G2",ipcCtrl2.statusString()); }
      if (up=="G2 OFF")   { ipcCtrl2.stop("cmd");                              return CommandResult(true,"G2",ipcCtrl2.statusString()); }
      if (up=="G2 STATUS"){                                                     return CommandResult(true,"G2",ipcCtrl2.statusString()); }
      // Combined status
      if (up == "PUMP STATUS") {
        return CommandResult(true, "PUMP",
          wttCtrl1.statusString() + "\n"
        + wttCtrl2.statusString() + "\n"
        + "G1:" + ipcCtrl.statusString()   + "\n"
        + "G2:" + ipcCtrl2.statusString());
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
        "FG1|FG2 ON|OFF|AUTO|STATUS  G1|G2 ON|OFF|STATUS\n"
        "T1|T2 STATUS  PUMP STATUS\n"
        "WSCH FG1 I:id,T:HH:MM,R:D|W|O[,D:mask][,M:min]\n"
        "ISCH G1 I:id,T:HH:MM,R:W,D:42,Q:n.v.min-n.v.min\n"
        "DEL/DIS/ENA FG1:id | WSCH LIST|STATUS");
    });

  // ScheduleManager needs userComm access for sending notifications.
  // CommManager exposes a thin pointer for this purpose.
  scheduleMgr.init(commMgr.getUserComm(), commMgr.getNodeComm(), &ipcCtrl, &irrigSeq);

  // ── Pump controllers ──────────────────────────────────────────────────
  // ── WSP pumps (pure GPIO) ────────────────────────────────────────────────
  wspCtrl.begin();
  wspCtrl.setAlertCallback([](const String &m, const String &s){ commMgr.sendAlert(m,s); });
  wspCtrl2.begin();
  wspCtrl2.setAlertCallback([](const String &m, const String &s){ commMgr.sendAlert(m,s); });

  // ── Tanks — wire sensor pins then alert callback ──────────────────────────
  tank1.setAlertCallback([](const String &m, const String &s){ commMgr.sendAlert(m,s); });
#if WSP_TANK_EMPTY_PIN > 0
  pinMode(WSP_TANK_EMPTY_PIN, INPUT_PULLUP);
  tank1.setSensorCallbacks(
    [] { return digitalRead(WSP_TANK_EMPTY_PIN) == LOW; },  // empty = LOW
    [] { return digitalRead(WSP_TANK_FULL_PIN)  == LOW; }); // full  = LOW
#endif
  tank2.setAlertCallback([](const String &m, const String &s){ commMgr.sendAlert(m,s); });
#if WSP2_TANK_EMPTY_PIN > 0
  pinMode(WSP2_TANK_EMPTY_PIN, INPUT_PULLUP);
  tank2.setSensorCallbacks(
    [] { return digitalRead(WSP2_TANK_EMPTY_PIN) == LOW; },
    [] { return digitalRead(WSP2_TANK_FULL_PIN)  == LOW; });
#endif

  // ── Fill groups — bind pump + tank ────────────────────────────────────────
  wttCtrl1.setAlertCallback([](const String &m, const String &s){ commMgr.sendAlert(m,s); });
  // wttCtrl1 init done by applyWTTConfig() from saved config
  wttCtrl2.setAlertCallback([](const String &m, const String &s){ commMgr.sendAlert(m,s); });
  // wttCtrl2 init done by applyWTTConfig() from saved config


  // ── G1 ──────────────────────────────────────────────────────────────
  ipcCtrl.begin();
  ipcCtrl.setMinOpenValves(IPC_MIN_OPEN_VALVES);
  ipcCtrl.setAlertCallback([](const String &m, const String &s) { commMgr.sendAlert(m, s); });
  // ── G2 ──────────────────────────────────────────────────────────────
  ipcCtrl2.begin();
  ipcCtrl2.setMinOpenValves(IPC_MIN_OPEN_VALVES);
  ipcCtrl2.setAlertCallback([](const String &m, const String &s) { commMgr.sendAlert(m, s); });

  irrigSeq.init(commMgr.getNodeComm(), &ipcCtrl, commMgr.getUserComm());
  irrigSeq.setMinOpenValves(IPC_MIN_OPEN_VALVES);
  commMgr.setAutoCloseCallback([](int nodeId, const String &reason) {
    irrigSeq.onNodeAutoClose(nodeId, reason);
  });

  // ── Register Serial setup callbacks ─────────────────────────────────────
  if (auto *sch = commMgr.getSerialCfgHandler()) {
    sch->setWTTSetupCallback ([](const WTTGroupConfig &cfg) { return applyWTTConfig(cfg); });
    sch->setIrrSetupCallback ([](const IrrGroupConfig  &cfg) { return applyIrrConfig(cfg); });
    sch->setSetupDelCallback ([](const String &id)            { return String("Removed: ") + id; });
    sch->setSetupShowCallback([]()                             { return setupShowAll(); });
  }

  // ── Load and apply saved process configs ─────────────────────────────────
  {
    WTTGroupConfig wttCfgs[MAX_WTT_GROUPS]; int wc = 0;
    storage.loadWTTConfigs(wttCfgs, MAX_WTT_GROUPS, wc);
    for (int i = 0; i < wc; i++) {
      String r = applyWTTConfig(wttCfgs[i]);
      Serial.println("[Setup] WTT: " + r);
    }
    IrrGroupConfig irrCfgs[MAX_IRR_GROUPS]; int ic = 0;
    storage.loadIrrConfigs(irrCfgs, MAX_IRR_GROUPS, ic);
    for (int i = 0; i < ic; i++) {
      String r = applyIrrConfig(irrCfgs[i]);
      Serial.println("[Setup] IRR: " + r);
    }
    if (wc + ic == 0)
      Serial.println("[Setup] ⚠ No process groups configured. Use SETUP WTT / SETUP IRR via Serial.");
  }
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
  wttCtrl1.process();   // drives WSP pump + tank sensor for FG1
  wttCtrl2.process();   // drives WSP pump + tank sensor for FG2
  ipcCtrl.process();  ipcCtrl2.process();
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
