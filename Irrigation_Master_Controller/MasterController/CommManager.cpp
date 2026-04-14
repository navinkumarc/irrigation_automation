#include "Config.h"
#include "CommConfig.h"
#include "StorageManager.h"
// CommManager.cpp - Central communication manager
// All communication concerns live here. MasterController.ino calls only the
// public API: begin(), process(), notify*(), getStatus().
#include "CommManager.h"
#include "NodeCommunication.h"

// commMgr global instance is defined in MasterController.ino
// CommManager.h provides: extern CommManager commMgr;

// ─── Constructor ──────────────────────────────────────────────────────────────
CommManager::CommManager() {}

// ─── Step logging ─────────────────────────────────────────────────────────────
void CommManager::printStepHeader(const String &n) {
  Serial.println("[CommMgr] [" + String(++stepCounter) + "] " + n);
}
void CommManager::printStepSuccess(const String &n) {
  Serial.println("[CommMgr] ✓ " + n + " ready");
}
void CommManager::printStepFailure(const String &n, const String &r) {
  String m = "[CommMgr] ❌ " + n + " failed";
  if (r.length()) m += " — " + r;
  Serial.println(m);
}

// ─── Private init steps ───────────────────────────────────────────────────────

#if ENABLE_BLE
bool CommManager::initBLE() {
  printStepHeader("BLE");
  initStatus.totalModules++;
  if (!bleComm.init()) { printStepFailure("BLE"); return false; }
  printStepSuccess("BLE");
  initStatus.bleOk = true; initStatus.successfulModules++;
  return true;
}
#endif

#if ENABLE_LORA
bool CommManager::initLoRa() {
  printStepHeader("LoRa");
  initStatus.totalModules++;
  if (!loraComm.init()) { printStepFailure("LoRa"); return false; }
  printStepSuccess("LoRa");
  initStatus.loraOk = true; initStatus.successfulModules++;
  return true;
}
#endif

// initWiFi() removed — NetworkRouter owns WiFi init via registerBearers()/begin()

#if ENABLE_MODEM
bool CommManager::initModem() {
  printStepHeader("Modem (ModemBase)");
  initStatus.totalModules++;
  if (!modemBase.init()) { printStepFailure("Modem"); return false; }
  printStepSuccess("Modem");
  initStatus.modemOk = true; initStatus.successfulModules++;
  return true;
}
#endif

#if ENABLE_SMS
bool CommManager::initSMS() {
  printStepHeader("SMS (ModemSMS)");
  initStatus.totalModules++;
  if (!modemSMS.configure()) { printStepFailure("SMS"); return false; }
  printStepSuccess("SMS");
  initStatus.smsOk = true; initStatus.successfulModules++;
  return true;
}
#endif

// initPPPoS() removed — NetworkRouter owns PPPoS init via registerBearers()/begin()

bool CommManager::initNetworkRouter() {
  // NetworkRouter owns all bearer logic.
  // CommManager only registers the hardware pointers and calls begin().
  printStepHeader("NetworkRouter");
  initStatus.totalModules++;

  // Register hardware pointers — pass nullptr for modules not compiled in
#if ENABLE_PPPOS && ENABLE_WIFI
  networkRouter.registerBearers(&modemPPPoS, &wifiComm);
#elif ENABLE_PPPOS
  networkRouter.registerBearers(&modemPPPoS, nullptr);
#elif ENABLE_WIFI
  networkRouter.registerBearers(nullptr, &wifiComm);
#else
  networkRouter.registerBearers(nullptr, nullptr);
#endif

  // begin() decides which bearer(s) to bring up per commCfg runtime flags:
  //   PPPoS only → PPPoS
  //   WiFi  only → WiFi
  //   Both       → PPPoS primary, WiFi fallback
  if (!networkRouter.begin()) {
    printStepFailure("NetworkRouter", "no bearer came up");
    initStatus.networkRouterOk = false; initStatus.successfulModules++;
    return false;
  }
  printStepSuccess("NetworkRouter");
  initStatus.networkRouterOk = true; initStatus.successfulModules++;
  return true;
}

