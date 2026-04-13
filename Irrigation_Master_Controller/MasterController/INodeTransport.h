// INodeTransport.h  —  Abstract interface for master-to-node transport channels
//
// NodeCommunication uses this interface exclusively for sending commands to
// valve nodes and receiving their responses. It never holds raw module
// pointers (LoRaComm*, BLEComm*, etc.).
//
// Loose-coupling contract:
//   • NodeCommunication calls only sendCommand(), isAvailable(), channelName()
//   • The adapter decides internally whether the transport is ready
//   • The adapter handles framing, ACK, retry, and addressing
//   • NodeCommunication is compiled with zero #if ENABLE_X guards
//
// Registered adapters are tried in priority order:
//   Primary   — first available adapter wins
//   Fallback  — next adapter tried if primary fails
//
// Inbound path:
//   Each adapter's pollIncoming() drains its hardware receive buffer and
//   pushes raw node messages to NodeCommunication via the inbound callback.

#ifndef INODE_TRANSPORT_H
#define INODE_TRANSPORT_H

#include <Arduino.h>
#include <functional>

// Callback NodeCommunication registers with each transport to receive raw
// inbound messages. Transport calls this when a full node message arrives.
using NodeInboundCallback = std::function<void(const String &rawMsg, const char *transport)>;

class INodeTransport {
public:
  virtual ~INodeTransport() {}

  // Human-readable transport name ("LoRa", "BLE")
  virtual const char* transportName() const = 0;

  // True when the transport is ready to send/receive
  virtual bool isAvailable() const = 0;

  // Send a structured node command.
  // cmdType: "OPEN" | "CLOSE" | "PING" | "STATUS"  (see MessageFormats.h CMD_*)
  // nodeId:  1-255
  // schedId: schedule ID string (may be empty)
  // seqIdx:  sequence step index
  // durationMs: valve open duration (OPEN only; 0 for other cmds)
  // Returns true if the command was delivered (ACK received or best-effort sent)
  virtual bool sendCommand(const String &cmdType, int nodeId,
                           const String &schedId, int seqIdx,
                           uint32_t durationMs = 0) = 0;

  // Register the callback NodeCommunication uses to receive inbound messages.
  // Called once during init; the transport stores the callback and fires it
  // whenever a complete node message arrives.
  virtual void setInboundCallback(NodeInboundCallback cb) = 0;

  // Drive the transport's receive loop. Called every process() iteration.
  // Should be non-blocking; fires inboundCallback for each complete message.
  virtual void pollIncoming() = 0;
};

#endif // INODE_TRANSPORT_H
