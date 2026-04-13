// MQTTComm.cpp - MQTT v3.1.1 communication using ESP-IDF native MQTT client
#include "MQTTComm.h"
#include <WiFi.h>

// Static instance pointer for event handler
static MQTTComm *mqttInstance = nullptr;

// Global singleton instance
MQTTComm mqtt;

MQTTComm::MQTTComm()
  : mqttClient(nullptr),
    configured(false),
    connected(false),
    lastReconnectAttempt(0),
    reconnectInterval(5000),
    messageCallback(nullptr) {
  mqttInstance = this;
}

MQTTComm::~MQTTComm() {
  if (mqttClient != nullptr) {
    esp_mqtt_client_stop(mqttClient);
    esp_mqtt_client_destroy(mqttClient);
  }
}

bool MQTTComm::init() {
  Serial.println("[MQTT] Initializing MQTT v3.1.1 client...");

  // Build MQTT broker URI (store in member variable to keep it in scope)
  brokerUri = "mqtts://";  // Use mqtts:// for SSL/TLS
  brokerUri += MQTT_BROKER;
  brokerUri += ":";
  brokerUri += String(MQTT_PORT);

  Serial.println("[MQTT] Broker URI: " + brokerUri);

  // Configure MQTT client
  esp_mqtt_client_config_t mqtt_cfg = {};
  mqtt_cfg.broker.address.uri = brokerUri.c_str();
  mqtt_cfg.credentials.username = MQTT_USER;
  mqtt_cfg.credentials.authentication.password = MQTT_PASS;
  mqtt_cfg.credentials.client_id = MQTT_CLIENT_ID;
  mqtt_cfg.session.protocol_ver = MQTT_PROTOCOL_V_3_1_1;  // MQTT v3.1.1 (enabled by default)
  mqtt_cfg.network.timeout_ms = 10000;
  mqtt_cfg.session.keepalive = 120;
  mqtt_cfg.buffer.size = 2048;
  mqtt_cfg.buffer.out_size = 2048;

#if MQTT_USE_SSL
  // TLS/SSL configuration for HiveMQ Cloud Free Plan
  Serial.println("[MQTT] → Configuring TLS/SSL...");

  // HiveMQ Cloud Free Plan: Skip certificate verification
  mqtt_cfg.broker.verification.skip_cert_common_name_check = true;
  mqtt_cfg.broker.verification.certificate = NULL;
  Serial.println("[MQTT] ⚠ Certificate validation disabled (HiveMQ Free Plan)");

  Serial.println("[MQTT] ✓ TLS/SSL configured");
#endif

  // Create MQTT client
  mqttClient = esp_mqtt_client_init(&mqtt_cfg);

  if (mqttClient == nullptr) {
    Serial.println("[MQTT] ❌ Failed to create MQTT client");
    return false;
  }

  // Register event handler
  esp_mqtt_client_register_event(mqttClient, MQTT_EVENT_ANY, mqttEventHandler, this);

  Serial.println("[MQTT] ✓ MQTT v3.1.1 client initialized");
  Serial.println("[MQTT] Broker: " + String(MQTT_BROKER) + ":" + String(MQTT_PORT));
  Serial.println("[MQTT] Protocol: MQTT v3.1.1 over TLS/SSL");
  Serial.println("[MQTT] Client ID: " + String(MQTT_CLIENT_ID));
  Serial.println("[MQTT] Note: Using HiveMQ Cloud with MQTT v3.1.1");

  return true;
}

bool MQTTComm::configure() {
  Serial.println("[MQTT] Starting MQTT client...");

  if (mqttClient == nullptr) {
    Serial.println("[MQTT] ❌ Client not initialized");
    return false;
  }

  // Check network connectivity before starting MQTT
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[MQTT] ⚠ Warning: WiFi not connected - relying on PPPoS");
    // Note: If using PPPoS, this is expected and OK
  } else {
    Serial.println("[MQTT] ✓ WiFi connected: " + WiFi.localIP().toString());
  }

  esp_err_t err = esp_mqtt_client_start(mqttClient);

  if (err != ESP_OK) {
    Serial.println("[MQTT] ❌ Failed to start client, error: " + String(err));
    return false;
  }

  configured = true;
  Serial.println("[MQTT] ✓ MQTT client started (will connect asynchronously)");

  return true;
}

