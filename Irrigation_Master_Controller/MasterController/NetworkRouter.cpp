// NetworkRouter.cpp  —  Internet bearer management
// Single owner of PPPoS and WiFi: init, connect, liveness, processBackground.
#include "NetworkRouter.h"
#include "CommConfig.h"

#if ENABLE_PPPOS
  #include "ModemPPPoS.h"
  extern ModemPPPoS modemPPPoS;
#endif
#if ENABLE_WIFI
  #include "WiFiComm.h"
  extern WiFiComm wifiComm;
#endif

NetworkRouter networkRouter;

// ─── registerBearers() ────────────────────────────────────────────────────────
void NetworkRouter::registerBearers(ModemPPPoS *ppposModule, WiFiComm *wifiModule) {
  pppos = ppposModule;
  wifi  = wifiModule;
  Serial.println("[Router] Bearers registered:");
  Serial.printf ("[Router]   PPPoS : %s\n", pppos ? "present" : "not compiled");
  Serial.printf ("[Router]   WiFi  : %s\n", wifi  ? "present" : "not compiled");
}

// ─── begin() ──────────────────────────────────────────────────────────────────
// Decides which bearer(s) to bring up from runtime CommConfig flags.
// Priority: PPPoS = primary, WiFi = fallback.
bool NetworkRouter::begin() {
  Serial.println("[Router] ============================================");
  Serial.println("[Router] Bearer bring-up...");
  Serial.printf ("[Router]   PPPoS enabled : %s\n", commCfg.enablePPPoS ? "YES" : "NO");
  Serial.printf ("[Router]   WiFi  enabled : %s\n", commCfg.enableWiFi  ? "YES" : "NO");
  state = RouterState::CONNECTING;
  lastReconnectAttempt = millis();

  // ── Case 1: PPPoS only ───────────────────────────────────────────────────
  if (commCfg.enablePPPoS && !commCfg.enableWiFi) {
#if ENABLE_PPPOS
    Serial.println("[Router] Mode: PPPoS only");
    if (bringUpPPPoS()) {
      ppposState = ConnectionType::PPPOS;
      Serial.println("[Router] ✓ PPPoS up  IP: " + getPPPoSIP());
    } else {
      Serial.println("[Router] ❌ PPPoS failed — no internet");
    }
#else
    Serial.println("[Router] ❌ PPPoS selected but ENABLE_PPPOS=0");
#endif
  }

  // ── Case 2: WiFi only ────────────────────────────────────────────────────
  else if (commCfg.enableWiFi && !commCfg.enablePPPoS) {
#if ENABLE_WIFI
    Serial.println("[Router] Mode: WiFi only");
    if (bringUpWiFi()) {
      wifiState = ConnectionType::WIFI;
      Serial.println("[Router] ✓ WiFi up  IP: " + getWiFiIP());
    } else {
      Serial.println("[Router] ❌ WiFi failed — no internet");
    }
#else
    Serial.println("[Router] ❌ WiFi selected but ENABLE_WIFI=0");
#endif
  }

  // ── Case 3: Both — PPPoS primary, WiFi fallback ───────────────────────────
  else if (commCfg.enablePPPoS && commCfg.enableWiFi) {
    Serial.println("[Router] Mode: PPPoS primary, WiFi fallback");
#if ENABLE_PPPOS
    if (bringUpPPPoS()) {
      ppposState = ConnectionType::PPPOS;
      Serial.println("[Router] ✓ PPPoS up (primary)  IP: " + getPPPoSIP());
    } else {
      Serial.println("[Router] ✗ PPPoS failed — trying WiFi fallback");
#if ENABLE_WIFI
      if (bringUpWiFi()) {
        wifiState = ConnectionType::WIFI;
        Serial.println("[Router] ✓ WiFi up (fallback)  IP: " + getWiFiIP());
      } else {
        Serial.println("[Router] ❌ WiFi fallback also failed — no internet");
      }
#else
      Serial.println("[Router] ❌ WiFi fallback not compiled in");
#endif
    }
#elif ENABLE_WIFI
    // PPPoS not compiled — use WiFi
    if (bringUpWiFi()) {
      wifiState = ConnectionType::WIFI;
      Serial.println("[Router] ✓ WiFi up  IP: " + getWiFiIP());
    } else {
      Serial.println("[Router] ❌ WiFi failed");
    }
#endif
  }

  // ── Case 4: Neither enabled ───────────────────────────────────────────────
  else {
    Serial.println("[Router] ❌ No bearer enabled in CommConfig");
  }

  bool anyUp = isAnyUp();
  state = anyUp ? RouterState::CONNECTED : RouterState::FAILED;
  printStatus();
  Serial.println("[Router] ============================================");
  return anyUp;
}

