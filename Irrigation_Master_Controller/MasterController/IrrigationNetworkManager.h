// IrrigationNetworkManager.h - Unified network connection manager with automatic fallback
// Renamed to avoid conflict with ESP32's NetworkManager class
#ifndef IRRIGATION_NETWORK_MANAGER_H
#define IRRIGATION_NETWORK_MANAGER_H

#include <Arduino.h>
#include "Config.h"

// Forward declarations — actual headers included only when flags are set
class ModemPPPoS;
class WiFiComm;

enum class ConnectionType {
  NONE,
  PPPOS,
  WIFI
};

enum class NetworkState {
  DISCONNECTED,
  CONNECTING_PPPOS,
  CONNECTING_WIFI,
  CONNECTED
};

class IrrigationNetworkManager {
private:
  ModemPPPoS*   ppposManager;
  WiFiComm*       wifiComm;
  HardwareSerial* modemSerial;

  ConnectionType activeConnection;
  NetworkState   state;
  String         localIP;

  unsigned long lastConnectionAttempt;
  unsigned long reconnectInterval;

  bool tryPPPoS(uint32_t timeout_ms);
  bool tryWiFi(uint32_t timeout_ms);

public:
  IrrigationNetworkManager();

  // Initialize with PPPoS and WiFi managers (either may be nullptr if disabled)
  void init(ModemPPPoS* pppos, WiFiComm* wifi, HardwareSerial* serial);

  // Connect with automatic fallback (PPPoS first, then WiFi)
  bool connect(uint32_t pppos_timeout_ms = PPPOS_CONNECT_TIMEOUT_MS,
               uint32_t wifi_timeout_ms  = WIFI_CONNECT_TIMEOUT_MS);

  // Connection status
  bool           isConnected();
  ConnectionType getConnectionType();
  String         getLocalIP();
  NetworkState   getState();

  // Reconnection management
  void processBackground();

  // Manual control
  bool disconnect();
  void setReconnectInterval(unsigned long interval_ms);
};

#endif
