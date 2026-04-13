// NetworkRouter.cpp - Data provider selection and fallback management
#include "NetworkRouter.h"
#include "CommConfig.h"

#if ENABLE_PPPOS
  #include "ModemPPPoS.h"
#endif
#if ENABLE_WIFI
  #include "WiFiComm.h"
  #include <WiFi.h>
#endif

// ─── Global instance ──────────────────────────────────────────────────────────
NetworkRouter networkRouter;

// ─── Constructor ──────────────────────────────────────────────────────────────
NetworkRouter::NetworkRouter()
  : pppos(nullptr), wifi(nullptr),
    ppposState(ConnectionType::NONE),
    wifiState(ConnectionType::NONE),
    state(RouterState::IDLE),
    lastReconnectAttempt(0),
    reconnectInterval(NETWORK_RECONNECT_INTERVAL_MS) {}

// ─── init() ───────────────────────────────────────────────────────────────────
void NetworkRouter::init(ModemPPPoS* ppposModule, WiFiComm* wifiModule) {
  pppos = ppposModule;
  wifi  = wifiModule;
  Serial.println("[Router] ✓ NetworkRouter initialized");
  Serial.printf("[Router] MQTT  primary: %s  fallback: %s\n",
    bearerName(preferredBearerFor(Service::MQTT)),
    bearerName(fallbackBearerFor (Service::MQTT)));
  Serial.printf("[Router] HTTP  primary: %s  fallback: %s\n",
    bearerName(preferredBearerFor(Service::HTTP)),
    bearerName(fallbackBearerFor (Service::HTTP)));
}

// ─── connect() ────────────────────────────────────────────────────────────────
// Determines which bearers are needed by the configured services and brings
// them up. A bearer is attempted only when at least one service lists it as
// primary or fallback.
bool NetworkRouter::connect() {
  Serial.println("[Router] ==========================================");
  Serial.println("[Router] Starting bearer bring-up...");
  state = RouterState::CONNECTING;
  lastReconnectAttempt = millis();

  bool needPPPoS = false;
  bool needWiFi  = false;

  // Collect which bearers are needed
  for (auto svc : { Service::MQTT, Service::HTTP }) {
    if (preferredBearerFor(svc) == ConnectionType::PPPOS ||
        fallbackBearerFor (svc) == ConnectionType::PPPOS) needPPPoS = true;
    if (preferredBearerFor(svc) == ConnectionType::WIFI  ||
        fallbackBearerFor (svc) == ConnectionType::WIFI)  needWiFi  = true;
  }

  // Bring up PPPoS if needed
  if (needPPPoS) {
#if ENABLE_PPPOS
    if (pppos != nullptr) {
      Serial.println("[Router] → Bringing up PPPoS bearer...");
      if (bringUpPPPoS(PPPOS_CONNECT_TIMEOUT_MS)) {
        ppposState = ConnectionType::PPPOS;
        Serial.println("[Router] ✓ PPPoS up: " + getPPPoSIP());
      } else {
        Serial.println("[Router] ❌ PPPoS bring-up failed");
      }
    }
#else
    Serial.println("[Router] ⚠ PPPoS needed but ENABLE_PPPOS = 0");
#endif
  }

  // Bring up WiFi if needed
  if (needWiFi) {
#if ENABLE_WIFI
    if (wifi != nullptr) {
      Serial.println("[Router] → Bringing up WiFi bearer...");
      if (bringUpWiFi(WIFI_CONNECT_TIMEOUT_MS)) {
        wifiState = ConnectionType::WIFI;
        Serial.println("[Router] ✓ WiFi up: " + getWiFiIP());
      } else {
        Serial.println("[Router] ❌ WiFi bring-up failed");
      }
    }
#else
    Serial.println("[Router] ⚠ WiFi needed but ENABLE_WIFI = 0");
#endif
  }

  bool anyUp = isAnyUp();
  state = anyUp ? RouterState::CONNECTED : RouterState::FAILED;

  Serial.println("[Router] ------------------------------------------");
  printStatus();
  Serial.println("[Router] ==========================================");
  return anyUp;
}

