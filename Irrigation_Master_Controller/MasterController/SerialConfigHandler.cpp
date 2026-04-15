// SerialConfigHandler.cpp  —  Serial config console
// All persistence delegated to StorageManager.
#include "Config.h"
#include "SerialConfigHandler.h"

bool SerialConfigHandler::handle(const String &line) {
  String up = line; up.trim(); up.toUpperCase();

  if (up == "CONFIG HELP" || up == "HELP CONFIG") { printHelp(); return true; }
  if (up == "SHOW CONFIG"  || up == "CONFIG")     { commCfg.print(); return true; }
  if (up == "SAVE CONFIG") {
    _storage.saveCommConfig(commCfg);
    Serial.println("[Config] Saved. Reboot to apply changes.");
    return true;
  }
  if (up == "RESET CONFIG") {
    Serial.println("[Config] Resetting to firmware defaults...");
    _storage.resetCommConfig(commCfg);
    commCfg.print();
    return true;
  }
  if (up.startsWith("SET CHANNEL ")) return handleSetChannel(up);
  if (up.startsWith("ENABLE "))      return handleBearer(up, true);
  if (up.startsWith("DISABLE "))     return handleBearer(up, false);
  if (up.startsWith("SET "))         return handleSet(up, line);
  if (up.startsWith("SETUP "))       return handleSetup(up, line);

  if (up == "RESTART" || up == "REBOOT") {
    Serial.println("[Config] Restarting in 1 second...");
    Serial.flush(); delay(1000); ESP.restart();
    return true;
  }
  if (up == "FACTORY RESET") {
    Serial.println("[Config] Factory reset — clearing all config and restarting...");
    _storage.resetCommConfig(commCfg);
    Serial.println("[Config] ✓ Config cleared. Restarting...");
    Serial.flush(); delay(1000); ESP.restart();
    return true;
  }
  return false;
}

// ── SET CHANNEL SMS | MQTT | HTTP ────────────────────────────────────────────
bool SerialConfigHandler::handleSetChannel(const String &up) {
  String ch = up.substring(12); ch.trim();   // after "SET CHANNEL "

  if (ch == "SMS") {
#if ENABLE_SMS
    commCfg.activeChannel = ActiveChannel::SMS;
    Serial.println("[Config] Active channel -> SMS");
#else
    Serial.println("[Config] SMS not compiled in (ENABLE_SMS=0)");
#endif
  } else if (ch == "MQTT") {
#if ENABLE_MQTT
    commCfg.activeChannel = ActiveChannel::MQTT;
    Serial.println("[Config] Active channel -> MQTT");
#else
    Serial.println("[Config] MQTT not compiled in (ENABLE_MQTT=0)");
#endif
  } else if (ch == "HTTP") {
#if ENABLE_HTTP
    commCfg.activeChannel = ActiveChannel::HTTP;
    Serial.println("[Config] Active channel -> HTTP");
#else
    Serial.println("[Config] HTTP not compiled in (ENABLE_HTTP=0)");
#endif
  } else if (ch == "NONE") {
    commCfg.activeChannel = ActiveChannel::NONE;
    Serial.println("[Config] Active channel -> NONE (no outbound alerts)");
  } else {
    Serial.println("[Config] Unknown channel: " + ch + "  (SMS | MQTT | HTTP | NONE)");
    return true;
  }
  Serial.println("[Config] Run SAVE CONFIG to persist. Reboot to apply.");
  return true;
}

// ── ENABLE / DISABLE (independent channels + bearers) ────────────────────────
bool SerialConfigHandler::handleBearer(const String &up, bool enable) {
  // Extract token after "ENABLE " or "DISABLE "
  String tok = up.substring(enable ? 7 : 8); tok.trim();
  const char* verb = enable ? "ENABLED" : "DISABLED";

  if (tok == "BLE" || tok == "BLUETOOTH") {
    commCfg.chBluetooth = enable;
    Serial.printf("[Config] Bluetooth %s\n", verb);
  } else if (tok == "LORA") {
    commCfg.chLoRa = enable;
    Serial.printf("[Config] LoRa %s\n", verb);
  } else if (tok == "SERIAL") {
    Serial.println("[Config] Serial is always ON — cannot be changed");
    return true;
  } else if (tok == "WIFI") {
    commCfg.enableWiFi = enable;
    Serial.printf("[Config] WiFi bearer %s\n", verb);
  } else if (tok == "PPPOS") {
    commCfg.enablePPPoS = enable;
    Serial.printf("[Config] PPPoS bearer %s\n", verb);
  } else {
    Serial.println("[Config] Unknown: " + tok);
    Serial.println("[Config] Use: ENABLE/DISABLE  BLE | LORA | WIFI | PPPOS");
    return true;
  }
  Serial.println("[Config] Run SAVE CONFIG to persist. Reboot to apply.");
  return true;
}