// ─── disconnect() ─────────────────────────────────────────────────────────────
void NetworkRouter::disconnect() {
  Serial.println("[Router] Disconnecting all bearers...");
#if ENABLE_PPPOS
  if (ppposState == ConnectionType::PPPOS && pppos) {
    pppos->disconnect();
    ppposState = ConnectionType::NONE;
    Serial.println("[Router] ✓ PPPoS disconnected");
  }
#endif
#if ENABLE_WIFI
  if (wifiState == ConnectionType::WIFI && wifi) {
    wifi->disconnect();
    wifiState = ConnectionType::NONE;
    Serial.println("[Router] ✓ WiFi disconnected");
  }
#endif
  state = RouterState::IDLE;
}

// ─── processBackground() ──────────────────────────────────────────────────────
// Single call from CommManager::process() — owns everything about bearers.
void NetworkRouter::processBackground() {
  // 1. Feed PPP stack — must run every loop when PPPoS is active
#if ENABLE_PPPOS
  if (ppposState == ConnectionType::PPPOS && pppos)
    pppos->loop();
#endif

  // 2. Drive WiFi background (reconnect, DHCP, etc.)
#if ENABLE_WIFI
  if (wifi)
    wifi->processBackground();
#endif

  // 3. Liveness checks — mark bearers down if they dropped
  if (!checkPPPoS() && ppposState == ConnectionType::PPPOS)
    ppposState = ConnectionType::NONE;
  if (!checkWiFi()  && wifiState  == ConnectionType::WIFI)
    wifiState  = ConnectionType::NONE;

  // 4. Auto-reconnect when all bearers are down
  if (!isAnyUp()) {
    if (state == RouterState::CONNECTED) {
      Serial.println("[Router] ⚠ All bearers down — scheduling reconnect");
      state = RouterState::RECONNECTING;
    }
    if (millis() - lastReconnectAttempt >= reconnectInterval) {
      Serial.println("[Router] → Reconnect attempt...");
      begin();
    }
  } else {
    state = RouterState::CONNECTED;
  }
}

// ─── getProviderFor() ─────────────────────────────────────────────────────────
// PPPoS preferred over WiFi (primary/fallback rule).
NetworkRouter::ServiceRoute NetworkRouter::getProviderFor(Service svc) {
  ServiceRoute r;
  r.service = svc;
  r.bearer  = ConnectionType::NONE;

  if (isPPPoSUp()) {
    r.bearer  = ConnectionType::PPPOS;
    r.localIP = getPPPoSIP();
  } else if (isWiFiUp()) {
    r.bearer       = ConnectionType::WIFI;
    r.localIP      = getWiFiIP();
    r.isOnFallback = true;
    Serial.printf("[Router] ⚠ %s on WiFi fallback (PPPoS down)\n",
                  svc == Service::MQTT ? "MQTT" : "HTTP");
  } else {
    Serial.printf("[Router] ❌ No bearer for %s\n",
                  svc == Service::MQTT ? "MQTT" : "HTTP");
  }
  return r;
}