// ─── disconnect() ─────────────────────────────────────────────────────────────
bool NetworkRouter::disconnect() {
  Serial.println("[Router] Disconnecting all bearers...");
#if ENABLE_PPPOS
  if (ppposState == ConnectionType::PPPOS && pppos != nullptr) {
    pppos->disconnect();
    ppposState = ConnectionType::NONE;
    Serial.println("[Router] ✓ PPPoS disconnected");
  }
#endif
#if ENABLE_WIFI
  if (wifiState == ConnectionType::WIFI && wifi != nullptr) {
    wifi->disconnect();
    wifiState = ConnectionType::NONE;
    Serial.println("[Router] ✓ WiFi disconnected");
  }
#endif
  state = RouterState::IDLE;
  return true;
}

// ─── getProviderFor() ─────────────────────────────────────────────────────────
// Returns the best available bearer for a service:
//   1. Primary bearer if up
//   2. Fallback bearer if up
//   3. NONE if neither is available
NetworkRouter::ServiceRoute NetworkRouter::getProviderFor(Service svc) {
  ServiceRoute route;
  route.service      = svc;
  route.bearer       = ConnectionType::NONE;
  route.isOnFallback = false;

  ConnectionType primary  = preferredBearerFor(svc);
  ConnectionType fallback = fallbackBearerFor(svc);

  if (isBearerUp(primary)) {
    route.bearer  = primary;
    route.localIP = (primary == ConnectionType::PPPOS) ? getPPPoSIP() : getWiFiIP();
  } else if (isBearerUp(fallback)) {
    route.bearer       = fallback;
    route.localIP      = (fallback == ConnectionType::PPPOS) ? getPPPoSIP() : getWiFiIP();
    route.isOnFallback = true;
    Serial.printf("[Router] ⚠ %s using fallback bearer (%s — primary %s is down)\n",
      (svc == Service::MQTT) ? "MQTT" : "HTTP",
      bearerName(fallback), bearerName(primary));
  } else {
    Serial.printf("[Router] ❌ No bearer available for %s (primary: %s, fallback: %s)\n",
      (svc == Service::MQTT) ? "MQTT" : "HTTP",
      bearerName(primary), bearerName(fallback));
  }

  return route;
}

bool NetworkRouter::isServiceAvailable(Service svc) {
  return getProviderFor(svc).bearer != ConnectionType::NONE;
}

// ─── Bearer bring-up helpers ──────────────────────────────────────────────────
bool NetworkRouter::bringUpPPPoS(uint32_t timeout_ms) {
#if ENABLE_PPPOS
  if (!pppos) return false;
  if (!pppos->init()) return false;
  return pppos->connect(timeout_ms);
#else
  return false;
#endif
}

bool NetworkRouter::bringUpWiFi(uint32_t timeout_ms) {
#if ENABLE_WIFI
  if (!wifi) return false;
  return wifi->init(commCfg.wifiSSID, commCfg.wifiPass);
#else
  return false;
#endif
}

void NetworkRouter::tearDownPPPoS() {
#if ENABLE_PPPOS
  if (pppos && ppposState == ConnectionType::PPPOS) {
    pppos->disconnect();
    ppposState = ConnectionType::NONE;
  }
#endif
}

// ─── Liveness checks ──────────────────────────────────────────────────────────
bool NetworkRouter::checkPPPoS() {
#if ENABLE_PPPOS
  if (!pppos) return false;
  bool up = pppos->isConnected();
  if (!up && ppposState == ConnectionType::PPPOS) {
    Serial.println("[Router] ⚠ PPPoS link lost");
    ppposState = ConnectionType::NONE;
  }
  return up;
#else
  return false;
#endif
}

bool NetworkRouter::checkWiFi() {
#if ENABLE_WIFI
  if (!wifi) return false;
  bool up = wifi->isConnected();
  if (!up && wifiState == ConnectionType::WIFI) {
    Serial.println("[Router] ⚠ WiFi link lost");
    wifiState = ConnectionType::NONE;
  }
  return up;
#else
  return false;
#endif
}

// ─── processBackground() ──────────────────────────────────────────────────────
void NetworkRouter::processBackground() {
  // Feed PPP stack — MUST run every loop iteration when PPPoS is active
#if ENABLE_PPPOS
  if (ppposState == ConnectionType::PPPOS && pppos != nullptr) {
    pppos->loop();
  }
#endif

#if ENABLE_WIFI
  if (wifi != nullptr) {
    wifi->processBackground();
  }
#endif

  // Liveness checks — update bearer states
  bool ppposUp = checkPPPoS();
  bool wifiUp  = checkWiFi();

  if (ppposState == ConnectionType::PPPOS && !ppposUp) ppposState = ConnectionType::NONE;
  if (wifiState  == ConnectionType::WIFI  && !wifiUp)  wifiState  = ConnectionType::NONE;

  // Update top-level state
  if (!isAnyUp()) {
    if (state == RouterState::CONNECTED) {
      Serial.println("[Router] ⚠ All bearers down — will reconnect");
      state = RouterState::RECONNECTING;
    }
    // Auto-reconnect after interval
    if (millis() - lastReconnectAttempt >= reconnectInterval) {
      Serial.println("[Router] → Auto-reconnect attempt...");
      connect();
    }
  } else {
    state = RouterState::CONNECTED;
  }
}