// ── SET <KEY> <value> ─────────────────────────────────────────────────────────
bool SerialConfigHandler::handleSet(const String &up, const String &raw) {
  String body = up.substring(4); body.trim();
  int sp = body.indexOf(' ');
  if (sp <= 0) { Serial.println("[Config] Usage: SET <KEY> <value>"); return true; }
  String key = body.substring(0, sp); key.trim();

  // Value preserves original case (passwords, SSIDs, URLs)
  String rawBody = raw.substring(4); rawBody.trim();
  int rsp = rawBody.indexOf(' ');
  String val = (rsp > 0) ? rawBody.substring(rsp + 1) : "";
  val.trim();
  if (val.length() == 0) { Serial.println("[Config] Value cannot be empty"); return true; }
  String vu = val; vu.toUpperCase();

  if      (key == "WIFI_SSID")   { commCfg.wifiSSID     = val; Serial.println("[Config] WiFi SSID updated"); }
  else if (key == "WIFI_PASS")   { commCfg.wifiPass     = val; Serial.println("[Config] WiFi password updated"); }
  else if (key == "APN")         { commCfg.cellularAPN  = val; Serial.println("[Config] APN -> " + val); }
  else if (key == "MQTT_BROKER") { commCfg.mqttBroker   = val; Serial.println("[Config] MQTT broker -> " + val); }
  else if (key == "MQTT_PORT")   { commCfg.mqttPort     = (uint16_t)val.toInt(); Serial.println("[Config] MQTT port -> " + val); }
  else if (key == "MQTT_USER")   { commCfg.mqttUser     = val; Serial.println("[Config] MQTT user -> " + val); }
  else if (key == "MQTT_PASS")   { commCfg.mqttPass     = val; Serial.println("[Config] MQTT password updated"); }
  else if (key == "MQTT_CID")    { commCfg.mqttClientId = val; Serial.println("[Config] MQTT client ID -> " + val); }
  else if (key == "MQTT_TLS") {
    if      (vu == "ON"  || vu == "1") { commCfg.mqttTLS = true;  Serial.println("[Config] MQTT TLS ON");  }
    else if (vu == "OFF" || vu == "0") { commCfg.mqttTLS = false; Serial.println("[Config] MQTT TLS OFF"); }
    else { Serial.println("[Config] MQTT_TLS must be ON or OFF"); return true; }
  }
  else if (key == "SMS_PHONE1")  { commCfg.smsPhone1       = val; Serial.println("[Config] SMS phone 1 -> " + val); }
  else if (key == "SMS_PHONE2")  { commCfg.smsPhone2       = val; Serial.println("[Config] SMS phone 2 -> " + val); }
  else if (key == "BLE_NAME")    { commCfg.bleName         = val; Serial.println("[Config] BLE name -> " + val); }
  else if (key == "LORA_FREQ")   { commCfg.loraFrequencyHz = (uint32_t)val.toInt(); Serial.println("[Config] LoRa freq -> " + val + " Hz"); }
  else if (key == "HTTP_PORT")   { commCfg.httpPort        = (uint16_t)val.toInt(); Serial.println("[Config] HTTP port -> " + val); }
  else {
    Serial.println("[Config] Unknown key: " + key + "  (type CONFIG HELP)");
    return true;
  }
  Serial.println("[Config] Run SAVE CONFIG to persist. Reboot to apply.");
  return true;
}


