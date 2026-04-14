// NetworkRouter.h  —  Internet bearer management (PPPoS and WiFi)
//
// NetworkRouter is the single owner of all internet bearer logic.
// CommManager calls only begin() and processBackground().
//
// ── Responsibilities ──────────────────────────────────────────────────────────
//   • Owns PPPoS and WiFi hardware modules (holds pointers)
//   • Decides which bearer(s) to bring up based on runtime CommConfig flags
//   • Priority rule: PPPoS = primary, WiFi = fallback
//       PPPoS only enabled  → use PPPoS
//       WiFi  only enabled  → use WiFi
//       Both  enabled       → try PPPoS first, fall back to WiFi
//       Neither enabled     → no internet
//   • Provides IP connectivity for MQTT and HTTP
//   • Drives PPP stack (pppos->loop()) every processBackground() call
//   • Drives WiFi reconnect (wifi->processBackground()) every call
//   • Auto-reconnects dropped bearers after reconnectInterval
//
// ── CommManager interface (the only surface CommManager touches) ──────────────
//   networkRouter.begin()              — init bearers per CommConfig, connect
//   networkRouter.processBackground()  — call every loop(); feeds all bearers
//   networkRouter.isAnyUp()            — at least one bearer connected
//   networkRouter.isPPPoSUp()          — PPPoS specifically up
//   networkRouter.isWiFiUp()           — WiFi specifically up
//   networkRouter.getActiveIP()        — IP from whichever bearer is up
//   networkRouter.printStatus()        — diagnostics

#ifndef NETWORK_ROUTER_H
#define NETWORK_ROUTER_H

#include <Arduino.h>
#include "Config.h"
#include "CommConfig.h"

// Forward declarations
class ModemPPPoS;
class WiFiComm;

// ─── Bearer identifier ────────────────────────────────────────────────────────
enum class ConnectionType { NONE, PPPOS, WIFI };

// ─── Router state ─────────────────────────────────────────────────────────────
enum class RouterState { IDLE, CONNECTING, CONNECTED, RECONNECTING, FAILED };

// ─── NetworkRouter ────────────────────────────────────────────────────────────
class NetworkRouter {
public:
  // Service tokens used by getProviderFor()
  enum class Service { MQTT, HTTP };

  struct ServiceRoute {
    Service        service;
    ConnectionType bearer;
    String         localIP;
    bool           isOnFallback = false;
  };

private:
  ModemPPPoS    *pppos = nullptr;
  WiFiComm      *wifi  = nullptr;

  ConnectionType ppposState = ConnectionType::NONE;
  ConnectionType wifiState  = ConnectionType::NONE;
  RouterState    state      = RouterState::IDLE;

  unsigned long lastReconnectAttempt = 0;
  unsigned long reconnectInterval    = NETWORK_RECONNECT_INTERVAL_MS;

  // Internal bearer bring-up / tear-down
  bool bringUpPPPoS();
  bool bringUpWiFi();
  void tearDownPPPoS();
  bool checkPPPoS();
  bool checkWiFi();
  bool isBearerUp(ConnectionType b) const;
  const char* bearerName(ConnectionType b) const;

public:
  NetworkRouter() = default;

  // ── Lifecycle ─────────────────────────────────────────────────────────────

  // Register bearer modules. Called once during CommManager startup.
  // Pass nullptr for a module that is not compiled in.
  void registerBearers(ModemPPPoS *ppposModule, WiFiComm *wifiModule);

  // Initialise and connect bearers according to CommConfig runtime flags.
  //   PPPoS only: bring up PPPoS
  //   WiFi  only: bring up WiFi
  //   Both:       try PPPoS first (primary), fall back to WiFi
  // Returns true if at least one bearer came up.
  bool begin();

  // Tear down all active bearers.
  void disconnect();

  // ── Background — call every loop() ───────────────────────────────────────

  // • Feeds PPP stack bytes (pppos->loop()) when PPPoS active
  // • Drives WiFi reconnect (wifi->processBackground()) when WiFi registered
  // • Checks bearer liveness; drops stale bearer state
  // • Triggers auto-reconnect when all bearers are down
  void processBackground();

  // ── Service routing ───────────────────────────────────────────────────────

  // Returns the best available bearer for a service (PPPoS preferred over WiFi).
  ServiceRoute getProviderFor(Service svc);
  bool         isServiceAvailable(Service svc);

  // ── Status ───────────────────────────────────────────────────────────────
  bool        isPPPoSUp()     const;
  bool        isWiFiUp()      const;
  bool        isAnyUp()       const;
  RouterState getState()      const;
  String      getPPPoSIP()    const;
  String      getWiFiIP()     const;
  String      getActiveIP()   const;   // IP of whichever bearer is up (PPPoS preferred)

  void setReconnectInterval(unsigned long ms);
  void printStatus() const;
};

// Global instance
extern NetworkRouter networkRouter;

#endif // NETWORK_ROUTER_H
