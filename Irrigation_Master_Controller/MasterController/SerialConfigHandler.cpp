// SerialConfigHandler.cpp  —  Serial config console
// All persistence delegated to StorageManager.
#include "Config.h"
#include "SerialConfigHandler.h"

bool SerialConfigHandler::handle(const String &line) {
  String up = line;
  up.trim();
  up.toUpperCase();

  if (up == "CONFIG HELP" || up == "HELP CONFIG") { printHelp(); return true; }
  if (up == "SHOW CONFIG"  || up == "CONFIG")     { commCfg.print(); return true; }

  if (up == "SAVE CONFIG") {
    _storage.saveCommConfig(commCfg);
    Serial.println("[Config] Changes will take effect on next reboot.");
    return true;
  }
  if (up == "RESET CONFIG") {
    Serial.println("[Config] Resetting to firmware defaults...");
    _storage.resetCommConfig(commCfg);
    commCfg.print();
    return true;
  }
  if (up.startsWith("ENABLE "))  return handleEnable (up, line);
  if (up.startsWith("DISABLE ")) return handleDisable(up, line);
  if (up.startsWith("SET "))     return handleSet    (up, line);
  return false;
}

bool SerialConfigHandler::handleEnable(const String &up, const String &) {
  String ch = up.substring(7); ch.trim();

  if      (ch == "SMS")                       { commCfg.chSMS       = true; Serial.println("[Config] SMS channel ENABLED"); }
  else if (ch == "DATA" || ch == "MQTT")      { commCfg.chData      = true; Serial.println("[Config] Data/MQTT channel ENABLED"); }
  else if (ch == "BLE"  || ch == "BLUETOOTH") { commCfg.chBluetooth = true; Serial.println("[Config] Bluetooth channel ENABLED"); }
  else if (ch == "LORA")                      { commCfg.chLoRa      = true; Serial.println("[Config] LoRa channel ENABLED"); }
  else if (ch == "SERIAL")                    { Serial.println("[Config] Serial is always ON — cannot be disabled"); return true; }
  else { Serial.println("[Config] Unknown channel: " + ch + "  (SMS | DATA | BLE | LORA)"); return true; }

  Serial.println("[Config] Run SAVE CONFIG to persist. Reboot to apply.");
  return true;
}

bool SerialConfigHandler::handleDisable(const String &up, const String &) {
  String ch = up.substring(8); ch.trim();

  if      (ch == "SMS")                       { commCfg.chSMS       = false; Serial.println("[Config] SMS channel DISABLED"); }
  else if (ch == "DATA" || ch == "MQTT")      { commCfg.chData      = false; Serial.println("[Config] Data/MQTT channel DISABLED"); }
  else if (ch == "BLE"  || ch == "BLUETOOTH") { commCfg.chBluetooth = false; Serial.println("[Config] Bluetooth channel DISABLED"); }
  else if (ch == "LORA")                      { commCfg.chLoRa      = false; Serial.println("[Config] LoRa channel DISABLED"); }
  else if (ch == "SERIAL")                    { Serial.println("[Config] Serial cannot be disabled — it is the config console"); return true; }
  else { Serial.println("[Config] Unknown channel: " + ch + "  (SMS | DATA | BLE | LORA)"); return true; }

  Serial.println("[Config] Run SAVE CONFIG to persist. Reboot to apply.");
  return true;
}

