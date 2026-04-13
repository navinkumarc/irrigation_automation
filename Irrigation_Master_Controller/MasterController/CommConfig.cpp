// CommConfig.cpp  —  Runtime communication configuration persistence
#include "Config.h"
#include "CommConfig.h"

CommConfig commCfg;   // global instance, default-constructed from Config.h macros

// NVS key names (max 15 chars each)
static const char* NS        = "commcfg";
static const char* K_CH_SMS  = "ch_sms";
static const char* K_CH_DATA = "ch_data";
static const char* K_CH_BT   = "ch_bt";
static const char* K_CH_LORA = "ch_lora";
static const char* K_BEARER  = "bearer";
static const char* K_W_SSID  = "wifi_ssid";
static const char* K_W_PASS  = "wifi_pass";
static const char* K_APN     = "cell_apn";
static const char* K_MB      = "mqtt_broker";
static const char* K_MP      = "mqtt_port";
static const char* K_MU      = "mqtt_user";
static const char* K_MPASS   = "mqtt_pass";
static const char* K_MCID    = "mqtt_cid";
static const char* K_MTLS    = "mqtt_tls";
static const char* K_P1      = "sms_p1";
static const char* K_P2      = "sms_p2";
static const char* K_BNAME   = "ble_name";
static const char* K_LFREQ   = "lora_freq";
static const char* K_HPORT   = "http_port";

void CommConfig::load(Preferences &prefs) {
  prefs.begin(NS, true);   // read-only
  chSMS             = prefs.getBool  (K_CH_SMS,  chSMS);
  chData            = prefs.getBool  (K_CH_DATA, chData);
  chBluetooth       = prefs.getBool  (K_CH_BT,   chBluetooth);
  chLoRa            = prefs.getBool  (K_CH_LORA, chLoRa);
  dataBearerPrimary = prefs.getUChar (K_BEARER,  dataBearerPrimary);
  wifiSSID          = prefs.getString(K_W_SSID,  wifiSSID);
  wifiPass          = prefs.getString(K_W_PASS,  wifiPass);
  cellularAPN       = prefs.getString(K_APN,     cellularAPN);
  mqttBroker        = prefs.getString(K_MB,      mqttBroker);
  mqttPort          = prefs.getUShort(K_MP,      mqttPort);
  mqttUser          = prefs.getString(K_MU,      mqttUser);
  mqttPass          = prefs.getString(K_MPASS,   mqttPass);
  mqttClientId      = prefs.getString(K_MCID,    mqttClientId);
  mqttTLS           = prefs.getBool  (K_MTLS,    mqttTLS);
  smsPhone1         = prefs.getString(K_P1,      smsPhone1);
  smsPhone2         = prefs.getString(K_P2,      smsPhone2);
  bleName           = prefs.getString(K_BNAME,   bleName);
  loraFrequencyHz   = prefs.getULong (K_LFREQ,   loraFrequencyHz);
  httpPort          = prefs.getUShort(K_HPORT,   httpPort);
  prefs.end();
  Serial.println("[CommCfg] Config loaded from NVS");
}

void CommConfig::save(Preferences &prefs) const {
  prefs.begin(NS, false);  // read-write
  prefs.putBool  (K_CH_SMS,  chSMS);
  prefs.putBool  (K_CH_DATA, chData);
  prefs.putBool  (K_CH_BT,   chBluetooth);
  prefs.putBool  (K_CH_LORA, chLoRa);
  prefs.putUChar (K_BEARER,  dataBearerPrimary);
  prefs.putString(K_W_SSID,  wifiSSID);
  prefs.putString(K_W_PASS,  wifiPass);
  prefs.putString(K_APN,     cellularAPN);
  prefs.putString(K_MB,      mqttBroker);
  prefs.putUShort(K_MP,      mqttPort);
  prefs.putString(K_MU,      mqttUser);
  prefs.putString(K_MPASS,   mqttPass);
  prefs.putString(K_MCID,    mqttClientId);
  prefs.putBool  (K_MTLS,    mqttTLS);
  prefs.putString(K_P1,      smsPhone1);
  prefs.putString(K_P2,      smsPhone2);
  prefs.putString(K_BNAME,   bleName);
  prefs.putULong (K_LFREQ,   loraFrequencyHz);
  prefs.putUShort(K_HPORT,   httpPort);
  prefs.end();
  Serial.println("[CommCfg] Config saved to NVS");
}

void CommConfig::reset(Preferences &prefs) {
  *this = CommConfig();   // rebuild from Config.h defaults
  save(prefs);
  Serial.println("[CommCfg] Config reset to firmware defaults");
}

void CommConfig::print() const {
  Serial.println(F("\n=== COMMUNICATION CONFIGURATION ==="));
  Serial.println(F("-- User Channels --"));
  Serial.printf("  SMS       : %s\n", chSMS       ? "ENABLED"  : "DISABLED");
  Serial.printf("  Data/MQTT : %s\n", chData      ? "ENABLED"  : "DISABLED");
  Serial.printf("  Bluetooth : %s\n", chBluetooth ? "ENABLED"  : "DISABLED");
  Serial.printf("  LoRa      : %s\n", chLoRa      ? "ENABLED"  : "DISABLED");
  Serial.println(F("  Serial    : ALWAYS ON (config console)"));
  Serial.println(F("-- Data Bearer (for MQTT/HTTP) --"));
  Serial.printf("  Primary   : %s\n", bearerName());
  Serial.printf("  WiFi SSID : %s\n", wifiSSID.c_str());
  Serial.printf("  Cell APN  : %s\n", cellularAPN.c_str());
  Serial.println(F("-- MQTT --"));
  Serial.printf("  Broker    : %s\n", mqttBroker.c_str());
  Serial.printf("  Port      : %u\n", mqttPort);
  Serial.printf("  User      : %s\n", mqttUser.c_str());
  Serial.printf("  Client ID : %s\n", mqttClientId.c_str());
  Serial.printf("  TLS       : %s\n", mqttTLS ? "ON" : "OFF");
  Serial.println(F("-- SMS --"));
  Serial.printf("  Phone 1   : %s\n", smsPhone1.c_str());
  Serial.printf("  Phone 2   : %s\n", smsPhone2.length() ? smsPhone2.c_str() : "(none)");
  Serial.println(F("-- Bluetooth --"));
  Serial.printf("  Name      : %s\n", bleName.c_str());
  Serial.println(F("-- LoRa --"));
  Serial.printf("  Frequency : %lu Hz\n", (unsigned long)loraFrequencyHz);
  Serial.println(F("-- HTTP --"));
  Serial.printf("  Port      : %u\n", httpPort);
  Serial.println(F("===================================\n"));
}