void MQTTComm::mqttEventHandler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
  MQTTComm *instance = static_cast<MQTTComm*>(handler_args);
  esp_mqtt_event_handle_t event = static_cast<esp_mqtt_event_handle_t>(event_data);

  switch (event_id) {
    case MQTT_EVENT_CONNECTED:
      Serial.println("[MQTT] ✓ Connected to broker");
      Serial.println("[MQTT] Session present: " + String(event->session_present));
      instance->connected = true;

      // Auto-subscribe to commands topic
      if (instance->mqttClient) {
        int msg_id = esp_mqtt_client_subscribe(instance->mqttClient, MQTT_TOPIC_COMMANDS, 0);
        Serial.println("[MQTT] Auto-subscribing to commands topic, msg_id=" + String(msg_id));
      }
      break;

    case MQTT_EVENT_DISCONNECTED:
      Serial.println("[MQTT] ⚠ Disconnected from broker");
      instance->connected = false;
      break;

    case MQTT_EVENT_SUBSCRIBED:
      Serial.println("[MQTT] ✓ Subscribed, msg_id=" + String(event->msg_id));
      break;

    case MQTT_EVENT_UNSUBSCRIBED:
      Serial.println("[MQTT] Unsubscribed, msg_id=" + String(event->msg_id));
      break;

    case MQTT_EVENT_PUBLISHED:
      Serial.println("[MQTT] ✓ Published, msg_id=" + String(event->msg_id));
      break;

    case MQTT_EVENT_DATA:
      {
        String topic = String(event->topic).substring(0, event->topic_len);
        String payload = String(event->data).substring(0, event->data_len);

        Serial.println("[MQTT] ← " + topic + ": " + payload);

        // Call user callback if set
        if (instance->messageCallback) {
          instance->messageCallback(topic, payload);
        }
      }
      break;

    case MQTT_EVENT_ERROR:
      Serial.println("[MQTT] ❌ Error occurred");
      if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
        Serial.println("[MQTT] TCP transport error");
      } else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
        Serial.println("[MQTT] Connection refused");
      }
      break;

    case MQTT_EVENT_BEFORE_CONNECT:
      Serial.println("[MQTT] → Connecting to broker...");
      break;

    default:
      Serial.println("[MQTT] Event: " + String(event_id));
      break;
  }
}

bool MQTTComm::publish(const String &topic, const String &payload, int qos) {
  if (!connected || mqttClient == nullptr) {
    Serial.println("[MQTT] ❌ Cannot publish - not connected");
    return false;
  }

  Serial.println("[MQTT] → " + topic + ": " + payload);

  int msg_id = esp_mqtt_client_publish(
    mqttClient,
    topic.c_str(),
    payload.c_str(),
    payload.length(),
    qos,
    0  // retain
  );

  if (msg_id >= 0) {
    Serial.println("[MQTT] ✓ Publish queued, msg_id=" + String(msg_id));
    return true;
  } else {
    Serial.println("[MQTT] ❌ Publish failed");
    return false;
  }
}

bool MQTTComm::subscribe(const String &topic, int qos) {
  if (!connected || mqttClient == nullptr) {
    Serial.println("[MQTT] ❌ Cannot subscribe - not connected");
    return false;
  }

  Serial.println("[MQTT] Subscribing to: " + topic);

  int msg_id = esp_mqtt_client_subscribe(mqttClient, topic.c_str(), qos);

  if (msg_id >= 0) {
    Serial.println("[MQTT] ✓ Subscribe request sent, msg_id=" + String(msg_id));
    return true;
  } else {
    Serial.println("[MQTT] ❌ Subscribe failed");
    return false;
  }
}

bool MQTTComm::isConnected() {
  return connected;
}

void MQTTComm::reconnect() {
  // ESP-IDF MQTT client handles reconnection automatically
  // This method is kept for API compatibility
  if (!connected && configured && mqttClient != nullptr) {
    Serial.println("[MQTT] → Reconnection handled automatically by ESP-IDF MQTT");
  }
}

void MQTTComm::processBackground() {
  // ESP-IDF MQTT client runs in its own task
  // No processing needed in loop() - events are handled asynchronously
  // This method is kept for API compatibility
}

void MQTTComm::setMessageCallback(MQTTMessageCallback callback) {
  messageCallback = callback;
}

void MQTTComm::disconnect() {
  if (mqttClient != nullptr) {
    Serial.println("[MQTT] Disconnecting...");
    esp_mqtt_client_stop(mqttClient);
    connected = false;
    Serial.println("[MQTT] ✓ Disconnected");
  }
}