// ── SETUP WTT / SETUP IRR / SETUP SHOW / SETUP DEL ───────────────────────────
// Serial-only. Creates and persists process group configurations.
// Group IDs are immutable after setup.
//
// SETUP WTT ID:FG1,W:W1,T:T1         create WTT group
// SETUP IRR ID:IG1,G:G1,N:15,V:4,M:1 create irrigation group
// SETUP SHOW                          list all configured groups
// SETUP DEL <id>                      delete a group config
bool SerialConfigHandler::handleSetup(const String &up, const String &raw) {
  String body = up.substring(6); body.trim();  // after "SETUP "

  // ── SETUP SHOW ─────────────────────────────────────────────────────────
  if (body == "SHOW") {
    if (_onSetupShow) Serial.println(_onSetupShow());
    else              Serial.println("[Setup] No show callback");
    return true;
  }

  // ── SETUP DEL <id> ──────────────────────────────────────────────────────
  if (body.startsWith("DEL ")) {
    String id = body.substring(4); id.trim();
    if (_storage.deleteProcessConfig(id)) {
      Serial.println("[Setup] ✓ Deleted config: " + id);
      if (_onSetupDel) Serial.println(_onSetupDel(id));
    } else {
      Serial.println("[Setup] Not found: " + id);
    }
    return true;
  }

  // ── SETUP WTT ID:x,W:W1,T:T1 ───────────────────────────────────────────
  if (body.startsWith("WTT ")) {
    String params = body.substring(4); params.trim();
    WTTGroupConfig cfg;

    // Parse key:value pairs
    int pos = 0;
    while (pos < (int)params.length()) {
      int comma = params.indexOf(',', pos);
      String tok = (comma < 0) ? params.substring(pos) : params.substring(pos, comma);
      tok.trim();
      int col = tok.indexOf(':');
      if (col > 0) {
        String k = tok.substring(0, col); k.toUpperCase();
        String v = tok.substring(col + 1); v.trim();
        if (k == "ID") cfg.id     = v;
        if (k == "W")  cfg.pumpId = v;
        if (k == "T")  cfg.tankId = v;
      }
      if (comma < 0) break;
      pos = comma + 1;
    }

    if (!cfg.isValid()) {
      Serial.println("[Setup] ❌ WTT requires ID:, W: and T: fields");
      Serial.println("[Setup]    Example: SETUP WTT ID:FG1,W:W1,T:T1");
      return true;
    }
    // Validate values
    if (cfg.pumpId != "W1" && cfg.pumpId != "W2") {
      Serial.println("[Setup] ❌ W: must be W1 or W2"); return true;
    }
    if (cfg.tankId != "T1" && cfg.tankId != "T2") {
      Serial.println("[Setup] ❌ T: must be T1 or T2"); return true;
    }

    cfg.configured = true;
    _storage.saveWTTConfig(cfg);
    Serial.println("[Setup] ✓ WTT group saved: " + cfg.id
                   + " pump=" + cfg.pumpId + " tank=" + cfg.tankId);
    if (_onWTTSetup) Serial.println(_onWTTSetup(cfg));
    Serial.println("[Setup] Group " + cfg.id + " is now available on all channels.");
    return true;
  }

  // ── SETUP NODE <groupId>,N:<node>,V:<v1>,<v2>... ──────────────────────
  // Add a node and its participating valves to an irrigation group.
  // Run before SETUP IRR to define node membership.
  if (body.startsWith("NODE ")) {
    String params = body.substring(5); params.trim();

    // SETUP NODE DEL <groupId>,N:<node>
    if (params.toUpperCase().startsWith("DEL ")) {
      String rest = params.substring(4); rest.trim();
      int cm = rest.indexOf(',');
      String gid = (cm > 0) ? rest.substring(0, cm) : rest;
      uint8_t nid = 0;
      if (cm > 0) {
        String npart = rest.substring(cm+1); npart.trim(); npart.toUpperCase();
        if (npart.startsWith("N:")) nid = (uint8_t)npart.substring(2).toInt();
      }
      if (gid.length() && nid > 0) {
        _storage.deleteIrrNode(gid, nid);
        Serial.println("[Setup] ✓ Removed node " + String(nid) + " from " + gid);
      } else {
        Serial.println("[Setup] Usage: SETUP NODE DEL <id>,N:<node>");
      }
      return true;
    }

    // Parse: <groupId>,N:<node>,V:<v1>,<v2>,...
    // Example: IG1,N:1,V:2,3   or   IG1,N:2,V:4
    String groupId;
    uint8_t nodeId = 0;
    uint8_t valves[MAX_VALVES_PER_NODE] = {};
    uint8_t valveCount = 0;

    int pos = 0;
    bool parsingValves = false;
    while (pos < (int)params.length()) {
      int comma = params.indexOf(',', pos);
      String tok = (comma < 0) ? params.substring(pos) : params.substring(pos, comma);
      tok.trim();
      String tokU = tok; tokU.toUpperCase();
      int col = tok.indexOf(':');

      if (groupId.length() == 0 && col < 0) {
        groupId = tok;  // first bare token = group ID
      } else if (tokU.startsWith("N:")) {
        nodeId = (uint8_t)tok.substring(2).toInt();
        parsingValves = false;
      } else if (tokU.startsWith("V:")) {
        // V:2 — first valve after V:
        uint8_t v = (uint8_t)tok.substring(2).toInt();
        if (v > 0 && valveCount < MAX_VALVES_PER_NODE) valves[valveCount++] = v;
        parsingValves = true;
      } else if (parsingValves && isdigit(tok.charAt(0))) {
        // Additional valve numbers after V:
        uint8_t v = (uint8_t)tok.toInt();
        if (v > 0 && valveCount < MAX_VALVES_PER_NODE) valves[valveCount++] = v;
      }
      if (comma < 0) break;
      pos = comma + 1;
    }

    if (groupId.length() == 0 || nodeId == 0 || valveCount == 0) {
      Serial.println("[Setup] ❌ Usage: SETUP NODE <id>,N:<node>,V:<v1>,<v2>...");
      Serial.println("[Setup]   Example: SETUP NODE IG1,N:1,V:2,3");
      return true;
    }
    if (nodeId < 1 || nodeId > 15) {
      Serial.println("[Setup] ❌ Node must be 1-15"); return true;
    }

    // Load existing nodes, update/add this node, save
    IrrGroupConfig tmp; tmp.id = groupId;
    _storage.loadIrrNodes(groupId, tmp);  // ok if not found yet
    IrrNodeEntry *existing = tmp.findNode(nodeId);
    if (existing) {
      // Replace valves
      existing->valveCount = 0;
      for (int i = 0; i < valveCount; i++) existing->addValve(valves[i]);
    } else {
      if (tmp.nodeCount >= MAX_NODES_PER_GROUP) {
        Serial.println("[Setup] ❌ Max nodes reached"); return true;
      }
      IrrNodeEntry &ne = tmp.nodes[tmp.nodeCount++];
      ne.nodeId = nodeId;
      for (int i = 0; i < valveCount; i++) ne.addValve(valves[i]);
    }
    _storage.saveIrrNodes(groupId, tmp);
    String valveList;
    for (int i = 0; i < valveCount; i++) { if (i) valveList += ","; valveList += valves[i]; }
    Serial.println("[Setup] ✓ Node " + String(nodeId) + " valves=[" + valveList + "] → " + groupId);
    Serial.println("[Setup] " + groupId + " has " + String(tmp.nodeCount) + " node(s) configured.");
    return true;
  }

  // ── SETUP IRR ID:x,G:G1,M:1 ──────────────────────────────────────────
  // Creates the irrigation group referencing previously defined nodes.
  if (body.startsWith("IRR ")) {
    String params = body.substring(4); params.trim();
    IrrGroupConfig cfg;

    int pos = 0;
    while (pos < (int)params.length()) {
      int comma = params.indexOf(',', pos);
      String tok = (comma < 0) ? params.substring(pos) : params.substring(pos, comma);
      tok.trim();
      int col = tok.indexOf(':');
      if (col > 0) {
        String k = tok.substring(0, col); k.toUpperCase();
        String v = tok.substring(col + 1); v.trim();
        if (k == "ID") cfg.id       = v;
        if (k == "G")  cfg.pumpId   = v;
        if (k == "M")  cfg.minValves= (uint8_t)v.toInt();
      }
      if (comma < 0) break;
      pos = comma + 1;
    }

    if (!cfg.isValid()) {
      Serial.println("[Setup] ❌ IRR requires ID: and G: fields");
      Serial.println("[Setup]    Example: SETUP IRR ID:IG1,G:G1,M:1");
      return true;
    }
    if (cfg.pumpId != "G1" && cfg.pumpId != "G2") {
      Serial.println("[Setup] ❌ G: must be G1 or G2"); return true;
    }

    // Load node definitions
    _storage.loadIrrNodes(cfg.id, cfg);
    if (cfg.nodeCount == 0) {
      Serial.println("[Setup] ⚠ No nodes defined yet. Use SETUP NODE first.");
      Serial.println("[Setup]   Example: SETUP NODE " + cfg.id + ",N:1,V:1,2,3,4");
    }

    cfg.configured = true;
    _storage.saveIrrConfig(cfg);

    // Print node summary
    Serial.println("[Setup] ✓ IRR group: " + cfg.id + " pump=" + cfg.pumpId
                   + " minValves=" + cfg.minValves
                   + " nodes=" + cfg.nodeCount);
    for (int i = 0; i < cfg.nodeCount; i++) {
      String vl;
      for (int v = 0; v < cfg.nodes[i].valveCount; v++) {
        if (v) vl += ",";
        vl += cfg.nodes[i].valves[v];
      }
      Serial.println("[Setup]   Node " + String(cfg.nodes[i].nodeId) + " valves=[" + vl + "]");
    }
    if (_onIrrSetup) Serial.println(_onIrrSetup(cfg));
    Serial.println("[Setup] Group " + cfg.id + " is now available on all channels.");
    return true;
  }

  Serial.println("[Setup] Unknown SETUP command.");
  Serial.println("[Setup] Commands:");
  Serial.println("[Setup]   SETUP WTT ID:FG1,W:W1,T:T1");
  Serial.println("[Setup]   SETUP NODE IG1,N:1,V:2,3");
  Serial.println("[Setup]   SETUP NODE IG1,N:2,V:4");
  Serial.println("[Setup]   SETUP IRR  ID:IG1,G:G1,M:1");
  Serial.println("[Setup]   SETUP SHOW");
  Serial.println("[Setup]   SETUP DEL <id>");
  return true;
}

