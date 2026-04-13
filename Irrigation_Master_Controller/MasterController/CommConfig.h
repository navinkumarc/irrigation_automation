// CommConfig.h  —  Runtime communication configuration (in-memory struct)
//
// CommConfig holds all runtime-editable comm settings.
// NO persistence logic here — StorageManager owns load/save entirely.
//
// ── Channel model ────────────────────────────────────────────────────────────
//
//   Mutually exclusive (only ONE active at a time):
//     SMS    — modem AT mode, text alerts and commands
//     MQTT   — MQTT over internet bearer (WiFi or PPPoS)
//     HTTP   — REST API over internet bearer (WiFi or PPPoS)
//
//   Independent:
//     Bluetooth — BLE notify/write, always enabled by default
//     LoRa      — user broadcast, independent enable/disable
//     Serial    — always ON, config console, NOT stored here
//
// ── Internet bearer (only used when MQTT or HTTP is active) ──────────────────
//   PPPoS  — primary cellular data via modem
//   WiFi   — fallback / alternative Wi-Fi
//   Both can be enabled; PPPoS is primary, WiFi is fallback.
//
// ── Lifecycle ────────────────────────────────────────────────────────────────
//   1. storage.loadCommConfig(commCfg)   — setup(), before commMgr.begin()
//   2. SerialConfigHandler mutates commCfg fields in memory
//   3. storage.saveCommConfig(commCfg)   — on "SAVE CONFIG"
//   4. storage.resetCommConfig(commCfg)  — on "RESET CONFIG"

#ifndef COMM_CONFIG_H
#define COMM_CONFIG_H

#include <Arduino.h>
#include "Config.h"

// ── Active channel (mutually exclusive group) ─────────────────────────────────
enum class ActiveChannel : uint8_t {
  NONE = 0,
  SMS  = 1,
  MQTT = 2,
  HTTP = 3
};

struct CommConfig {

  // ── Mutually exclusive active channel ────────────────────────────────────
  ActiveChannel activeChannel;   // SMS | MQTT | HTTP (only one at a time)

  // ── Independent channels ──────────────────────────────────────────────────
  bool chLoRa;        // LoRa — default ON (user + node channel)
  bool chBluetooth;   // Bluetooth — default OFF, enable via ENABLE BLE

  // ── Internet bearer (used when activeChannel == MQTT or HTTP) ────────────
  bool enablePPPoS;   // Enable PPPoS (cellular) as primary bearer
  bool enableWiFi;    // Enable WiFi as fallback bearer

  // ── WiFi credentials ──────────────────────────────────────────────────────
  String wifiSSID;
  String wifiPass;

  // ── PPPoS / cellular ──────────────────────────────────────────────────────
  String cellularAPN;

  // ── MQTT settings ─────────────────────────────────────────────────────────
  String   mqttBroker;
  uint16_t mqttPort;
  String   mqttUser;
  String   mqttPass;
  String   mqttClientId;
  bool     mqttTLS;

  // ── SMS settings ──────────────────────────────────────────────────────────
  String smsPhone1;   // Primary admin phone (E.164)
  String smsPhone2;   // Secondary admin phone (empty = disabled)

  // ── Bluetooth settings ────────────────────────────────────────────────────
  String bleName;

  // ── LoRa settings ─────────────────────────────────────────────────────────
  uint32_t loraFrequencyHz;

  // ── HTTP settings ─────────────────────────────────────────────────────────
  uint16_t httpPort;

  // ── Constructor: populate from Config.h compile-time defaults ─────────────
  CommConfig() {
    // Default active channel: SMS (or MQTT if SMS not compiled in)
    activeChannel   = (ENABLE_SMS  == 1) ? ActiveChannel::SMS  :
                      (ENABLE_MQTT == 1) ? ActiveChannel::MQTT :
                      (ENABLE_HTTP == 1) ? ActiveChannel::HTTP :
                                           ActiveChannel::NONE;

    chLoRa          = (ENABLE_LORA == 1);     // ON when hardware compiled in
    chBluetooth     = false;                  // OFF by default — enable via Serial

    // Bearer defaults: PPPoS primary if compiled, WiFi as fallback
    enablePPPoS     = (ENABLE_PPPOS == 1);
    enableWiFi      = (ENABLE_WIFI  == 1);

    wifiSSID        = WIFI_SSID;
    wifiPass        = WIFI_PASS;
    cellularAPN     = PPPOS_APN;

    mqttBroker      = MQTT_BROKER;
    mqttPort        = MQTT_PORT;
    mqttUser        = MQTT_USER;
    mqttPass        = MQTT_PASS;
    mqttClientId    = MQTT_CLIENT_ID;
    mqttTLS         = (MQTT_USE_SSL == 1);

    smsPhone1       = SMS_ALERT_PHONE_1;
    smsPhone2       = SMS_ALERT_PHONE_2;

    bleName         = BLE_DEVICE_NAME;
    loraFrequencyHz = RF_FREQUENCY;
    httpPort        = HTTP_SERVER_PORT;
  }

  // ── Channel query helpers ─────────────────────────────────────────────────
  bool isSMS()  const { return activeChannel == ActiveChannel::SMS;  }
  bool isMQTT() const { return activeChannel == ActiveChannel::MQTT; }
  bool isHTTP() const { return activeChannel == ActiveChannel::HTTP; }

  // Returns true when an internet bearer is needed
  bool needsInternet() const {
    return activeChannel == ActiveChannel::MQTT ||
           activeChannel == ActiveChannel::HTTP;
  }

  const char* activeChannelName() const {
    switch (activeChannel) {
      case ActiveChannel::SMS:  return "SMS";
      case ActiveChannel::MQTT: return "MQTT";
      case ActiveChannel::HTTP: return "HTTP";
      default:                  return "NONE";
    }
  }

  void print() const;
};

// Global instance — defaults from constructor, overwritten by StorageManager
extern CommConfig commCfg;

#endif // COMM_CONFIG_H