#if ENABLE_MQTT
bool CommManager::initMQTT() {
  printStepHeader("MQTT");
  initStatus.totalModules++;
  if (!mqtt.init())      { printStepFailure("MQTT", "init() failed");      return false; }
  if (!mqtt.configure()) { printStepFailure("MQTT", "configure() failed"); return false; }
  printStepSuccess("MQTT");
  initStatus.mqttOk = true; initStatus.successfulModules++;
  return true;
}
#endif

#if ENABLE_HTTP
bool CommManager::initHTTP() {
  printStepHeader("HTTP REST API");
  initStatus.totalModules++;
  if (!httpComm.init(HTTP_SERVER_PORT)) { printStepFailure("HTTP"); return false; }
  printStepSuccess("HTTP");
  initStatus.httpOk = true; initStatus.successfulModules++;
  return true;
}
#endif

bool CommManager::initNodeCommunication() {
  printStepHeader("NodeCommunication");
  initStatus.totalModules++;

  // Register node transport adapters respecting runtime config and priority:
  //   LoRa only        → LoRa
  //   BLE only         → BLE
  //   LoRa + BLE       → LoRa (primary), BLE (fallback)
  // Registration order = priority order inside NodeCommunication.

#if ENABLE_LORA
  if (commCfg.chLoRa) {
    loraNodeTransport = new LoRaNodeTransport(loraComm);
    loraNodeTransport->setHwReady(initStatus.loraOk);
    nodeComm.registerTransport(loraNodeTransport);
    Serial.println("[CommMgr]  LoRa registered as node transport (primary)");
  }
#endif

#if ENABLE_BLE
  if (commCfg.chBluetooth) {
    bleNodeTransport = new BLENodeTransport(bleComm);
    nodeComm.registerTransport(bleNodeTransport);
    Serial.println("[CommMgr]  BLE registered as node transport (fallback)");
  }
#endif

  if (!nodeComm.begin()) {
    printStepFailure("NodeCommunication", "no transports available");
    initStatus.nodeCommOk = false; initStatus.successfulModules++;
    return false;
  }

  // Node message callback: format via MsgFmt and forward to UserCommunication.
  nodeComm.setMessageCallback([this](const NodeMessage &nm) {
    String alert;
    switch (nm.type) {
      case NodeMessageType::TELEMETRY:
        alert = MsgFmt::alertNodeTelemetry(
          nm.nodeId, nm.batteryPercent, nm.batteryVoltage,
          nm.solarVoltage, nm.valveStates);
        break;
      case NodeMessageType::AUTO_CLOSE:
        alert = MsgFmt::alertAutoClose(nm.nodeId, nm.reason);
        break;
      default:
        alert = MsgFmt::alertWarning("Unknown message from node " + String(nm.nodeId));
        break;
    }
    Serial.println("[CommMgr] Node event: " + alert);
    userComm.sendAlert(alert, SEV_INFO);
  });

  printStepSuccess("NodeCommunication");
  initStatus.nodeCommOk = true; initStatus.successfulModules++;
  return true;
}

