// NodeCommunication.h  —  All master ↔ node communication
//
// NodeCommunication is the single entry point for all valve-node interactions.
// It mirrors UserCommunication's design: zero direct transport knowledge,
// all channels accessed through the INodeTransport adapter interface.
//
// ── What NodeCommunication owns ─────────────────────────────────────────────
//   Transport adapters  — LoRaNodeTransport, BLENodeTransport (registered externally)
//   Message parsing     — STAT|, AUTO_CLOSE| → NodeMessage struct
//   Outbound routing    — picks first available transport; falls back to next
//   Inbound dispatch    — fires NodeMessageCallback for each parsed node event
//
// ── What NodeCommunication does NOT know about ───────────────────────────────
//   LoRaComm, BLEComm, WiFiComm, ModemSMS, or any hardware module
//   CommManager internals
//   Any #if ENABLE_X guards
//
// ── MasterController.ino API (through CommManager) ───────────────────────────
//   nodeComm.registerTransport(adapter)          — add a transport
//   nodeComm.sendValveOpen(node, sched, idx, ms) — open a valve
//   nodeComm.sendValveClose(node, sched, idx)    — close a valve
//   nodeComm.sendPing(nodeId)                    — ping a node
//   nodeComm.sendCommand(type, node, ...)        — generic command
//   nodeComm.process()                           — drive all transports (call every loop)
//   nodeComm.setMessageCallback(cb)              — receive node events
//
// ── Transport priority ────────────────────────────────────────────────────────
//   Transports are tried in registration order.
//   First available (isAvailable() == true) wins.
//   If primary fails, next is tried automatically.

#ifndef NODE_COMMUNICATION_H
#define NODE_COMMUNICATION_H

#include <Arduino.h>
#include <functional>
#include <vector>
#include "INodeTransport.h"
#include "MessageFormats.h"

// ─── Inbound message types ────────────────────────────────────────────────────

enum class NodeMessageType {
  TELEMETRY,    // STAT|   — sensor data from node
  AUTO_CLOSE,   // AUTO_CLOSE| — node closed valve automatically
  UNKNOWN
};

struct NodeMessage {
  NodeMessageType type       = NodeMessageType::UNKNOWN;
  String          rawMessage;
  String          transport;   // Which transport delivered this ("LoRa", "BLE")
  int             nodeId       = 0;

  // Telemetry (STAT)
  int    batteryPercent  = 0;
  float  batteryVoltage  = 0.0f;
  float  solarVoltage    = 0.0f;
  String valveStates;
  String moistureLevels;

  // Auto-close
  String reason;
};

// Callback fired by NodeCommunication when a parsed node message arrives
using NodeMessageCallback = std::function<void(const NodeMessage &)>;

// ─── NodeCommunication ────────────────────────────────────────────────────────

class NodeCommunication {
  // Registered transports in priority order (index 0 = highest priority)
  std::vector<INodeTransport*> transports;

  // Callback registered by CommManager to receive parsed node events
  NodeMessageCallback messageCallback;

  bool initialized = false;

  // Parse raw wire message into NodeMessage struct
  NodeMessage parseMessage(const String &raw, const char *transport) const;

  // Inbound handler registered with each transport adapter
  void onRawMessage(const String &raw, const char *transport);

public:
  NodeCommunication() : messageCallback(nullptr) {}

  // ── Setup ──────────────────────────────────────────────────────────────────

  // Register a transport adapter. Call before begin().
  // Adapters are tried in registration order when sending.
  void registerTransport(INodeTransport *transport);

  // Finalise setup and register inbound callbacks with all transports.
  // Returns true if at least one transport is available.
  bool begin();

  bool isInitialized() const { return initialized; }

  // ── Outbound — application sends commands to nodes ────────────────────────

  // Open a valve on a node.
  bool sendValveOpen(int nodeId, const String &schedId,
                     int seqIdx, uint32_t durationMs);

  // Close a valve on a node.
  bool sendValveClose(int nodeId, const String &schedId, int seqIdx);

  // Ping a node (connectivity check).
  bool sendPing(int nodeId);

  // Request telemetry from a node.
  bool requestStatus(int nodeId);

  // Generic command (cmdType from CMD_* in MessageFormats.h).
  bool sendCommand(const String &cmdType, int nodeId,
                   const String &schedId, int seqIdx,
                   uint32_t durationMs = 0);

  // ── Inbound — register callback for parsed node events ───────────────────

  void setMessageCallback(NodeMessageCallback cb) { messageCallback = cb; }

  // ── Background — call every loop() ───────────────────────────────────────

  // Drives all registered transport adapters: polls inbound, handles queues.
  void process();

  // ── Diagnostics ───────────────────────────────────────────────────────────

  void printStatus() const;
  int  availableTransportCount() const;
};

#endif // NODE_COMMUNICATION_H
