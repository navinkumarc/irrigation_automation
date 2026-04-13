// CommConfig.cpp  —  Runtime communication configuration
// No persistence here. StorageManager owns load/save.
#include "Config.h"
#include "CommConfig.h"

// Global instance — constructed with Config.h defaults,
// overwritten by StorageManager::loadCommConfig() at boot.
CommConfig commCfg;

void CommConfig::print() const {
  Serial.println(F("\n=== COMMUNICATION CONFIGURATION ==="));
  Serial.println(F("-- User Channels --"));
  Serial.printf("  SMS       : %s\n", chSMS       ? "ENABLED" : "DISABLED");
  Serial.printf("  Internet  : %s\n", chInternet  ? "ENABLED" : "DISABLED");
  Serial.printf("  Bluetooth : %s\n", chBluetooth ? "ENABLED" : "DISABLED");
  Serial.printf("  LoRa      : %s\n", chLoRa      ? "ENABLED" : "DISABLED");
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
