// SerialConfigHandler.h  —  Serial config console
//
// Intercepts configuration commands on the Serial channel.
// Called by CommManager::pollSerial() BEFORE the normal command dispatcher.
// Returns true if the line was a config command (caller must not process it further).
//
// Storage: delegates to StorageManager for persistence.
//   SAVE CONFIG  → storage.saveCommConfig(commCfg)
//   RESET CONFIG → storage.resetCommConfig(commCfg)
//
// ═══════════════════════════════════════════════════════
//  CONFIG HELP                     Show this reference
//  SHOW CONFIG                     Print current settings
//  SAVE CONFIG                     Persist to LittleFS
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
//  SET BLE_NAME    <n>
//  SET LORA_FREQ   <Hz>
//  SET HTTP_PORT   <number>
// ═══════════════════════════════════════════════════════
//  Changes are live in memory immediately.
//  Run SAVE CONFIG to persist across reboots.
//  Most changes require a reboot to take full effect.

#ifndef SERIAL_CONFIG_HANDLER_H
#define SERIAL_CONFIG_HANDLER_H

#include <Arduino.h>
#include "CommConfig.h"
#include "StorageManager.h"

class SerialConfigHandler {
  StorageManager &_storage;

  bool handleEnable (const String &up, const String &raw);
  bool handleDisable(const String &up, const String &raw);
  bool handleSet    (const String &up, const String &raw);
  void printHelp    () const;

public:
  explicit SerialConfigHandler(StorageManager &storage) : _storage(storage) {}

  // Returns true if the line was a config command.
  bool handle(const String &line);
};

#endif // SERIAL_CONFIG_HANDLER_H