bool CommManager::initUserCommunication() {
  printStepHeader("UserCommunication + Channel Adapters");
  initStatus.totalModules++;

  // userComm lives inside CommManager — adminPhone set in begin()
  // (already called before this step)

  // Create and register one adapter per enabled, initialized transport
#if ENABLE_SMS
  if (initStatus.smsOk) {
    smsAdapter = new SMSChannelAdapter(modemSMS);
    userComm.registerAdapter(smsAdapter);
  }
#endif
#if ENABLE_MQTT
  if (initStatus.mqttOk) {
    mqttAdapter = new MQTTChannelAdapter(mqtt, MQTT_TOPIC_ALERTS);
    userComm.registerAdapter(mqttAdapter);
  }
#endif
  // User channel adapters for LoRa and BLE: LoRa primary, BLE fallback.
  // Registered in priority order — UserCommunication broadcasts to all available.
#if ENABLE_LORA && ENABLE_LORA_USER_COMM
  if (commCfg.chLoRa && initStatus.loraOk) {
    loraAdapter = new LoRaChannelAdapter(loraComm, initStatus.loraOk);
    userComm.registerAdapter(loraAdapter);
    Serial.println("[CommMgr]  LoRa registered as user channel (primary)");
  }
#endif
#if ENABLE_BLE
  if (commCfg.chBluetooth && initStatus.bleOk) {
    bleAdapter = new BLEChannelAdapter(bleComm);
    userComm.registerAdapter(bleAdapter);
    Serial.println("[CommMgr]  BLE registered as user channel (fallback)");
  }
#endif

#if ENABLE_SERIAL_COMM
  // Serial is always available — no init-status gate needed
  serialAdapter = new SerialChannelAdapter(Serial);
  userComm.registerAdapter(serialAdapter);
#endif

#if ENABLE_SERIAL_COMM
  if (!serialCfgHandler)
    serialCfgHandler = new SerialConfigHandler(storage);
#endif

  printStepSuccess("UserCommunication");
  initStatus.userCommOk = true; initStatus.successfulModules++;
  return true;
}

// ─── begin() ──────────────────────────────────────────────────────────────────
CommManagerStatus CommManager::begin(std::vector<Schedule> *sched,
                                      bool *running,
                                      bool *loaded) {
  Serial.println("\n==========================================");
  Serial.println("  COMM MANAGER INITIALIZATION");
  Serial.println("==========================================\n");

  // Store application state refs for process() and buildSystemStatus()
  schedules       = sched;
  scheduleRunning = running;
  scheduleLoaded  = loaded;

  // Init UserCommunication with admin phone before adapters are created
  userComm.init(commCfg.smsPhone1);

  // Init order: hardware transports → bearers → network → app-layer → UC
// Init LoRa and BLE only when runtime config enables them
#if ENABLE_LORA
  if (commCfg.chLoRa) {
    initLoRa();
  } else {
    Serial.println("[CommMgr]  LoRa skipped (disabled in config)");
  }
#endif
#if ENABLE_BLE
  if (commCfg.chBluetooth) {
    initBLE();
  } else {
    Serial.println("[CommMgr]  BLE skipped (disabled in config)");
  }
#endif
// ── Modem (SMS or PPPoS) — always init when modem hardware present ──────────
#if ENABLE_MODEM
  initModem();       // Must precede SMS / PPPoS
#endif
#if ENABLE_SMS
  initSMS();
#endif

// ── Internet stack — only when active channel needs it (MQTT or HTTP) ──────
// Step 1: bring up the configured bearer(s) — PPPoS and/or WiFi
// Step 2: init NetworkRouter with whichever bearers are enabled
// Step 3: init MQTT and/or HTTP on top of the established bearer
  if (commCfg.needsInternet()) {

    // NetworkRouter owns all bearer init (PPPoS/WiFi) and routing.
    // CommManager calls begin() — NetworkRouter decides which bearer(s)
    // to bring up based on commCfg.enablePPPoS / enableWiFi.
    initNetworkRouter();

    // ── Services: init MQTT and/or HTTP once bearer is up ─────────────────
#if ENABLE_MQTT
    if (commCfg.isMQTT()) initMQTT();
    else Serial.println("[CommMgr]  MQTT skipped (not active channel)");
#endif
#if ENABLE_HTTP
    if (commCfg.isHTTP()) initHTTP();
    else Serial.println("[CommMgr]  HTTP skipped (not active channel)");
#endif

  } else {
    Serial.println("[CommMgr]  Internet stack skipped — active channel: "
                   + String(commCfg.activeChannelName()));
  }
  initNodeCommunication();
  initUserCommunication(); // Always last — adapters need modules to be up

  initialized = true;
  printSummary();
  return initStatus;
}

