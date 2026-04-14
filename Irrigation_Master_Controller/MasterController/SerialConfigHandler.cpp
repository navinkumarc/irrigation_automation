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
    "=======================\n"
  ));
}
