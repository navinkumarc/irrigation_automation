// MQTTComm.h - MQTT v3.1.1 communication using ESP-IDF native MQTT client
// Works with both PPPoS (cellular) and WiFi connections
// Supports TLS/SSL and MQTT v3.1.1 protocol (enabled by default in Arduino ESP32)
#ifndef MQTT_COMM_H
#define MQTT_COMM_H

#include <Arduino.h>
#include "mqtt_client.h"  // ESP-IDF MQTT client
#include "Config.h"

// Callback type for incoming MQTT messages
typedef void (*MQTTMessageCallback)(const String &topic, const String &payload);

class MQTTComm {
private:
  esp_mqtt_client_handle_t mqttClient;
  bool configured;
  bool connected;
  unsigned long lastReconnectAttempt;
  unsigned long reconnectInterval;
  MQTTMessageCallback messageCallback;
  String brokerUri;  // Store URI string to keep it in scope

  // Static event handler (required by ESP-IDF MQTT)
  static void mqttEventHandler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);

public:
  MQTTComm();
  ~MQTTComm();

  // Initialize MQTT (no network connection needed yet)
  bool init();

  // Configure MQTT connection (requires active network: PPPoS or WiFi)
  bool configure();

  // Publish message to topic (QoS 0, 1, or 2)
  bool publish(const String &topic, const String &payload, int qos = 0);

  // Subscribe to topic (QoS 0, 1, or 2)
  bool subscribe(const String &topic, int qos = 0);

  // Check if connected to MQTT broker
  bool isConnected();

  // Attempt reconnection if disconnected
  void reconnect();

  // Process background tasks (not needed for ESP-IDF MQTT - handled by events)
  void processBackground();

  // Set callback for incoming messages
  void setMessageCallback(MQTTMessageCallback callback);

  // Disconnect from broker
  void disconnect();
};

#endif