bool NetworkRouter::isServiceAvailable(Service svc) {
  return getProviderFor(svc).bearer != ConnectionType::NONE;
}

// ─── Bearer bring-up ──────────────────────────────────────────────────────────
bool NetworkRouter::bringUpPPPoS() {
#if ENABLE_PPPOS
  if (!pppos) return false;
  Serial.printf("[Router]   APN: %s\n", commCfg.cellularAPN.c_str());
  if (!pppos->init())                             return false;
  return pppos->connect(PPPOS_CONNECT_TIMEOUT_MS);
#else
  return false;
#endif
}

bool NetworkRouter::bringUpWiFi() {
#if ENABLE_WIFI
  if (!wifi) return false;
  Serial.printf("[Router]   SSID: %s\n", commCfg.wifiSSID.c_str());
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
  if (!up && ppposState == ConnectionType::PPPOS)
    Serial.println("[Router] ⚠ PPPoS link lost");
  return up;
#else
  return false;
#endif
}

bool NetworkRouter::checkWiFi() {
#if ENABLE_WIFI
  if (!wifi) return false;
  bool up = wifi->isConnected();
  if (!up && wifiState == ConnectionType::WIFI)
    Serial.println("[Router] ⚠ WiFi link lost");
  return up;
#else
  return false;
#endif
}

// ─── Status ───────────────────────────────────────────────────────────────────
bool        NetworkRouter::isPPPoSUp()  const { return ppposState == ConnectionType::PPPOS; }
bool        NetworkRouter::isWiFiUp()   const { return wifiState  == ConnectionType::WIFI;  }
bool        NetworkRouter::isAnyUp()    const { return isPPPoSUp() || isWiFiUp(); }
RouterState NetworkRouter::getState()   const { return state; }
bool        NetworkRouter::isBearerUp(ConnectionType b) const {
  if (b == ConnectionType::PPPOS) return isPPPoSUp();
  if (b == ConnectionType::WIFI)  return isWiFiUp();
  return false;
}

String NetworkRouter::getPPPoSIP() const {
#if ENABLE_PPPOS
  return (pppos && isPPPoSUp()) ? pppos->getLocalIP() : "";
#else
  return "";
#endif
}

String NetworkRouter::getWiFiIP() const {
#if ENABLE_WIFI
  return (wifi && isWiFiUp()) ? wifi->getIPAddress() : "";
#else
  return "";
#endif
}

String NetworkRouter::getActiveIP() const {
  if (isPPPoSUp()) return getPPPoSIP();
  if (isWiFiUp())  return getWiFiIP();
  return "";
}

void NetworkRouter::setReconnectInterval(unsigned long ms) {
  reconnectInterval = ms;
}

const char* NetworkRouter::bearerName(ConnectionType b) const {
  switch (b) {
    case ConnectionType::PPPOS: return "PPPoS";
    case ConnectionType::WIFI:  return "WiFi";
    default:                    return "None";
  }
}

void NetworkRouter::printStatus() const {
  const char *stStr =
    state == RouterState::IDLE         ? "IDLE"         :
    state == RouterState::CONNECTING   ? "CONNECTING"   :
    state == RouterState::CONNECTED    ? "CONNECTED"    :
    state == RouterState::RECONNECTING ? "RECONNECTING" : "FAILED";

  Serial.println("[Router] ===== NetworkRouter =====");
  Serial.printf ("[Router]  State : %s\n",    stStr);
  Serial.printf ("[Router]  PPPoS : %s  IP: %s\n",
                 isPPPoSUp() ? "UP" : "DOWN", getPPPoSIP().c_str());
  Serial.printf ("[Router]  WiFi  : %s  IP: %s\n",
                 isWiFiUp()  ? "UP" : "DOWN", getWiFiIP().c_str());
  Serial.printf ("[Router]  Active IP: %s\n", getActiveIP().c_str());
  Serial.println("[Router] ==========================");
}