bool SerialConfigHandler::handleSet(const String &up, const String &raw) {
  String body = up.substring(4); body.trim();
  int sp = body.indexOf(' ');
  if (sp <= 0) { Serial.println("[Config] Usage: SET <KEY> <value>"); return true; }

  String key = body.substring(0, sp); key.trim();

  // Preserve original case for value (passwords, SSIDs, names, URLs)
  String rawBody = raw.substring(4); rawBody.trim();
  int rsp = rawBody.indexOf(' ');
  String val = (rsp > 0) ? rawBody.substring(rsp + 1) : "";
  val.trim();

  if (val.length() == 0) { Serial.println("[Config] Value cannot be empty"); return true; }
  String vu = val; vu.toUpperCase();

  if (key == "BEARER") {
    if      (vu == "WIFI")  { commCfg.dataBearerPrimary = DATA_BEARER_WIFI;  Serial.println("[Config] Bearer -> WiFi");  }
    else if (vu == "PPPOS") { commCfg.dataBearerPrimary = DATA_BEARER_PPPOS; Serial.println("[Config] Bearer -> PPPoS"); }
    else { Serial.println("[Config] Bearer must be WIFI or PPPOS"); return true; }
  }
  else if (key == "WIFI_SSID")   { commCfg.wifiSSID      = val; Serial.println("[Config] WiFi SSID updated"); }
  else if (key == "WIFI_PASS")   { commCfg.wifiPass      = val; Serial.println("[Config] WiFi password updated"); }
  else if (key == "APN")         { commCfg.cellularAPN   = val; Serial.println("[Config] APN -> " + val); }
  else if (key == "MQTT_BROKER") { commCfg.mqttBroker    = val; Serial.println("[Config] MQTT broker -> " + val); }
  else if (key == "MQTT_PORT")   { commCfg.mqttPort      = (uint16_t)val.toInt(); Serial.println("[Config] MQTT port -> " + val); }
  else if (key == "MQTT_USER")   { commCfg.mqttUser      = val; Serial.println("[Config] MQTT user -> " + val); }
  else if (key == "MQTT_PASS")   { commCfg.mqttPass      = val; Serial.println("[Config] MQTT password updated"); }
  else if (key == "MQTT_CID")    { commCfg.mqttClientId  = val; Serial.println("[Config] MQTT client ID -> " + val); }
  else if (key == "MQTT_TLS") {
    if      (vu == "ON"  || vu == "YES" || vu == "1") { commCfg.mqttTLS = true;  Serial.println("[Config] MQTT TLS ON");  }
    else if (vu == "OFF" || vu == "NO"  || vu == "0") { commCfg.mqttTLS = false; Serial.println("[Config] MQTT TLS OFF"); }
    else { Serial.println("[Config] MQTT_TLS must be ON or OFF"); return true; }
  }
  else if (key == "SMS_PHONE1")  { commCfg.smsPhone1     = val; Serial.println("[Config] SMS phone 1 -> " + val); }
  else if (key == "SMS_PHONE2")  { commCfg.smsPhone2     = val; Serial.println("[Config] SMS phone 2 -> " + val); }
  else if (key == "BLE_NAME")    { commCfg.bleName       = val; Serial.println("[Config] BLE name -> " + val); }
  else if (key == "LORA_FREQ")   { commCfg.loraFrequencyHz = (uint32_t)val.toInt(); Serial.println("[Config] LoRa freq -> " + val + " Hz"); }
  else if (key == "HTTP_PORT")   { commCfg.httpPort      = (uint16_t)val.toInt(); Serial.println("[Config] HTTP port -> " + val); }
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
    "SHOW CONFIG               Print current config\n"
    "SAVE CONFIG               Save to LittleFS flash\n"
    "RESET CONFIG              Restore firmware defaults\n"
    "\n"
    "ENABLE  SMS|DATA|BLE|LORA Enable a user channel\n"
    "DISABLE SMS|DATA|BLE|LORA Disable a user channel\n"
    "(Serial is always on and cannot be disabled)\n"
    "\n"
    "SET BEARER    WIFI|PPPOS  Data bearer for MQTT/HTTP\n"
    "SET WIFI_SSID <ssid>      WiFi network name\n"
    "SET WIFI_PASS <pass>      WiFi password\n"
    "SET APN       <apn>       Cellular APN\n"
    "\n"
    "SET MQTT_BROKER <host>    MQTT broker hostname\n"
    "SET MQTT_PORT   <n>       MQTT port (default 8883)\n"
    "SET MQTT_USER   <user>    MQTT username\n"
    "SET MQTT_PASS   <pass>    MQTT password\n"
    "SET MQTT_CID    <id>      MQTT client ID\n"
    "SET MQTT_TLS    ON|OFF    MQTT TLS/SSL\n"
    "\n"
    "SET SMS_PHONE1  +<n>      Primary admin SMS number\n"
    "SET SMS_PHONE2  +<n>      Secondary admin number\n"
    "\n"
    "SET BLE_NAME    <n>       Bluetooth device name\n"
    "SET LORA_FREQ   <Hz>      LoRa frequency in Hz\n"
    "SET HTTP_PORT   <n>       HTTP API port\n"
    "=======================\n"
  ));
}