// ─── process() ────────────────────────────────────────────────────────────────
// Single call from MasterController loop(). Drives everything.
void CommManager::process() {
  if (!initialized) return;

  // 1. Network / bearer background — only when internet channel is active.
  // networkRouter.processBackground() feeds the PPP stack, drives WiFi
  // reconnect (via wifi->processBackground() internally), checks liveness,
  // and triggers auto-reconnect. No separate WiFi call needed here.
  if (commCfg.needsInternet()) {
    networkRouter.processBackground();
  }

  // 2. Poll inbound channels and deliver ChannelMessages to userComm
  pollSMS();
  pollMQTT();
  pollHTTP();
  pollLoRa();
  pollSerial();

  // 3. Drive NodeCommunication — polls all registered transport adapters
  nodeComm.process();

  // 4. Drain general incomingQueue (non-user messages queued by LoRa etc.)
  String raw;
  while (incomingQueue.dequeue(raw)) {
    Serial.println("[CommMgr] Queue: " + raw);
    // Non-node queue messages (node msgs use MSG_STAT_PREFIX / MSG_AUTO_CLOSE_PREFIX)
  }
}

// ─── Inbound channel pollers ──────────────────────────────────────────────────

void CommManager::pollSMS() {
#if ENABLE_SMS
  if (!initStatus.smsOk || !modemSMS.isReady()) return;
  modemSMS.processBackground();
  auto incoming = modemSMS.processIncomingMessages(commCfg.smsPhone1);
  SystemStatus sys = buildSystemStatus();
  for (auto &sms : incoming) {
    String sender = sms.sender;
    int    idx    = sms.index;
    auto   reply  = [sender, idx](const String &resp) {
      modemSMS.sendSMS(sender, resp);
      modemSMS.deleteSMS(idx);
    };
    deliverMessage(ChannelMessage(sms.message, sms.sender, "SMS", reply));
  }
#endif
}

void CommManager::pollMQTT() {
#if ENABLE_MQTT
  if (initStatus.mqttOk) mqtt.processBackground();
  // MQTT inbound arrives via the message callback set during initMQTT.
  // That callback calls deliverMessage() directly (wired in initMQTT).
#endif
}

void CommManager::pollHTTP() {
#if ENABLE_HTTP
  if (!initStatus.httpOk) return;
  httpComm.processBackground();
  if (!httpComm.hasCommands()) return;
  auto cmds = httpComm.getCommands();
  httpComm.clearCommands();
  SystemStatus sys = buildSystemStatus();
  for (auto &cmd : cmds) {
    String src = cmd.source;
    auto reply = [src](const String &resp) {
      Serial.println("[CommMgr] HTTP reply → " + src + ": " + resp);
    };
    deliverMessage(ChannelMessage(cmd.command, cmd.source, "HTTP", reply));
  }
#endif
}

void CommManager::pollLoRa() {
  // LoRa user commands arrive via incomingQueue (set by LoRaComm callbacks).
  // node-to-node messages are handled by nodeComm.processIncoming() in process().
  // User-facing LoRa commands share incomingQueue — drain and deliver below.
  // (Raw queue drain happens in process(); individual STAT/AUTO_CLOSE messages
  //  are dispatched by NodeCommunication. User commands here would be "CMD|..." style.)
}

