// IChannelAdapter.h - Abstract interface for outbound user communication channels
//
// UserCommunication uses this interface exclusively to send messages.
// It never holds raw module pointers (ModemSMS*, MQTTComm*, etc.).
// Each concrete adapter wraps one transport module and lives in CommManager.
//
// Loose-coupling contract:
//   • UserCommunication calls only send() and isAvailable()
//   • The adapter decides internally whether the channel is ready
//   • The adapter handles any module-specific framing / topic / address
//   • UserCommunication is compiled with zero #if ENABLE_X guards

#ifndef ICHANNEL_ADAPTER_H
#define ICHANNEL_ADAPTER_H

#include <Arduino.h>

class IChannelAdapter {
public:
  virtual ~IChannelAdapter() {}

  // Human-readable channel name for logging (e.g. "SMS", "MQTT", "BLE")
  virtual const char* channelName() const = 0;

  // True when the channel can accept outbound messages right now.
  // UserCommunication checks this before calling send().
  virtual bool isAvailable() const = 0;

  // Send a plain-text message to the user.
  // Returns true if the message was accepted by the channel.
  virtual bool send(const String &message) = 0;

  // Optional: send a reply to a specific address (e.g. phone number, MQTT topic).
  // Default implementation falls back to send().
  virtual bool sendTo(const String &address, const String &message) {
    (void)address;
    return send(message);
  }
};

#endif // ICHANNEL_ADAPTER_H
