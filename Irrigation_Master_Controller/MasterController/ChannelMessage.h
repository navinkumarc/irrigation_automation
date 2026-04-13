// ChannelMessage.h - Uniform inbound message from any user-facing channel
//
// All channel processors (SMS, MQTT, HTTP, BLE) translate their
// raw inputs into a ChannelMessage and deliver it to UserCommunication
// via userComm.onMessageReceived(). UserCommunication never queries
// the channel directly — it only sees this struct.

#ifndef CHANNEL_MESSAGE_H
#define CHANNEL_MESSAGE_H

#include <Arduino.h>
#include <functional>

// Reply callback: called by UserCommunication to send a response back
// through the same channel the message arrived on.
// The adapter layer captures the necessary context (sender address,
// MQTT topic, BLE connection handle, etc.) in the lambda closure.
using ReplyFn = std::function<void(const String &response)>;

struct ChannelMessage {
  String  text;          // Raw message text / command
  String  sender;        // Sender identifier (phone number, MQTT client ID, "BLE", etc.)
  String  channel;       // Channel name for logging ("SMS", "MQTT", "BLE", "HTTP")
  ReplyFn reply;         // Call this to send a response back; may be nullptr

  ChannelMessage() : reply(nullptr) {}

  ChannelMessage(const String &t, const String &s,
                 const String &ch, ReplyFn r = nullptr)
    : text(t), sender(s), channel(ch), reply(r) {}

  // Convenience: does this message have a reply path?
  bool canReply() const { return reply != nullptr; }
};

#endif // CHANNEL_MESSAGE_H