// ─── pollSerial() ─────────────────────────────────────────────────────────────
// Reads complete lines from the hardware Serial port (USB monitor / UART0).
// Each complete line is wrapped in a ChannelMessage and delivered to
// UserCommunication exactly like any other channel.
// Reply lambda writes the response back to Serial so the operator sees it
// immediately in the monitor.
void CommManager::pollSerial() {
#if ENABLE_SERIAL_COMM
  if (!serialAdapter) return;

  String line = serialAdapter->readLine();
  if (line.length() == 0) return;   // No complete line yet

  Serial.println("[Serial] ← "" + line + """);

  // Config commands (SET/ENABLE/DISABLE/SHOW/SAVE/RESET CONFIG)
  // are intercepted here — never reach UserCommunication.
  if (serialCfgHandler && serialCfgHandler->handle(line)) return;

  // Build a reply lambda that echoes the response back to Serial
  auto reply = [](const String &response) {
    Serial.println("[Serial] → " + response);
  };

  ChannelMessage msg(line, "Serial", "Serial", reply);
  deliverMessage(msg);
#endif
}

// ─── deliverMessage() ────────────────────────────────────────────────────────
void CommManager::deliverMessage(const ChannelMessage &msg) {
  if (!initialized) return;
  SystemStatus sys = buildSystemStatus();
  userComm.onMessageReceived(msg, scheduleRunning, scheduleLoaded, sys);
}

// ─── Application event notifications ─────────────────────────────────────────
void CommManager::notifyScheduleStarted  (const String &id) { userComm.onScheduleStarted(id);   }
void CommManager::notifyScheduleCompleted(const String &id) { userComm.onScheduleCompleted(id);  }
void CommManager::notifyScheduleFailed   (const String &id, const String &reason) {
  userComm.onScheduleFailed(id, reason);
}
void CommManager::notifyValveAction(int nodeId, const String &valve, const String &action) {
  userComm.onValveAction(nodeId, valve, action);
}
void CommManager::notifySystemError  (const String &msg) { userComm.onSystemError(msg);   }
void CommManager::notifySystemWarning(const String &msg) { userComm.onSystemWarning(msg); }
void CommManager::sendAlert(const String &msg, const String &severity) {
  userComm.sendAlert(msg, severity);
}

// ─── Status ───────────────────────────────────────────────────────────────────
SystemStatus CommManager::getStatus() const {
  return buildSystemStatus();
}

CommManagerStatus CommManager::getInitStatus() const { return initStatus; }

bool CommManager::isHealthy() const {
  return userComm.isSystemHealthy();
}

String CommManager::getHealthStatus() const {
  return userComm.getHealthStatus();
}

bool CommManager::isFullyInitialized() const {
  return initialized &&
         initStatus.totalModules > 0 &&
         initStatus.successfulModules == initStatus.totalModules;
}

void CommManager::printStatus() const {
  const_cast<CommManager*>(this)->printSummary();
}

void CommManager::printBriefStatus() const {
  userComm.printBriefStatus(buildSystemStatus());
}

void CommManager::printDiagnostics() const {
  userComm.printSystemStatus(buildSystemStatus());
  userComm.printSystemDiagnostics();
}

String CommManager::getStatusJSON() const {
  return userComm.getStatusJSON(buildSystemStatus());
}

void CommManager::setNodeCommandCallback(NodeCommandCallback cb) {
  userComm.setNodeCommandCallback(cb);
}

