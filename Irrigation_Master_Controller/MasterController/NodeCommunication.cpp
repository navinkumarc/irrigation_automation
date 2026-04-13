// NodeCommunication.cpp - Handles all LoRa node communications
#include "NodeCommunication.h"
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

  // Response would be in the LoRa acknowledgment
  // For now, just return success if ACK received
  response = "ACK";
  return true;
}

// Process incoming messages from nodes (node to main)
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

  // Could implement node tracking here
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

  // Determine message type
  if (msg.startsWith("STAT|")) {
    nodeMsg.type = NodeMessageType::TELEMETRY;

    // Parse node ID
    int nPos = msg.indexOf("N=");
    if (nPos >= 0) {
      int comma = msg.indexOf(',', nPos);
      String nodeIdStr = msg.substring(nPos + 2, comma > 0 ? comma : msg.length());
      nodeMsg.nodeId = nodeIdStr.toInt();
    }

    // Parse battery percentage
    int battPos = msg.indexOf("BATT=");
    if (battPos >= 0) {
      int battEnd = msg.indexOf(',', battPos);
      String battStr = msg.substring(battPos + 5, battEnd > 0 ? battEnd : msg.length());
      nodeMsg.batteryPercent = battStr.toInt();
    }

    // Parse battery voltage
    int bvPos = msg.indexOf("BV=");
    if (bvPos >= 0) {
      int bvEnd = msg.indexOf(',', bvPos);
      String bvStr = msg.substring(bvPos + 3, bvEnd > 0 ? bvEnd : msg.length());
      nodeMsg.batteryVoltage = bvStr.toFloat();
    }

    // Parse solar voltage
    int solPos = msg.indexOf("SOLV=");
    if (solPos >= 0) {
      int solEnd = msg.indexOf(',', solPos);
      String solStr = msg.substring(solPos + 5, solEnd > 0 ? solEnd : msg.length());
      nodeMsg.solarVoltage = solStr.toFloat();
    }

  } else if (msg.startsWith("AUTO_CLOSE|")) {
    nodeMsg.type = NodeMessageType::AUTO_CLOSE;

    // Parse node ID
    int nPos = msg.indexOf("N=");
    if (nPos >= 0) {
      int comma = msg.indexOf(',', nPos);
      String nodeIdStr = msg.substring(nPos + 2, comma > 0 ? comma : msg.length());
      nodeMsg.nodeId = nodeIdStr.toInt();
    }

    nodeMsg.reason = "Auto-close triggered";
  } else {
    nodeMsg.type = NodeMessageType::UNKNOWN;
  }

  return nodeMsg;
}

// Process node-specific messages from the incoming queue
void NodeCommunication::processNodeMessages() {
  if (!initialized) {
    return;
  }

  // Dequeue messages and check if they're node messages
  String msg;
  while (incomingQueue.dequeue(msg)) {
    // Check if this is a node message
    if (msg.startsWith("STAT|") || msg.startsWith("AUTO_CLOSE|")) {
      Serial.println("[NodeComm] Processing node message: " + msg.substring(0, 30) + "...");

      // Parse the message
      NodeMessage nodeMsg = parseNodeMessage(msg);

      // Call the business logic callback if set
      if (messageCallback) {
        messageCallback(nodeMsg);
      } else {
        Serial.println("[NodeComm] ⚠ No callback set for node messages");
      }
    } else {
      // Not a node message, put it back for UserCommunication
      incomingQueue.enqueue(msg);
      break;  // Stop processing to avoid infinite loop
    }
  }
}
