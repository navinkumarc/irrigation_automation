// NodeTransportAdapters.h  —  Concrete INodeTransport implementations
//
// One adapter per physical transport. Each adapter:
//   • Is compiled only when its ENABLE_ flag is set
//   • Holds a reference to its hardware module
//   • Implements sendCommand() using the module's send API
//   • Implements pollIncoming() to drain the module's receive buffer
//     and fire the inbound callback for each complete node message
//
// Adapters are instantiated in CommManager and registered into NodeCommunication.
// NodeCommunication never includes these headers — it only knows INodeTransport.

#ifndef NODE_TRANSPORT_ADAPTERS_H
#define NODE_TRANSPORT_ADAPTERS_H

#include "INodeTransport.h"
#include "MessageFormats.h"
#include "Config.h"

// ─── LoRa node transport ──────────────────────────────────────────────────────
// Uses LoRaComm::sendWithAck() for reliable delivery with ACK + retry.
// Receives STAT| and AUTO_CLOSE| messages via LoRaComm::processIncoming()
// which populates incomingQueue; pollIncoming() drains that queue.
#if ENABLE_LORA
#include "LoRaComm.h"
#include "MessageQueue.h"

extern MessageQueue incomingQueue;

class LoRaNodeTransport : public INodeTransport {
  LoRaComm        &_lora;
  NodeInboundCallback _cb;
  bool            _hwReady;

public:
  explicit LoRaNodeTransport(LoRaComm &lora)
    : _lora(lora), _cb(nullptr), _hwReady(false) {}

  void setHwReady(bool ready) { _hwReady = ready; }

  const char* transportName() const override { return "LoRa"; }

  bool isAvailable() const override { return _hwReady; }

  bool sendCommand(const String &cmdType, int nodeId,
                   const String &schedId, int seqIdx,
                   uint32_t durationMs = 0) override {
    if (!_hwReady) {
      Serial.println("[LoRaTransport] ❌ LoRa not ready");
      return false;
    }
    return _lora.sendWithAck(cmdType, nodeId, schedId, seqIdx, durationMs);
  }

  void setInboundCallback(NodeInboundCallback cb) override { _cb = cb; }

  // Drain the LoRa receive buffer then pull node messages from incomingQueue
  void pollIncoming() override {
    if (!_hwReady) return;

    // Step 1: let LoRaComm fill incomingQueue from the radio
    _lora.processIncoming();

    // Step 2: drain node messages from the queue
    if (!_cb) return;
    String msg;
    std::vector<String> nonNode;
    while (incomingQueue.dequeue(msg)) {
      if (msg.startsWith(MSG_STAT_PREFIX MSG_SEP) ||
          msg.startsWith(MSG_AUTO_CLOSE_PREFIX MSG_SEP)) {
        _cb(msg, transportName());
      } else {
        nonNode.push_back(msg);  // preserve for other consumers
      }
    }
    for (auto &m : nonNode) incomingQueue.enqueue(m);
  }
};
#endif // ENABLE_LORA

// ─── BLE node transport ───────────────────────────────────────────────────────
// Uses BLEComm::notify() to push commands to a BLE-connected node.
// Inbound: BLE commands arrive via BLEComm's commandCallback, which pushes
// to incomingQueue. pollIncoming() drains that queue for node messages.
// Note: BLE is connectionful — isAvailable() requires an active client.
#if ENABLE_BLE
#include "BLEComm.h"
#include "MessageQueue.h"

extern MessageQueue incomingQueue;

class BLENodeTransport : public INodeTransport {
  BLEComm         &_ble;
  NodeInboundCallback _cb;

public:
  explicit BLENodeTransport(BLEComm &ble) : _ble(ble), _cb(nullptr) {}

  const char* transportName() const override { return "BLE"; }

  // BLE node transport is only available when a client is connected
  bool isAvailable() const override { return _ble.isConnected(); }

  // Send a formatted node command via BLE notify.
  // BLE is best-effort (no ACK layer at this level) — returns true if notify succeeded.
  bool sendCommand(const String &cmdType, int nodeId,
                   const String &schedId, int seqIdx,
                   uint32_t durationMs = 0) override {
    if (!isAvailable()) {
      Serial.println("[BLETransport] ❌ No BLE client connected");
      return false;
    }
    // Build the same wire format used on LoRa so nodes handle both identically
    uint32_t mid = (uint32_t)millis();  // Best-effort mid for BLE
    String cmd = MsgFmt::buildNodeCmd(mid, cmdType, nodeId, schedId, seqIdx, durationMs);
    bool ok = _ble.notify(cmd);
    if (ok) Serial.println("[BLETransport] → Sent: " + cmd);
    else    Serial.println("[BLETransport] ❌ notify failed");
    return ok;
  }

  void setInboundCallback(NodeInboundCallback cb) override { _cb = cb; }

  // BLE inbound node messages arrive via incomingQueue (set by BLEComm callbacks)
  void pollIncoming() override {
    if (!_cb) return;
    String msg;
    std::vector<String> nonNode;
    while (incomingQueue.dequeue(msg)) {
      if (msg.startsWith(MSG_STAT_PREFIX MSG_SEP) ||
          msg.startsWith(MSG_AUTO_CLOSE_PREFIX MSG_SEP)) {
        _cb(msg, transportName());
      } else {
        nonNode.push_back(msg);
      }
    }
    for (auto &m : nonNode) incomingQueue.enqueue(m);
  }
};
#endif // ENABLE_BLE

#endif // NODE_TRANSPORT_ADAPTERS_H