// ─── buildSystemStatus() ─────────────────────────────────────────────────────
SystemStatus CommManager::buildSystemStatus() const {
  SystemStatus s;
  s.uptimeSeconds  = millis() / 1000;
  s.freeHeapBytes  = ESP.getFreeHeap();
  s.totalHeapBytes = ESP.getHeapSize();

  s.scheduleRunning = scheduleRunning ? *scheduleRunning : false;

  if (schedules) {
    s.totalSchedules = schedules->size();
    for (auto &sch : *schedules) if (sch.enabled) s.enabledSchedules++;
  }

  // ── Active channel ────────────────────────────────────────────────────
  s.activeChannelName = commCfg.activeChannelName();
#if ENABLE_SMS
  s.smsReady      = commCfg.isSMS()  && initStatus.smsOk  && modemSMS.isReady();
#endif
#if ENABLE_MQTT
  s.mqttConnected = commCfg.isMQTT() && initStatus.mqttOk && mqtt.isConnected();
#endif
#if ENABLE_HTTP
  s.httpReady     = commCfg.isHTTP() && initStatus.httpOk && httpComm.isReady();
#endif

  // ── Independent channels ──────────────────────────────────────────────
#if ENABLE_BLE
  s.bleConnected = initStatus.bleOk && bleComm.isConnected();
#endif
#if ENABLE_LORA
  s.loraUp = initStatus.loraOk;
#endif

  // ── Internet bearer — queried from NetworkRouter ──────────────────────
  s.ppposUp    = networkRouter.isPPPoSUp();
  s.wifiUp     = networkRouter.isWiFiUp();
  s.networkIP  = networkRouter.getActiveIP();
  s.bearerName = s.ppposUp ? "PPPoS" : (s.wifiUp ? "WiFi" : "None");

  return s;
}


// ─── sendNodeCommand() ────────────────────────────────────────────────────────
bool CommManager::sendNodeCommand(int nodeId, const String &command) {
  // command is the cmdType token: "OPEN", "CLOSE", "PING", etc.
  if (!initStatus.nodeCommOk) {
    Serial.println("[CommMgr] ❌ sendNodeCommand: NodeCommunication not ready");
    return false;
  }
  return nodeComm.sendCommand(command, nodeId, "", 0, 0);
}

// ─── getUserComm() ────────────────────────────────────────────────────────────
UserCommunication* CommManager::getUserComm() {
  return initialized ? &userComm : nullptr;
}

NodeCommunication* CommManager::getNodeComm() {
  return initialized ? &nodeComm : nullptr;
}

// ─── printSummary() ──────────────────────────────────────────────────────────
void CommManager::printSummary() {
  Serial.println("\n==========================================");
  Serial.println("  COMM MANAGER — INIT SUMMARY");
  Serial.println("==========================================");
  Serial.printf("Modules: %d/%d OK\n", initStatus.successfulModules, initStatus.totalModules);
  Serial.println();
#if ENABLE_BLE
  Serial.printf("  BLE:     %s\n", initStatus.bleOk           ? "✓ OK" : "✗ FAILED");
#endif
#if ENABLE_LORA
  Serial.printf("  LoRa:    %s\n", initStatus.loraOk          ? "✓ OK" : "✗ FAILED");
#endif
#if ENABLE_WIFI
  Serial.printf("  WiFi:    %s\n", initStatus.wifiOk          ? "✓ OK" : "⚠ CONNECTING");
#endif
#if ENABLE_MODEM
  Serial.printf("  Modem:   %s\n", initStatus.modemOk         ? "✓ OK" : "✗ FAILED");
#endif
#if ENABLE_SMS
  Serial.printf("  SMS:     %s\n", initStatus.smsOk           ? "✓ OK" : "✗ FAILED");
#endif
#if ENABLE_PPPOS
  Serial.printf("  PPPoS:   %s\n", initStatus.ppposOk         ? "✓ OK" : "✗ FAILED");
#endif
  Serial.printf("  Router:  %s\n", initStatus.networkRouterOk ? "✓ OK" : "⚠ NO BEARER");
#if ENABLE_MQTT
  Serial.printf("  MQTT:    %s\n", initStatus.mqttOk          ? "✓ OK" : "⚠ CONNECTING");
#endif
#if ENABLE_HTTP
  Serial.printf("  HTTP:    %s\n", initStatus.httpOk          ? "✓ OK" : "✗ FAILED");
#endif
  Serial.printf("  NodeComm:%s\n", initStatus.nodeCommOk      ? "✓ OK" : "✗ FAILED");
  Serial.printf("  UserComm:%s\n", initStatus.userCommOk      ? "✓ OK" : "✗ FAILED");
  Serial.println("==========================================\n");
}
