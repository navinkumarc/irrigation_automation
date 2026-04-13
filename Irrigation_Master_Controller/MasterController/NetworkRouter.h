// NetworkRouter.h - Data provider selection and fallback management
//
// Responsibilities:
//   • Manages the active data bearer: PPPoS (cellular) or WiFi
//   • Per-service provider selection (MQTT, HTTP/REST)
//       – Primary and fallback providers are configured in Config.h
//       – At runtime, NetworkRouter picks the best available bearer
//         for each service and exposes it via getProviderFor()
//   • Automatic fallback: if the primary bearer is unavailable,
//     the fallback bearer is tried transparently
//   • Auto-reconnect loop via processBackground()
//   • Feeds the PPP stack (calls modemPPPoS.loop()) when PPPoS is active
//
// Bearer options (set in Config.h):
//   NET_BEARER_PPPOS = 1  — cellular data via ModemPPPoS
//   NET_BEARER_WIFI  = 2  — on-board WiFi via WiFiComm
//
// Per-service primary bearer (set in Config.h):
//   MQTT_PRIMARY_BEARER  — NET_BEARER_PPPOS or NET_BEARER_WIFI
//   HTTP_PRIMARY_BEARER  — NET_BEARER_PPPOS or NET_BEARER_WIFI
//
// Service lookup:
//   NetworkRouter::Service::MQTT
//   NetworkRouter::Service::HTTP
//   router.getProviderFor(Service::MQTT)  → ConnectionType (PPPOS / WIFI / NONE)

#ifndef NETWORK_ROUTER_H
#define NETWORK_ROUTER_H

#include <Arduino.h>
#include "Config.h"

// Forward declarations — real headers included only when flags are set
class ModemPPPoS;
class WiFiComm;

// ─── Bearer identifiers ───────────────────────────────────────────────────────

// Active data bearer
enum class ConnectionType {
  NONE,    // No bearer available
  PPPOS,   // Cellular PPP via ModemPPPoS
  WIFI     // On-board WiFi via WiFiComm
};

// Logical bearer preference token (maps to ConnectionType at runtime)
// Used as a value in Config.h macros
#define NET_BEARER_PPPOS  1
#define NET_BEARER_WIFI   2

// ─── Router states ────────────────────────────────────────────────────────────

enum class RouterState {
  IDLE,
  CONNECTING,
  CONNECTED,
  RECONNECTING,
  FAILED
};

// ─── NetworkRouter ────────────────────────────────────────────────────────────

class NetworkRouter {
public:
  // Services that need a data bearer
  enum class Service { MQTT, HTTP };

  // Per-service routing result
  struct ServiceRoute {
    Service        service;
    ConnectionType bearer;      // Active bearer for this service
    String         localIP;
    bool           isOnFallback; // True if primary was unavailable
  };

private:
  ModemPPPoS* pppos;
  WiFiComm*   wifi;

  ConnectionType ppposState;   // PPPOS or NONE
  ConnectionType wifiState;    // WIFI  or NONE

  RouterState state;
  unsigned long lastReconnectAttempt;
  unsigned long reconnectInterval;

  // Low-level bearer bring-up
  bool bringUpPPPoS(uint32_t timeout_ms);
  bool bringUpWiFi (uint32_t timeout_ms);
  void tearDownPPPoS();

  // Check liveness of an active bearer
  bool checkPPPoS();
  bool checkWiFi();

  // Resolve preferred bearer for a service (from Config.h macros)
  ConnectionType preferredBearerFor(Service svc) const;
  ConnectionType fallbackBearerFor (Service svc) const;

  // Is a given bearer currently up?
  bool isBearerUp(ConnectionType b) const;

  const char* bearerName(ConnectionType b) const;

public:
  NetworkRouter();

  // ── Lifecycle ──────────────────────────────────────────────────────────────

  // Register bearer modules. Call before connect().
  // Either pointer may be nullptr if the corresponding flag is disabled.
  void init(ModemPPPoS* ppposModule, WiFiComm* wifiModule);

  // Bring up the required bearers based on service provider config.
  // Tries PPPoS and/or WiFi depending on MQTT_PRIMARY_BEARER /
  // HTTP_PRIMARY_BEARER settings. Returns true if at least one
  // bearer needed by a configured service came up.
  bool connect();

  // Tear down all active bearers.
  bool disconnect();

  // ── Service routing ────────────────────────────────────────────────────────

  // Returns the active bearer for a given service.
  // If the primary bearer is down, returns the fallback bearer.
  // Returns ConnectionType::NONE if neither is available.
  ServiceRoute getProviderFor(Service svc);

  // Convenience: is there any bearer for this service?
  bool isServiceAvailable(Service svc);

  // ── Global bearer status ───────────────────────────────────────────────────

  bool           isPPPoSUp()  const;
  bool           isWiFiUp()   const;
  bool           isAnyUp()    const;
  RouterState    getState()   const;
  String         getPPPoSIP() const;
  String         getWiFiIP()  const;

  // ── Background tasks ───────────────────────────────────────────────────────

  // Call every loop():
  //   • Feeds PPP stack bytes when PPPoS is active
  //   • Detects dropped bearers and triggers reconnect
  //   • Attempts reconnect after reconnectInterval
  void processBackground();

  void setReconnectInterval(unsigned long ms);

  // ── Diagnostics ────────────────────────────────────────────────────────────
  void printStatus() const;
};

// ─── Global instance ──────────────────────────────────────────────────────────
extern NetworkRouter networkRouter;

#endif // NETWORK_ROUTER_H
