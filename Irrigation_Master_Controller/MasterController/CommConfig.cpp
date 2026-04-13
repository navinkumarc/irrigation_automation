// CommConfig.cpp  —  Runtime communication configuration
// No persistence here. StorageManager owns load/save.
#include "Config.h"
#include "CommConfig.h"

// Global instance — constructed with Config.h defaults,
// overwritten by StorageManager::loadCommConfig() at boot.
CommConfig commCfg;

void CommConfig::print() const {
  Serial.println(F("\n=== COMMUNICATION CONFIGURATION ==="));

  Serial.println(F("-- Active Channel (mutually exclusive) --"));
  Serial.printf ("  Active    : %s\n", activeChannelName());
  Serial.printf ("  (SMS / MQTT / HTTP — only one at a time)\n");

  Serial.println(F("-- Independent Channels --"));
  Serial.printf ("  LoRa      : %s (primary)\n",  chLoRa      ? "ENABLED" : "DISABLED");
  Serial.printf ("  Bluetooth : %s (fallback)\n", chBluetooth ? "ENABLED" : "DISABLED");
  Serial.println(F("  Serial    : ALWAYS ON (not configurable)"));

  if (needsInternet()) {
    Serial.println(F("-- Internet Bearer --"));
    Serial.printf ("  PPPoS     : %s (primary)\n", enablePPPoS ? "ENABLED" : "DISABLED");
    Serial.printf ("  WiFi      : %s (fallback)\n", enableWiFi  ? "ENABLED" : "DISABLED");
    Serial.printf ("  WiFi SSID : %s\n", wifiSSID.c_str());
    Serial.printf ("  Cell APN  : %s\n", cellularAPN.c_str());
  }

  if (activeChannel == ActiveChannel::MQTT) {
    Serial.println(F("-- MQTT --"));
    Serial.printf ("  Broker    : %s\n", mqttBroker.c_str());
    Serial.printf ("  Port      : %u\n", mqttPort);
    Serial.printf ("  User      : %s\n", mqttUser.c_str());
    Serial.printf ("  Client ID : %s\n", mqttClientId.c_str());
    Serial.printf ("  TLS       : %s\n", mqttTLS ? "ON" : "OFF");
  }

  if (activeChannel == ActiveChannel::HTTP) {
    Serial.println(F("-- HTTP --"));
    Serial.printf ("  Port      : %u\n", httpPort);
  }

  if (activeChannel == ActiveChannel::SMS) {
    Serial.println(F("-- SMS --"));
    Serial.printf ("  Phone 1   : %s\n", smsPhone1.c_str());
    Serial.printf ("  Phone 2   : %s\n", smsPhone2.length() ? smsPhone2.c_str() : "(none)");
  }

  Serial.println(F("-- Bluetooth --"));
  Serial.printf ("  Name      : %s\n", bleName.c_str());

  Serial.println(F("-- LoRa --"));
  Serial.printf ("  Frequency : %lu Hz\n", (unsigned long)loraFrequencyHz);

  Serial.println(F("===================================\n"));
}
