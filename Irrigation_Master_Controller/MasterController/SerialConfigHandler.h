// SerialConfigHandler.h  —  Serial config console
//
// Intercepts config commands from pollSerial() before UserCommunication.
// Delegates persistence to StorageManager.
//
// Channel model enforced:
//   Mutually exclusive (SET CHANNEL):  SMS | MQTT | HTTP | NONE
//   Independent (ENABLE/DISABLE):      BLE | LORA
//   Bearer (ENABLE/DISABLE):           PPPOS | WIFI
//   Serial: always ON, not configurable
//
// Quick reference:
//   CONFIG HELP          Full reference
//   SHOW CONFIG          Print current settings
//   SAVE CONFIG          Persist to LittleFS (/commconfig.json)
//   RESET CONFIG         Restore firmware defaults
//   SET CHANNEL SMS|MQTT|HTTP|NONE
//   ENABLE/DISABLE BLE|LORA|PPPOS|WIFI
//   SET WIFI_SSID / WIFI_PASS / APN
//   SET MQTT_BROKER / MQTT_PORT / MQTT_USER / MQTT_PASS / MQTT_CID / MQTT_TLS
//   SET SMS_PHONE1 / SMS_PHONE2
//   SET BLE_NAME / LORA_FREQ / HTTP_PORT

#ifndef SERIAL_CONFIG_HANDLER_H
#define SERIAL_CONFIG_HANDLER_H

#include <Arduino.h>
#include "CommConfig.h"
#include "StorageManager.h"

class SerialConfigHandler {
  StorageManager &_storage;

  bool handleSetChannel(const String &up);
  bool handleBearer    (const String &up, bool enable);
  bool handleSet       (const String &up, const String &raw);
  void printHelp       () const;

public:
  explicit SerialConfigHandler(StorageManager &storage) : _storage(storage) {}

  // Returns true if the line was a config command.
  bool handle(const String &line);
};

#endif // SERIAL_CONFIG_HANDLER_H
