// WiFiComm.cpp - WiFi connectivity management module
#include "WiFiComm.h"

// Global singleton instance — extern declared in WiFiComm.h
WiFiComm wifiComm;

WiFiComm::WiFiComm() : initialized(false), connected(false), lastConnectionAttempt(0),
                       lastStatusCheck(0), reconnectAttempts(0) {
}

bool WiFiComm::init(const String &wifiSsid, const String &wifiPassword) {
  ssid = wifiSsid;
  password = wifiPassword;

  Serial.println("[WiFi] Initializing WiFi...");
  Serial.println("[WiFi] SSID: " + ssid);

  // Set WiFi mode to station
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);

  // Attempt to connect
  if (connectToWiFi()) {
    initialized = true;
    Serial.println("[WiFi] ✓ WiFi initialized successfully");
    Serial.println("[WiFi] IP Address: " + getIPAddress());
    return true;
  } else {
    Serial.println("[WiFi] ⚠ Initial connection failed, will retry in background");
    initialized = true;  // Still mark as initialized so background reconnect can work
    return false;
  }
}

bool WiFiComm::connectToWiFi() {
  Serial.println("[WiFi] Connecting to WiFi...");

  WiFi.begin(ssid.c_str(), password.c_str());

  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED &&
         millis() - startAttempt < WIFI_CONNECT_TIMEOUT_MS) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    connected = true;
    reconnectAttempts = 0;
    Serial.println("[WiFi] ✓ Connected successfully");
    Serial.println("[WiFi] IP: " + WiFi.localIP().toString());
    Serial.println("[WiFi] Signal: " + String(WiFi.RSSI()) + " dBm");
    return true;
  } else {
    connected = false;
    reconnectAttempts++;
    Serial.println("[WiFi] ✗ Connection failed (attempt " + String(reconnectAttempts) + ")");
    return false;
  }
}

bool WiFiComm::isConnected() {
  return connected && (WiFi.status() == WL_CONNECTED);
}

bool WiFiComm::isReady() {
  return initialized && isConnected();
}

String WiFiComm::getStatus() {
  if (!initialized) {
    return "Not initialized";
  }

  if (isConnected()) {
    return "Connected to " + ssid + " (IP: " + getIPAddress() + ", RSSI: " + String(getSignalStrength()) + " dBm)";
  } else {
    return "Disconnected (Attempts: " + String(reconnectAttempts) + ")";
  }
}

String WiFiComm::getIPAddress() {
  if (isConnected()) {
    return WiFi.localIP().toString();
  }
  return "0.0.0.0";
}

int WiFiComm::getSignalStrength() {
  if (isConnected()) {
    return WiFi.RSSI();
  }
  return 0;
}

void WiFiComm::checkConnection() {
  // Check if WiFi is still connected
  if (WiFi.status() != WL_CONNECTED) {
    if (connected) {
      Serial.println("[WiFi] ⚠ Connection lost");
      connected = false;
    }
  } else {
    if (!connected) {
      Serial.println("[WiFi] ✓ Connection restored");
      connected = true;
      reconnectAttempts = 0;
    }
  }
}

void WiFiComm::processBackground() {
  if (!initialized) {
    return;
  }

  // Don't auto-reconnect if WiFi was intentionally disabled (e.g., PPPoS is active)
  if (WiFi.getMode() == WIFI_OFF) {
    return;
  }

  unsigned long now = millis();

  // Periodic status check
  if (now - lastStatusCheck > STATUS_CHECK_INTERVAL_MS) {
    lastStatusCheck = now;
    checkConnection();
  }

  // Auto-reconnect if disconnected
  if (!isConnected() && reconnectAttempts < MAX_RECONNECT_ATTEMPTS) {
    if (now - lastConnectionAttempt > RECONNECT_DELAY_MS) {
      lastConnectionAttempt = now;
      Serial.println("[WiFi] → Attempting reconnection...");
      connectToWiFi();
    }
  }
}

void WiFiComm::disconnect() {
  if (initialized) {
    WiFi.disconnect();
    connected = false;
    Serial.println("[WiFi] Disconnected");
  }
}

bool WiFiComm::reconnect() {
  if (!initialized) {
    return false;
  }

  Serial.println("[WiFi] Manual reconnection requested");
  reconnectAttempts = 0;  // Reset attempt counter
  return connectToWiFi();
}

bool WiFiComm::hasCommands() {
  return !pendingCommands.empty();
}

std::vector<WiFiCommand> WiFiComm::getCommands() {
  return pendingCommands;
}

void WiFiComm::clearCommands() {
  pendingCommands.clear();
}

void WiFiComm::queueCommand(const String &command, const String &source) {
  WiFiCommand cmd;
  cmd.command = command;
  cmd.source = source;
  cmd.timestamp = millis();
  pendingCommands.push_back(cmd);
  Serial.println("[WiFi] Command queued: " + command + " (from: " + source + ")");
}
