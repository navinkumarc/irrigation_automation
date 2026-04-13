// CommConfig.h  —  Runtime communication configuration (in-memory struct)
//
// CommConfig holds all runtime-editable communication settings in memory.
// It has NO persistence logic — StorageManager owns load/save entirely.
//
// Channel model:
//   SMS        modem AT mode
//   Internet  MQTT over data bearer (WiFi or PPPoS)
//   Bluetooth  BLE notify/write
//   LoRa       user broadcast
//   Serial     always-on config console (cannot be disabled)
//
//   WiFi and PPPoS are DATA BEARERS (transport), NOT user channels.
//
// Lifecycle:
//   1. storage.loadCommConfig(commCfg)  — called in setup() before commMgr.begin()
//   2. SerialConfigHandler mutates commCfg fields in memory
//   3. storage.saveCommConfig(commCfg)  — called on SAVE CONFIG command
//   4. storage.resetCommConfig(commCfg) — called on RESET CONFIG command

#ifndef COMM_CONFIG_H
#define COMM_CONFIG_H

#include <Arduino.h>
#include "Config.h"

// Data bearer tokens
#define DATA_BEARER_WIFI  1
#define DATA_BEARER_PPPOS 2

struct CommConfig {

  // ── User channel enables ──────────────────────────────────────────────────
  bool chSMS;          // SMS channel
  bool chInternet;     // Internet channel (MQTT over WiFi/PPPoS)
  bool chBluetooth;    // Bluetooth channel
  bool chLoRa;         // LoRa channel
  // Serial is always enabled — no flag needed

  // ── Data bearer ───────────────────────────────────────────────────────────
  uint8_t dataBearerPrimary;   // DATA_BEARER_WIFI or DATA_BEARER_PPPOS

  // ── WiFi ──────────────────────────────────────────────────────────────────
  String wifiSSID;
  String wifiPass;

  // ── PPPoS / cellular ──────────────────────────────────────────────────────
  String cellularAPN;

  // ── MQTT ──────────────────────────────────────────────────────────────────
  String   mqttBroker;
  uint16_t mqttPort;
  String   mqttUser;
  String   mqttPass;
  String   mqttClientId;
  bool     mqttTLS;

  // ── SMS ───────────────────────────────────────────────────────────────────
  String smsPhone1;
  String smsPhone2;

  // ── Bluetooth ─────────────────────────────────────────────────────────────
  String bleName;

  // ── LoRa ──────────────────────────────────────────────────────────────────
  uint32_t loraFrequencyHz;

  // ── HTTP ──────────────────────────────────────────────────────────────────
  uint16_t httpPort;

  // ── Constructor: populate from Config.h compile-time defaults ─────────────
  CommConfig() {
    chSMS             = (ENABLE_SMS            == 1);
    chInternet        = (ENABLE_MQTT           == 1);
    chBluetooth       = (ENABLE_BLE            == 1);
    chLoRa            = (ENABLE_LORA_USER_COMM == 1);
    dataBearerPrimary = DATA_BEARER_WIFI;
    wifiSSID          = WIFI_SSID;
    wifiPass          = WIFI_PASS;
    cellularAPN       = PPPOS_APN;
    mqttBroker        = MQTT_BROKER;
    mqttPort          = MQTT_PORT;
    mqttUser          = MQTT_USER;
    mqttPass          = MQTT_PASS;
    mqttClientId      = MQTT_CLIENT_ID;
    mqttTLS           = (MQTT_USE_SSL == 1);
    smsPhone1         = SMS_ALERT_PHONE_1;
    smsPhone2         = SMS_ALERT_PHONE_2;
    bleName           = BLE_DEVICE_NAME;
    loraFrequencyHz   = RF_FREQUENCY;
    httpPort          = HTTP_SERVER_PORT;
  }

  // ── Helpers ───────────────────────────────────────────────────────────────
  const char* bearerName() const {
    return (dataBearerPrimary == DATA_BEARER_PPPOS) ? "PPPoS" : "WiFi";
  }

  void print() const;
};

// Global instance — defaults set by constructor, overwritten by StorageManager
extern CommConfig commCfg;

#endif // COMM_CONFIG_H
