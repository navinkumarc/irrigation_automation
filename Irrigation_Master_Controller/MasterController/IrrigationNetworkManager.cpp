// IrrigationNetworkManager.cpp - Unified network connection manager with automatic fallback
// Renamed to avoid conflict with ESP32's NetworkManager class
#include "IrrigationNetworkManager.h"

#if ENABLE_PPPOS
  #include "PPPoSManager.h"
#endif
#if ENABLE_WIFI
  #include "WiFiComm.h"
  #include <WiFi.h>
#endif

IrrigationNetworkManager::IrrigationNetworkManager()
  : ppposManager(nullptr),
    wifiComm(nullptr),
    modemSerial(nullptr),
    activeConnection(ConnectionType::NONE),
    state(NetworkState::DISCONNECTED),
    localIP(""),
    lastConnectionAttempt(0),
    reconnectInterval(60000) {
}

void IrrigationNetworkManager::init(PPPoSManager* pppos, WiFiComm* wifi, HardwareSerial* serial) {
  Serial.println("[NetMgr] Initializing Network Manager...");

  ppposManager = pppos;
  wifiComm     = wifi;
  modemSerial  = serial;

  Serial.println("[NetMgr] ✓ Network Manager initialized");
  Serial.println("[NetMgr] Fallback order: PPPoS → WiFi");
}

bool IrrigationNetworkManager::connect(uint32_t pppos_timeout_ms, uint32_t wifi_timeout_ms) {
  Serial.println("[NetMgr] ========================================");
  Serial.println("[NetMgr] Starting network connection...");
  Serial.println("[NetMgr] ========================================");

  lastConnectionAttempt = millis();

#if ENABLE_PPPOS
  if (ppposManager != nullptr) {
    Serial.println("[NetMgr] [1/2] Attempting PPPoS connection...");
    if (tryPPPoS(pppos_timeout_ms)) {
      activeConnection = ConnectionType::PPPOS;
      state            = NetworkState::CONNECTED;
      localIP          = ppposManager->getLocalIP();

#if ENABLE_WIFI
      Serial.println("[NetMgr] → Disabling WiFi (PPPoS active)");
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
#endif

      Serial.println("[NetMgr] ========================================");
      Serial.println("[NetMgr] ✓ NETWORK CONNECTED VIA PPPOS");
      Serial.println("[NetMgr] IP Address: " + localIP);
      Serial.println("[NetMgr] ========================================");
      return true;
    }

    Serial.println("[NetMgr] ❌ PPPoS connection failed → Falling back to WiFi...");
  }
#endif

#if ENABLE_WIFI
  if (wifiComm != nullptr) {
    Serial.println("[NetMgr] [2/2] Attempting WiFi connection...");
    if (tryWiFi(wifi_timeout_ms)) {
      activeConnection = ConnectionType::WIFI;
      state            = NetworkState::CONNECTED;
      localIP          = wifiComm->getIPAddress();

      Serial.println("[NetMgr] ========================================");
      Serial.println("[NetMgr] ✓ NETWORK CONNECTED VIA WIFI");
      Serial.println("[NetMgr] SSID: " + String(WIFI_SSID));
      Serial.println("[NetMgr] IP Address: " + localIP);
      Serial.println("[NetMgr] ========================================");
      return true;
    }

    Serial.println("[NetMgr] ❌ WiFi connection failed");
  }
#endif

  // Both failed
  activeConnection = ConnectionType::NONE;
  state            = NetworkState::DISCONNECTED;
  localIP          = "";

  Serial.println("[NetMgr] ========================================");
  Serial.println("[NetMgr] ❌ ALL NETWORK CONNECTIONS FAILED");
  Serial.println("[NetMgr] → Will retry in " + String(reconnectInterval / 1000) + " seconds");
  Serial.println("[NetMgr] ========================================");
  return false;
}