void SerialConfigHandler::printHelp() const {
  Serial.println(F(
    "\n=== CONFIG COMMANDS ===\n"
    "SHOW CONFIG                   Print current config\n"
    "SAVE CONFIG                   Save to LittleFS flash\n"
    "RESET CONFIG                  Restore firmware defaults\n"
    "RESTART                       Reboot the controller\n"
    "FACTORY RESET                 Clear all config and reboot\n"
    "\n"
    "--- Active Channel (mutually exclusive) ---\n"
    "SET CHANNEL SMS               Use SMS as active channel\n"
    "SET CHANNEL MQTT              Use MQTT as active channel\n"
    "SET CHANNEL HTTP              Use HTTP as active channel\n"
    "SET CHANNEL NONE              Disable outbound alerts\n"
    "\n"
    "--- LoRa + Bluetooth Channels ---\n"
    "ENABLE  LORA                  Enable LoRa (primary, default ON)\n"
    "DISABLE LORA                  Disable LoRa\n"
    "ENABLE  BLE                   Enable Bluetooth (fallback, default OFF)\n"
    "DISABLE BLE                   Disable Bluetooth\n"
    "(Serial is always ON)\n"
    "\n"
    "--- Internet Bearer (for MQTT/HTTP) ---\n"
    "ENABLE  PPPOS                 Enable PPPoS cellular (primary)\n"
    "DISABLE PPPOS                 Disable PPPoS\n"
    "ENABLE  WIFI                  Enable WiFi (fallback)\n"
    "DISABLE WIFI                  Disable WiFi\n"
    "\n"
    "--- Credentials ---\n"
    "SET WIFI_SSID <ssid>          WiFi network name\n"
    "SET WIFI_PASS <pass>          WiFi password\n"
    "SET APN       <apn>           Cellular APN\n"
    "SET MQTT_BROKER <host>        MQTT broker hostname\n"
    "SET MQTT_PORT   <n>           MQTT port\n"
    "SET MQTT_USER   <user>        MQTT username\n"
    "SET MQTT_PASS   <pass>        MQTT password\n"
    "SET MQTT_CID    <id>          MQTT client ID\n"
    "SET MQTT_TLS    ON|OFF        MQTT TLS/SSL\n"
    "SET SMS_PHONE1  +<n>          Primary admin phone\n"
    "SET SMS_PHONE2  +<n>          Secondary admin phone\n"
    "SET BLE_NAME    <n>           Bluetooth device name\n"
    "SET LORA_FREQ   <Hz>          LoRa frequency\n"
    "SET HTTP_PORT   <n>           HTTP API port\n"
    "\n"
    "=== PROCESS GROUP SETUP (Serial only) ===\n"
    "SETUP WTT ID:<id>,W:W1|W2,T:T1|T2      Create WTT group\n"
    "SETUP NODE <id>,N:<node>,V:<v1>,<v2>...  Add node+valves to group\n"
    "SETUP NODE DEL <id>,N:<node>             Remove node from group\n"
    "SETUP IRR  ID:<id>,G:G1|G2[,M:1]        Create irrigation group\n"
    "SETUP SHOW                              List all groups\n"
    "SETUP DEL <id>                          Delete group config\n"
    "Note: ID is permanent. Use SETUP DEL + re-create to change.\n"
    "=======================\n"
  ));
}
