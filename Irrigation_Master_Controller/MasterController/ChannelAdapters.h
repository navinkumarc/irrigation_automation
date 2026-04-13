// ChannelAdapters.h  —  Concrete IChannelAdapter implementations
//
// Channel model
// ─────────────
//   SMS        modem AT mode
//   Internet  MQTT over data bearer (WiFi or PPPoS)
//   Bluetooth  BLE notify / write
//   LoRa       user broadcast
//   Serial     always-on config/debug console
//
//   WiFi and PPPoS are DATA BEARERS (transport), NOT user channels.
//   They never appear as IChannelAdapters.
//
// Runtime enable/disable is via CommConfig (commCfg).
// isAvailable() checks BOTH the commCfg runtime flag AND the hardware state.

#ifndef CHANNEL_ADAPTERS_H
#define CHANNEL_ADAPTERS_H

#include "IChannelAdapter.h"
#include "Config.h"
#include "CommConfig.h"

// ─── SMS ──────────────────────────────────────────────────────────────────────
#if ENABLE_SMS
#include "ModemSMS.h"

class SMSChannelAdapter : public IChannelAdapter {
  ModemSMS& _sms;
public:
  explicit SMSChannelAdapter(ModemSMS &sms) : _sms(sms) {}

  const char* channelName() const override { return "SMS"; }

  bool isAvailable() const override {
    return commCfg.chSMS && _sms.isReady();
  }

  bool send(const String &message) override {
    return _sms.sendNotification(message);
  }

  bool sendTo(const String &address, const String &message) override {
    return _sms.sendSMS(address, message);
  }
};
#endif // ENABLE_SMS

// ─── Data / MQTT ──────────────────────────────────────────────────────────────
// Bearer (WiFi or PPPoS) is irrelevant here — MQTTComm works the same
// regardless of which transport provides the IP connection.
#if ENABLE_MQTT
#include "MQTTComm.h"

class MQTTChannelAdapter : public IChannelAdapter {
  MQTTComm& _mqtt;
  String    _alertTopic;
public:
  explicit MQTTChannelAdapter(MQTTComm &mqtt,
                               const String &alertTopic = MQTT_TOPIC_ALERTS)
    : _mqtt(mqtt), _alertTopic(alertTopic) {}

  const char* channelName() const override { return "Internet"; }

  bool isAvailable() const override {
    return commCfg.chInternet && _mqtt.isConnected();
  }

  bool send(const String &message) override {
    return _mqtt.publish(_alertTopic, message);
  }

  bool sendTo(const String &topic, const String &message) override {
    return _mqtt.publish(topic, message);
  }
};
#endif // ENABLE_MQTT

// ─── Bluetooth ────────────────────────────────────────────────────────────────
#if ENABLE_BLE
#include "BLEComm.h"

class BLEChannelAdapter : public IChannelAdapter {
  BLEComm& _ble;
public:
  explicit BLEChannelAdapter(BLEComm &ble) : _ble(ble) {}

  const char* channelName() const override { return "Bluetooth"; }

  bool isAvailable() const override {
    return commCfg.chBluetooth && _ble.isConnected();
  }

  bool send(const String &message) override {
    return _ble.notify(message);
  }
};
#endif // ENABLE_BLE

// ─── LoRa user broadcast ──────────────────────────────────────────────────────
// Separate from NodeCommunication which handles valve-node protocol.
#if ENABLE_LORA && ENABLE_LORA_USER_COMM
#include "LoRaComm.h"

class LoRaChannelAdapter : public IChannelAdapter {
  LoRaComm& _lora;
  bool      _hwReady;
public:
  LoRaChannelAdapter(LoRaComm &lora, bool hwReady)
    : _lora(lora), _hwReady(hwReady) {}

  const char* channelName() const override { return "LoRa"; }

  bool isAvailable() const override {
    return commCfg.chLoRa && _hwReady;
  }

  bool send(const String &message) override {
    if (!_hwReady) return false;
    _lora.sendRaw(message);
    return true;
  }
};
#endif // ENABLE_LORA && ENABLE_LORA_USER_COMM

// ─── Serial  —  always-on config/debug console ───────────────────────────────
// Cannot be disabled. isAvailable() always returns true.
// Receives operator commands AND configuration commands (SerialConfigHandler
// intercepts SET/ENABLE/DISABLE/SHOW CONFIG before UserCommunication sees them).
#if ENABLE_SERIAL_COMM

class SerialChannelAdapter : public IChannelAdapter {
  HardwareSerial& _serial;
  String          _buf;    // line accumulator

public:
  explicit SerialChannelAdapter(HardwareSerial &serial = Serial)
    : _serial(serial) {}

  const char* channelName() const override { return "Serial"; }

  // Serial is unconditionally available — never gated by commCfg
  bool isAvailable() const override { return true; }

  bool send(const String &message) override {
    _serial.println("[Alert] " + message);
    return true;
  }

  bool sendTo(const String &, const String &message) override {
    return send(message);
  }

  // Non-blocking line reader. Call every loop(). Returns "" until '\n'.
  String readLine() {
    while (_serial.available()) {
      char c = (char)_serial.read();
      if (c == '\n') {
        String line = _buf;
        _buf = "";
        line.trim();
        return line;
      } else if (c != '\r') {
        _buf += c;
      }
    }
    return "";
  }
};

#endif // ENABLE_SERIAL_COMM

#endif // CHANNEL_ADAPTERS_H