bool IrrigationNetworkManager::tryPPPoS(uint32_t timeout_ms) {
#if ENABLE_PPPOS
  if (ppposManager == nullptr || modemSerial == nullptr) {
    Serial.println("[NetMgr] ❌ PPPoS manager not initialized");
    return false;
  }

  state = NetworkState::CONNECTING_PPPOS;

  Serial.println("[NetMgr] → Initializing PPPoS...");
  if (!ppposManager->init(modemSerial, PPPOS_APN)) {
    Serial.println("[NetMgr] ❌ PPPoS initialization failed");
    return false;
  }

  Serial.println("[NetMgr] → Connecting to cellular network (APN: " + String(PPPOS_APN) + ")...");

  if (ppposManager->connect(timeout_ms)) {
    Serial.println("[NetMgr] ✓ PPPoS connected, IP: " + ppposManager->getLocalIP());
    return true;
  }

  Serial.println("[NetMgr] ❌ PPPoS connection timeout");
  return false;
#else
  Serial.println("[NetMgr] ⚠ PPPoS disabled in Config.h");
  return false;
#endif
}

bool IrrigationNetworkManager::tryWiFi(uint32_t timeout_ms) {
#if ENABLE_WIFI
  if (wifiComm == nullptr) {
    Serial.println("[NetMgr] ❌ WiFi manager not initialized");
    return false;
  }

  state = NetworkState::CONNECTING_WIFI;

  Serial.println("[NetMgr] → Connecting to WiFi (SSID: " + String(WIFI_SSID) + ")...");

  if (wifiComm->init(WIFI_SSID, WIFI_PASS)) {
    Serial.println("[NetMgr] ✓ WiFi connected, IP: " + wifiComm->getIPAddress());
    return true;
  }

  Serial.println("[NetMgr] ❌ WiFi connection failed");
  return false;
#else
  Serial.println("[NetMgr] ⚠ WiFi disabled in Config.h");
  return false;
#endif
}

bool IrrigationNetworkManager::isConnected() {
  switch (activeConnection) {
    case ConnectionType::PPPOS:
#if ENABLE_PPPOS
      if (ppposManager != nullptr) {
        bool ok = ppposManager->isConnected();
        if (!ok) {
          Serial.println("[NetMgr] ⚠ PPPoS connection lost");
          state = NetworkState::DISCONNECTED;
          activeConnection = ConnectionType::NONE;
          localIP = "";
        }
        return ok;
      }
#endif
      return false;

    case ConnectionType::WIFI:
#if ENABLE_WIFI
      if (wifiComm != nullptr) {
        bool ok = wifiComm->isConnected();
        if (!ok) {
          Serial.println("[NetMgr] ⚠ WiFi connection lost");
          state = NetworkState::DISCONNECTED;
          activeConnection = ConnectionType::NONE;
          localIP = "";
        }
        return ok;
      }
#endif
      return false;

    case ConnectionType::NONE:
    default:
      return false;
  }
}

ConnectionType IrrigationNetworkManager::getConnectionType() { return activeConnection; }
String         IrrigationNetworkManager::getLocalIP()        { return localIP; }
NetworkState   IrrigationNetworkManager::getState()          { return state; }

void IrrigationNetworkManager::processBackground() {
  if (!isConnected() && state == NetworkState::DISCONNECTED) {
    if (millis() - lastConnectionAttempt >= reconnectInterval) {
      Serial.println("[NetMgr] → Attempting automatic reconnection...");
      connect();
    }
  }

#if ENABLE_PPPOS
  if (activeConnection == ConnectionType::PPPOS && ppposManager != nullptr) {
    ppposManager->loop();
  }
#endif
}

bool IrrigationNetworkManager::disconnect() {
  Serial.println("[NetMgr] Disconnecting network...");
  bool success = false;

  switch (activeConnection) {
    case ConnectionType::PPPOS:
#if ENABLE_PPPOS
      if (ppposManager != nullptr) {
        success = ppposManager->disconnect();
        Serial.println("[NetMgr] PPPoS disconnected");
      }
#endif
      break;

    case ConnectionType::WIFI:
#if ENABLE_WIFI
      if (wifiComm != nullptr) {
        wifiComm->disconnect();
        Serial.println("[NetMgr] WiFi disconnected");
        success = true;
      }
#endif
      break;

    case ConnectionType::NONE:
      Serial.println("[NetMgr] No active connection");
      success = true;
      break;
  }

  activeConnection = ConnectionType::NONE;
  state  = NetworkState::DISCONNECTED;
  localIP = "";
  return success;
}

void IrrigationNetworkManager::setReconnectInterval(unsigned long interval_ms) {
  reconnectInterval = interval_ms;
  Serial.println("[NetMgr] Reconnect interval: " + String(interval_ms / 1000) + "s");
}
