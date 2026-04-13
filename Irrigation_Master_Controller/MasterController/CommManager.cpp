#include "Config.h"
#include "CommConfig.h"
// CommManager.cpp - Central communication manager
// All communication concerns live here. MasterController.ino calls only the
// public API: begin(), process(), notify*(), getStatus().
#include "CommManager.h"

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

#if ENABLE_WIFI
bool CommManager::initWiFi() {
  printStepHeader("WiFi");
  initStatus.totalModules++;
  if (wifiComm.init(WIFI_SSID, WIFI_PASS))
    Serial.println("[CommMgr]   IP: " + wifiComm.getIPAddress());
  else
    Serial.println("[CommMgr]   ⚠ WiFi connecting in background");
  printStepSuccess("WiFi");
  initStatus.wifiOk = true; initStatus.successfulModules++;
  return true;
}
#endif

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

#if ENABLE_PPPOS
bool CommManager::initPPPoS() {
  printStepHeader("PPPoS (ModemPPPoS)");
  initStatus.totalModules++;
  if (!modemPPPoS.init()) { printStepFailure("PPPoS", "init() failed"); return false; }
  if (!modemPPPoS.connect(PPPOS_CONNECT_TIMEOUT_MS)) {
    printStepFailure("PPPoS", "connect() failed — check SIM/APN"); return false;
  }
  printStepSuccess("PPPoS");
  Serial.println("[CommMgr]   PPPoS IP: " + modemPPPoS.getLocalIP());
  initStatus.ppposOk = true; initStatus.successfulModules++;
  return true;
}
#endif

bool CommManager::initNetworkRouter() {
  printStepHeader("NetworkRouter");
  initStatus.totalModules++;
#if ENABLE_PPPOS && ENABLE_WIFI
  networkRouter.init(&modemPPPoS, &wifiComm);
#elif ENABLE_PPPOS
  networkRouter.init(&modemPPPoS, nullptr);
#elif ENABLE_WIFI
  networkRouter.init(nullptr, &wifiComm);
#else
  networkRouter.init(nullptr, nullptr);
  printStepFailure("NetworkRouter", "no bearers enabled");
  initStatus.successfulModules++;
  return false;
#endif
  if (!networkRouter.connect()) {
    printStepFailure("NetworkRouter", "no bearer came up");
    initStatus.networkRouterOk = false;
    initStatus.successfulModules++;
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
#if ENABLE_LORA
  if (!nodeComm.init(&loraComm)) { printStepFailure("NodeCommunication"); return false; }
#else
  Serial.println("[CommMgr]   LoRa disabled — node communication limited");
#endif
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
#if ENABLE_BLE
  if (initStatus.bleOk) {
    bleAdapter = new BLEChannelAdapter(bleComm);
    userComm.registerAdapter(bleAdapter);
  }
#endif
#if ENABLE_LORA && ENABLE_LORA_USER_COMM
  if (initStatus.loraOk) {
    loraAdapter = new LoRaChannelAdapter(loraComm, initStatus.loraOk);
    userComm.registerAdapter(loraAdapter);
  }
#endif

#if ENABLE_SERIAL_COMM
  // Serial is always available — no init-status gate needed
  serialAdapter = new SerialChannelAdapter(Serial);
  userComm.registerAdapter(serialAdapter);
#endif

#if ENABLE_SERIAL_COMM
  if (!serialCfgHandler)
    serialCfgHandler = new SerialConfigHandler(prefs);
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
#if ENABLE_BLE
  initBLE();
#endif
#if ENABLE_LORA
  initLoRa();
#endif
#if ENABLE_WIFI
  initWiFi();
#endif
#if ENABLE_MODEM
  initModem();       // Must precede SMS / PPPoS
#endif
#if ENABLE_SMS
  initSMS();
#endif
#if ENABLE_PPPOS
  initPPPoS();
#endif
  initNetworkRouter();
#if ENABLE_MQTT
  initMQTT();
#endif
#if ENABLE_HTTP
  initHTTP();
#endif
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

  // 1. Network / bearer background (PPP stack feed, WiFi reconnect, router)
  networkRouter.processBackground();
#if ENABLE_WIFI
  wifiComm.processBackground();
#endif

  // 2. Poll inbound channels and deliver ChannelMessages to userComm
  pollSMS();
  pollMQTT();
  pollHTTP();
  pollLoRa();
  pollSerial();

  // 3. Process raw LoRa node messages from incomingQueue
#if ENABLE_LORA
  loraComm.processIncoming();
  nodeComm.processIncoming();
#endif

  // 4. Drain general incomingQueue (non-user messages queued by LoRa etc.)
  String raw;
  while (incomingQueue.dequeue(raw)) {
    Serial.println("[CommMgr] Queue: " + raw);
    // Further routing can be added here (e.g. STAT| → telemetry handler)
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

  // ── User channels ─────────────────────────────────────────────────────
#if ENABLE_SMS
  s.smsReady     = initStatus.smsOk && modemSMS.isReady();
#endif
#if ENABLE_MQTT
  s.mqttConnected = initStatus.mqttOk && mqtt.isConnected();
  s.dataConnected = s.mqttConnected;   // Data channel = MQTT reachable
#endif
#if ENABLE_BLE
  s.bleConnected = initStatus.bleOk && bleComm.isConnected();
#endif
#if ENABLE_LORA
  s.loraUp = initStatus.loraOk;
#endif

  // ── Data bearer detail ────────────────────────────────────────────────
#if ENABLE_WIFI
  s.wifiUp = initStatus.wifiOk && wifiComm.isConnected();
  if (s.wifiUp && s.networkIP.length() == 0)
    s.networkIP = wifiComm.getIPAddress();
#endif
#if ENABLE_PPPOS
  s.ppposUp = initStatus.ppposOk && modemPPPoS.isConnected();
  if (s.ppposUp && s.networkIP.length() == 0)
    s.networkIP = modemPPPoS.getLocalIP();
#endif

  // Bearer name reflects which bearer is actually up
#if ENABLE_PPPOS
  if (s.ppposUp)      s.bearerName = "PPPoS";
  else
#endif
#if ENABLE_WIFI
  if (s.wifiUp)       s.bearerName = "WiFi";
  else
#endif
                      s.bearerName = "None";

  // ── Services ──────────────────────────────────────────────────────────
#if ENABLE_HTTP
  s.httpReady = initStatus.httpOk && httpComm.isReady();
#endif

  return s;
}


// ─── sendNodeCommand() ────────────────────────────────────────────────────────
bool CommManager::sendNodeCommand(int nodeId, const String &command) {
#if ENABLE_LORA
  if (!initStatus.nodeCommOk) {
    Serial.println("[CommMgr] ❌ sendNodeCommand: NodeCommunication not ready");
    return false;
  }
  return nodeComm.sendCommand(nodeId, command);
#else
  Serial.println("[CommMgr] ❌ sendNodeCommand: LoRa disabled");
  return false;
#endif
}

// ─── getUserComm() ────────────────────────────────────────────────────────────
UserCommunication* CommManager::getUserComm() {
  return initialized ? &userComm : nullptr;
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
