// ChannelAdapters.h - Concrete IChannelAdapter implementations
//
// One adapter per transport module. Each adapter:
//   • Is compiled only when its ENABLE_ flag is set
//   • Holds a reference to its module (e.g. ModemSMS&)
//   • Implements isAvailable() by querying the module's ready state
//   • Implements send() by calling the module's send API
//
// Adapters are instantiated in CommManager and registered into UserCommunication.
// UserCommunication never includes these headers — it only knows IChannelAdapter.

#ifndef CHANNEL_ADAPTERS_H
#define CHANNEL_ADAPTERS_H

#include "IChannelAdapter.h"
#include "Config.h"

// ─── SMS ──────────────────────────────────────────────────────────────────────
#if ENABLE_SMS
#include "ModemSMS.h"

class SMSChannelAdapter : public IChannelAdapter {
  ModemSMS&   _sms;
  String      _adminPhone;
public:
  SMSChannelAdapter(ModemSMS &sms, const String &adminPhone)
    : _sms(sms), _adminPhone(adminPhone) {}

  const char* channelName() const override { return "SMS"; }

  bool isAvailable() const override { return _sms.isReady(); }

  // Broadcast to configured admin phones
  bool send(const String &message) override {
    return _sms.sendNotification(message);
  }

  // Reply to a specific sender phone number
  bool sendTo(const String &address, const String &message) override {
    return _sms.sendSMS(address, message);
  }
};
#endif // ENABLE_SMS

// ─── MQTT ─────────────────────────────────────────────────────────────────────
#if ENABLE_MQTT
#include "MQTTComm.h"

class MQTTChannelAdapter : public IChannelAdapter {
  MQTTComm& _mqtt;
  String    _alertTopic;
public:
  explicit MQTTChannelAdapter(MQTTComm &mqtt,
                              const String &alertTopic = MQTT_TOPIC_ALERTS)
    : _mqtt(mqtt), _alertTopic(alertTopic) {}

  const char* channelName() const override { return "MQTT"; }

  bool isAvailable() const override { return _mqtt.isConnected(); }

  // Publish to alert topic
  bool send(const String &message) override {
    return _mqtt.publish(_alertTopic, message);
  }

  // Publish to a specific topic (address = topic string)
  bool sendTo(const String &address, const String &message) override {
    return _mqtt.publish(address, message);
  }
};
#endif // ENABLE_MQTT

// ─── BLE ──────────────────────────────────────────────────────────────────────
#if ENABLE_BLE
#include "BLEComm.h"

class BLEChannelAdapter : public IChannelAdapter {
  BLEComm& _ble;
public:
  explicit BLEChannelAdapter(BLEComm &ble) : _ble(ble) {}

  const char* channelName() const override { return "BLE"; }

  bool isAvailable() const override { return _ble.isConnected(); }

  bool send(const String &message) override {
    return _ble.notify(message);
  }
};
#endif // ENABLE_BLE

// ─── LoRa (user-facing broadcast) ────────────────────────────────────────────
// LoRa is primarily for node communication (handled by NodeCommunication).
// This adapter covers operator broadcast messages sent over LoRa.
#if ENABLE_LORA && ENABLE_LORA_USER_COMM
#include "LoRaComm.h"

class LoRaChannelAdapter : public IChannelAdapter {
  LoRaComm& _lora;
  bool      _initialized;
public:
  LoRaChannelAdapter(LoRaComm &lora, bool initialized)
    : _lora(lora), _initialized(initialized) {}

  const char* channelName() const override { return "LoRa"; }

  bool isAvailable() const override { return _initialized; }

  bool send(const String &message) override {
    if (!_initialized) return false;
    _lora.sendRaw(message);
    return true;
  }
};
#endif // ENABLE_LORA && ENABLE_LORA_USER_COMM

// ─── Serial (USB / UART0 debug console) ──────────────────────────────────────
// Always available as long as Serial has been initialised in setup().
// Inbound: CommManager::pollSerial() reads complete lines and delivers them
//          as ChannelMessages. The reply lambda writes back to Serial.
// Outbound: send() writes a prefixed line to Serial.
// No ENABLE_ guard needed — Serial is always present on Arduino/ESP32.
// The ENABLE_SERIAL_COMM flag in Config.h controls whether CommManager
// polls and registers this adapter.
#if ENABLE_SERIAL_COMM

class SerialChannelAdapter : public IChannelAdapter {
  HardwareSerial& _serial;
  String          _lineBuffer;    // Accumulates characters until newline

public:
  explicit SerialChannelAdapter(HardwareSerial &serial = Serial)
    : _serial(serial) {}

  const char* channelName() const override { return "Serial"; }

  // Serial is always available once Serial.begin() has been called.
  bool isAvailable() const override { return true; }

  // Write an outbound message to the serial console.
  bool send(const String &message) override {
    _serial.println("[CommMgr→Serial] " + message);
    return true;
  }

  // sendTo is not meaningful for Serial — falls back to send().
  bool sendTo(const String &address, const String &message) override {
    (void)address;
    return send(message);
  }

  // Read available bytes into the line buffer.
  // Returns a complete line (without trailing CR/LF) when one is ready,
  // or an empty String if no complete line is available yet.
  // Call this from CommManager::pollSerial() every loop iteration.
  String readLine() {
    while (_serial.available()) {
      char c = (char)_serial.read();
      if (c == '
') {
        String line = _lineBuffer;
        _lineBuffer = "";
        line.trim();
        return line;      // Caller gets the complete line
      } else if (c != '') {
        _lineBuffer += c;
      }
    }
    return "";            // No complete line yet
  }
};

#endif // ENABLE_SERIAL_COMM


#endif // CHANNEL_ADAPTERS_H
