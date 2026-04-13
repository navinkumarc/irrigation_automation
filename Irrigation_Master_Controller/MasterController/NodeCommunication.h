// NodeCommunication.h - Handles all LoRa node communications
#ifndef NODE_COMMUNICATION_H
#define NODE_COMMUNICATION_H

#include <Arduino.h>
#include <functional>
#include "Config.h"
#include "LoRaComm.h"

// Node message types
enum class NodeMessageType {
  TELEMETRY,      // STAT| messages with sensor data
  AUTO_CLOSE,     // AUTO_CLOSE| messages
  UNKNOWN
};

// Node message structure
struct NodeMessage {
  NodeMessageType type;
  String rawMessage;
  int nodeId;

  // Telemetry data (for STAT messages)
  int batteryPercent;
  float batteryVoltage;
  float solarVoltage;
  String valveStates;
  String moistureLevels;

  // Auto-close data
  String reason;
};

// Callback for handling node messages
typedef std::function<void(const NodeMessage&)> NodeMessageCallback;

class NodeCommunication {
private:
  LoRaComm* loraComm;
  bool initialized;
  NodeMessageCallback messageCallback;

  // Parse node messages
  NodeMessage parseNodeMessage(const String &msg);

public:
  NodeCommunication();
  bool init(LoRaComm* lora);
  bool isInitialized();

  // Set callback for node messages
  void setMessageCallback(NodeMessageCallback callback);

  // Main to Node - Send commands
  bool sendCommand(int nodeId, const String &command);
  bool sendCommandWithResponse(int nodeId, const String &command, String &response);

  // Node to Main - Process incoming messages (low-level LoRa receive)
  void processIncoming();

  // Node to Main - Process node-specific messages from queue
  void processNodeMessages();

  // Node status
  String getNodeStatus(int nodeId);
};

extern NodeCommunication nodeComm;

#endif
