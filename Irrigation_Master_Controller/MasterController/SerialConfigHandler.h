// SerialConfigHandler.h  —  Serial config console
//
// Intercepts configuration commands arriving on the Serial channel.
// Called by CommManager::pollSerial() before the normal command dispatcher.
// Returns true if the line was consumed as a config command.
//
// ═══════════════════════════════════════════════════════
//  CONFIG HELP                     Show this reference
//  SHOW CONFIG                     Print current config
//  SAVE CONFIG                     Persist to NVS flash
//  RESET CONFIG                    Restore firmware defaults
// ───────────────────────────────────────────────────────
//  ENABLE  SMS | DATA | BLE | LORA
//  DISABLE SMS | DATA | BLE | LORA
//  (Serial cannot be disabled)
// ───────────────────────────────────────────────────────
//  SET BEARER    WIFI | PPPOS
//  SET WIFI_SSID <ssid>
//  SET WIFI_PASS <password>
//  SET APN       <apn>
//  SET MQTT_BROKER <host>
//  SET MQTT_PORT   <number>
//  SET MQTT_USER   <username>
//  SET MQTT_PASS   <password>
//  SET MQTT_CID    <client-id>
//  SET MQTT_TLS    ON | OFF
//  SET SMS_PHONE1  +<E.164>
//  SET SMS_PHONE2  +<E.164>
//  SET BLE_NAME    <name>
//  SET LORA_FREQ   <Hz>
//  SET HTTP_PORT   <number>
// ═══════════════════════════════════════════════════════
//  Changes apply on next reboot.
//  Run SAVE CONFIG to persist across power cycles.

#ifndef SERIAL_CONFIG_HANDLER_H
#define SERIAL_CONFIG_HANDLER_H

#include <Arduino.h>
#include <Preferences.h>
#include "CommConfig.h"

class SerialConfigHandler {
  Preferences &_prefs;

  bool handleEnable (const String &up, const String &raw);
  bool handleDisable(const String &up, const String &raw);
  bool handleSet    (const String &up, const String &raw);
  void printHelp    () const;

public:
  explicit SerialConfigHandler(Preferences &prefs) : _prefs(prefs) {}

  // Returns true if the line was a config command (caller must not process further).
  bool handle(const String &line);
};

#endif // SERIAL_CONFIG_HANDLER_H
