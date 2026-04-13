// ModemPPPoS.h - PPP over Serial data connectivity for Quectel EC200U
// (Renamed from PPPoSManager to ModemPPPoS to reflect its role as a
//  dependent module of ModemBase, parallel to ModemSMS.)
//
// Dependency: ModemBase must be initialized before calling connect().
// Mode:       Requests MODEM_MODE_DATA from ModemBase on connect(),
//             releases it on disconnect().
// Constraint: Cannot be active simultaneously with ModemSMS.
//             Only one of ENABLE_PPPOS / ENABLE_SMS should be 1 at a time.
//
// When connected, the ESP32 TCP/IP stack is routed over the cellular PPP link,
// allowing standard WiFiClient / HTTPClient / MQTTClient code to work
// transparently over cellular data.

#ifndef MODEM_PPPOS_H
#define MODEM_PPPOS_H

#if ENABLE_PPPOS

#include <Arduino.h>
#include "esp_netif.h"
#include "esp_netif_ppp.h"
#include "esp_event.h"
#include "Config.h"
#include "ModemBase.h"

// ─── PPP connection states ────────────────────────────────────────────────────

enum PPPState {
  PPP_IDLE,
  PPP_CONFIGURING,
  PPP_DIALING,
  PPP_CONNECTING,
  PPP_CONNECTED,
  PPP_DISCONNECTED,
  PPP_ERROR
};

// ─── ModemPPPoS ───────────────────────────────────────────────────────────────

class ModemPPPoS {
private:
  esp_netif_t* pppNetif;
  PPPState     state;
  String       localIP;
  bool         initialized;

  // Static ESP-IDF event handlers
  static void onIPEvent (void* arg, esp_event_base_t base, int32_t id, void* data);
  static void onPPPEvent(void* arg, esp_event_base_t base, int32_t id, void* data);

  // Dial PPP and wait for CONNECT
  bool dialPPP(uint32_t timeout_ms);

  // Escape from PPP mode back to AT command mode (+++ sequence)
  bool escapeToATMode();

public:
  ModemPPPoS();
  ~ModemPPPoS();

  // ── Lifecycle ──────────────────────────────────────────────────────────────

  // Initialize ESP-NETIF infrastructure (call once in setup, before connect).
  bool init();

  // Request MODEM_MODE_DATA, deactivate PDP context if needed,
  // dial ATD*99#, start PPP stack.
  // Returns true when an IP address has been assigned.
  bool connect(uint32_t timeout_ms = PPPOS_CONNECT_TIMEOUT_MS);

  // Stop PPP stack, escape to AT mode, release MODEM_MODE_DATA.
  bool disconnect();

  // ── Status ─────────────────────────────────────────────────────────────────

  bool      isConnected() const;
  PPPState  getState()    const;
  String    getLocalIP()  const;

  // ── Loop ───────────────────────────────────────────────────────────────────

  // MUST be called every loop() iteration while connected.
  // Feeds incoming serial bytes into the PPP stack.
  void loop();

  // Diagnostics
  void printStatus() const;
};

// ─── Global instance ──────────────────────────────────────────────────────────
extern ModemPPPoS modemPPPoS;

#endif // ENABLE_PPPOS
#endif // MODEM_PPPOS_H
