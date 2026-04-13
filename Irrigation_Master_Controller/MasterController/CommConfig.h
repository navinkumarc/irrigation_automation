// CommConfig.h  —  Runtime communication configuration
//
// Channel model
// ─────────────
//   Four user-facing communication channels:
//     SMS        modem AT mode, text message alerts & commands
//     Data       MQTT over a data bearer (WiFi or PPPoS)
//     Bluetooth  BLE notify / write to a connected mobile client
//     LoRa       user broadcast over the LoRa radio
//
//   Serial is ALWAYS active — it is the mandatory config console
//   and debug monitor; it cannot be disabled.
//
//   WiFi and PPPoS are DATA BEARERS (transport only).
//   They provide IP connectivity for MQTT and HTTP.
//   They are NOT user channels and do not appear as IChannelAdapters.
//
// Persistence
// ───────────
//   All fields are stored in ESP32 NVS under namespace "commcfg".
//   Defaults come from Config.h compile-time macros.
//   Runtime changes are made via Serial commands (SerialConfigHandler).
//   commCfg.save(prefs) persists to flash; commCfg.load(prefs) restores on boot.

#ifndef COMM_CONFIG_H
#define COMM_CONFIG_H

#include <Arduino.h>
#include <Preferences.h>
#include "Config.h"

// Data bearer tokens
#define DATA_BEARER_WIFI  1
#define DATA_BEARER_PPPOS 2

struct CommConfig {

  // ── User channel enables ──────────────────────────────────────────────────
  bool chSMS;          // SMS user channel
  bool chData;         // Data/MQTT user channel
  bool chBluetooth;    // Bluetooth user channel
  bool chLoRa;         // LoRa user channel
  // Serial is always enabled — not stored here

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
  String smsPhone1;    // Primary admin phone  (E.164)
  String smsPhone2;    // Secondary admin phone (optional, "" to disable)

  // ── Bluetooth ─────────────────────────────────────────────────────────────
  String bleName;      // BLE advertised device name

  // ── LoRa ──────────────────────────────────────────────────────────────────
  uint32_t loraFrequencyHz;

  // ── HTTP ──────────────────────────────────────────────────────────────────
  uint16_t httpPort;

  // ── Constructor: populate from Config.h compile-time defaults ─────────────
  CommConfig() {
    chSMS             = (ENABLE_SMS            == 1);
    chData            = (ENABLE_MQTT           == 1);
    chBluetooth       = (ENABLE_BLE            == 1);
    chLoRa            = (ENABLE_LORA_USER_COMM == 1);

    dataBearerPrimary = DATA_BEARER_WIFI;   // WiFi is default bearer

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

  // ── NVS persistence ───────────────────────────────────────────────────────
  void load (Preferences &prefs);
  void save (Preferences &prefs) const;
  void reset(Preferences &prefs);   // Restore Config.h defaults then save

  // ── Helpers ───────────────────────────────────────────────────────────────
  const char* bearerName() const {
    return (dataBearerPrimary == DATA_BEARER_PPPOS) ? "PPPoS" : "WiFi";
  }

  void print() const;
};

extern CommConfig commCfg;

#endif // COMM_CONFIG_H
