// WiFiComm.h - WiFi connectivity management module
#ifndef WIFI_COMM_H
#define WIFI_COMM_H

#include <Arduino.h>
#include <WiFi.h>
#include "Config.h"

// WiFi command structure
struct WiFiCommand {
  String command;
  String source;  // Source identifier
  unsigned long timestamp;
};

class WiFiComm {
private:
  bool initialized;
  bool connected;
  String ssid;
  String password;
  unsigned long lastConnectionAttempt;
  unsigned long lastStatusCheck;
  int reconnectAttempts;

  static const int MAX_RECONNECT_ATTEMPTS = 5;
  static const unsigned long RECONNECT_DELAY_MS = 5000;
  static const unsigned long STATUS_CHECK_INTERVAL_MS = 10000;

  std::vector<WiFiCommand> pendingCommands;

  bool connectToWiFi();
  void checkConnection();

public:
  WiFiComm();

  // Initialize WiFi with credentials
  bool init(const String &ssid, const String &password);

  // Check if WiFi is connected
  bool isConnected();

  // Check if WiFi is initialized
  bool isReady();

  // Get WiFi status information
  String getStatus();

  // Get IP address
  String getIPAddress();

  // Get signal strength (RSSI)
  int getSignalStrength();

  // Process background tasks (reconnection, status checks)
  void processBackground();

  // Disconnect WiFi
  void disconnect();

  // Reconnect WiFi
  bool reconnect();

  // Command queue management
  bool hasCommands();
  std::vector<WiFiCommand> getCommands();
  void clearCommands();
  void queueCommand(const String &command, const String &source);
};

#endif // WIFI_COMM_H
