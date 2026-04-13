// NodeCommunication.cpp - Handles all LoRa node communications
#include "NodeCommunication.h"
#include "MessageFormats.h"
#include "MessageQueue.h"

extern MessageQueue incomingQueue;

NodeCommunication::NodeCommunication() : loraComm(nullptr), initialized(false), messageCallback(nullptr) {}

bool NodeCommunication::init(LoRaComm* lora) {
  if (lora == nullptr) {
    Serial.println("[NodeComm] ❌ LoRa pointer is null");
    return false;
  }

  loraComm = lora;
  initialized = true;
  Serial.println("[NodeComm] ✓ Initialized");
  return true;
}

bool NodeCommunication::isInitialized() {
  return initialized;
}

void NodeCommunication::setMessageCallback(NodeMessageCallback callback) {
  messageCallback = callback;
}

// Send command to node (main to node)
bool NodeCommunication::sendCommand(int nodeId, const String &command) {
  if (!initialized || loraComm == nullptr) {
    Serial.println("[NodeComm] ❌ Not initialized");
    return false;
  }

  if (nodeId < 1 || nodeId > 255) {
    Serial.println("[NodeComm] ❌ Invalid node ID: " + String(nodeId));
    return false;
  }

  Serial.println("[NodeComm] → Sending to Node " + String(nodeId) + ": " + command);
  bool result = loraComm->sendWithAck(command, nodeId, "", 0, 0);

  if (result) {
    Serial.println("[NodeComm] ✓ Node " + String(nodeId) + " acknowledged");
  } else {
    Serial.println("[NodeComm] ✗ Node " + String(nodeId) + " timeout");
  }

  return result;
}

// Send command and wait for specific response
bool NodeCommunication::sendCommandWithResponse(int nodeId, const String &command, String &response) {
  if (!sendCommand(nodeId, command)) {
    return false;
  }

  // Response is in the LoRa acknowledgment
  response = "ACK";
  return true;
}

// Process incoming messages from nodes (node to main) — low-level LoRa receive
void NodeCommunication::processIncoming() {
  if (!initialized || loraComm == nullptr) {
    return;
  }

  loraComm->processIncoming();
}

// Get node status
String NodeCommunication::getNodeStatus(int nodeId) {
  if (!initialized) {
    return "NodeComm not initialized";
  }

  return "Node " + String(nodeId) + " status unknown";
}

// Parse node message into structured data
NodeMessage NodeCommunication::parseNodeMessage(const String &msg) {
  NodeMessage nodeMsg;
  nodeMsg.rawMessage = msg;
  nodeMsg.nodeId = 0;
  nodeMsg.batteryPercent = 0;
  nodeMsg.batteryVoltage = 0.0;
  nodeMsg.solarVoltage = 0.0;
  nodeMsg.valveStates = "";
  nodeMsg.moistureLevels = "";
  nodeMsg.reason = "";

  if (msg.startsWith(MSG_STAT_PREFIX MSG_SEP)) {
    nodeMsg.type = NodeMessageType::TELEMETRY;

    // Use MsgFmt::extractField for all key=value parsing
    String nStr = MsgFmt::extractField(msg, KEY_NODE_ID);
    if (nStr.length()) nodeMsg.nodeId = nStr.toInt();

    String battStr = MsgFmt::extractField(msg, KEY_BATTERY);
    if (battStr.length()) nodeMsg.batteryPercent = battStr.toInt();

    String bvStr = MsgFmt::extractField(msg, KEY_BATT_V);
    if (bvStr.length()) nodeMsg.batteryVoltage = bvStr.toFloat();

    String solStr = MsgFmt::extractField(msg, KEY_SOLAR_V);
    if (solStr.length()) nodeMsg.solarVoltage = solStr.toFloat();

    nodeMsg.valveStates    = MsgFmt::extractField(msg, KEY_VALVES);
    nodeMsg.moistureLevels = MsgFmt::extractField(msg, KEY_MOISTURE);

  } else if (msg.startsWith(MSG_AUTO_CLOSE_PREFIX MSG_SEP)) {
    nodeMsg.type = NodeMessageType::AUTO_CLOSE;

    String nStr = MsgFmt::extractField(msg, KEY_NODE_ID);
    if (nStr.length()) nodeMsg.nodeId = nStr.toInt();

    String reason = MsgFmt::extractField(msg, KEY_REASON);
    nodeMsg.reason = reason.length() ? reason : "Auto-close triggered";

  } else {
    nodeMsg.type = NodeMessageType::UNKNOWN;
  }

  return nodeMsg;
}

// Process node-specific messages from the incoming queue.
// FIX: Previous version had an infinite-loop bug — it re-enqueued non-node
// messages then immediately broke, permanently blocking those messages from
// being processed. The fix drains the entire queue into a temporary buffer,
// dispatches node messages, then re-enqueues non-node messages so they are
// visible to UserCommunication on the next iteration.
void NodeCommunication::processNodeMessages() {
  if (!initialized) return;

  // Collect all currently queued messages
  std::vector<String> nonNodeMessages;
  String msg;

  while (incomingQueue.dequeue(msg)) {
    if (msg.startsWith(MSG_STAT_PREFIX MSG_SEP) || msg.startsWith(MSG_AUTO_CLOSE_PREFIX MSG_SEP)) {
      Serial.println("[NodeComm] Processing node message: " + msg.substring(0, 40));

      NodeMessage nodeMsg = parseNodeMessage(msg);

      if (messageCallback) {
        messageCallback(nodeMsg);
      } else {
        Serial.println("[NodeComm] ⚠ No callback set for node messages");
      }
    } else {
      // Keep non-node messages to re-enqueue
      nonNodeMessages.push_back(msg);
    }
  }

  // Re-enqueue non-node messages for UserCommunication to process
  for (const String &nonNode : nonNodeMessages) {
    incomingQueue.enqueue(nonNode);
  }
}