// ─── Routing resolution helpers ───────────────────────────────────────────────
// These map Config.h macro values (NET_BEARER_PPPOS / NET_BEARER_WIFI)
// to runtime ConnectionType. The fallback is always the other bearer.

ConnectionType NetworkRouter::preferredBearerFor(Service svc) const {
  int pref = (svc == Service::MQTT) ? MQTT_PRIMARY_BEARER : HTTP_PRIMARY_BEARER;
  return (pref == NET_BEARER_PPPOS) ? ConnectionType::PPPOS : ConnectionType::WIFI;
}

ConnectionType NetworkRouter::fallbackBearerFor(Service svc) const {
  ConnectionType primary = preferredBearerFor(svc);
  return (primary == ConnectionType::PPPOS) ? ConnectionType::WIFI : ConnectionType::PPPOS;
}

bool NetworkRouter::isBearerUp(ConnectionType b) const {
  if (b == ConnectionType::PPPOS) return ppposState == ConnectionType::PPPOS;
  if (b == ConnectionType::WIFI)  return wifiState  == ConnectionType::WIFI;
  return false;
}

// ─── Status accessors ─────────────────────────────────────────────────────────
bool        NetworkRouter::isPPPoSUp()  const { return ppposState == ConnectionType::PPPOS; }
bool        NetworkRouter::isWiFiUp()   const { return wifiState  == ConnectionType::WIFI; }
bool        NetworkRouter::isAnyUp()    const { return isPPPoSUp() || isWiFiUp(); }
RouterState NetworkRouter::getState()   const { return state; }

String NetworkRouter::getPPPoSIP() const {
#if ENABLE_PPPOS
  return (pppos && ppposState == ConnectionType::PPPOS) ? pppos->getLocalIP() : "";
#else
  return "";
#endif
}

String NetworkRouter::getWiFiIP() const {
#if ENABLE_WIFI
  return (wifi && wifiState == ConnectionType::WIFI) ? wifi->getIPAddress() : "";
#else
  return "";
#endif
}

void NetworkRouter::setReconnectInterval(unsigned long ms) {
  reconnectInterval = ms;
  Serial.println("[Router] Reconnect interval: " + String(ms / 1000) + "s");
}

const char* NetworkRouter::bearerName(ConnectionType b) const {
  switch (b) {
    case ConnectionType::PPPOS: return "PPPoS";
    case ConnectionType::WIFI:  return "WiFi";
    default:                    return "None";
  }
}

// ─── printStatus() ────────────────────────────────────────────────────────────
void NetworkRouter::printStatus() const {
  const char* stateStr =
    state == RouterState::IDLE         ? "IDLE"         :
    state == RouterState::CONNECTING   ? "CONNECTING"   :
    state == RouterState::CONNECTED    ? "CONNECTED"    :
    state == RouterState::RECONNECTING ? "RECONNECTING" : "FAILED";

  Serial.println("[Router] ===== NetworkRouter Status =====");
  Serial.printf("[Router]  State:       %s\n",    stateStr);
  Serial.printf("[Router]  PPPoS:       %s  IP: %s\n",
    isPPPoSUp() ? "UP" : "DOWN", getPPPoSIP().c_str());
  Serial.printf("[Router]  WiFi:        %s  IP: %s\n",
    isWiFiUp()  ? "UP" : "DOWN", getWiFiIP().c_str());
  Serial.printf("[Router]  MQTT bearer: primary=%s  fallback=%s\n",
    bearerName(preferredBearerFor(Service::MQTT)),
    bearerName(fallbackBearerFor (Service::MQTT)));
  Serial.printf("[Router]  HTTP bearer: primary=%s  fallback=%s\n",
    bearerName(preferredBearerFor(Service::HTTP)),
    bearerName(fallbackBearerFor (Service::HTTP)));
  Serial.println("[Router] ================================");
}
