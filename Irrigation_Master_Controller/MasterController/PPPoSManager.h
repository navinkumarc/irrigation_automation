// PPPoSManager.h - PPPoS (PPP over Serial) Manager for EC200U Modem
#ifndef PPPOS_MANAGER_H
#define PPPOS_MANAGER_H

#include <Arduino.h>
#include "esp_netif.h"
#include "esp_netif_ppp.h"
#include "esp_event.h"
#include "esp_log.h"

// PPP connection states
enum PPPState {
  PPP_IDLE,
  PPP_CONFIGURING,
  PPP_DIALING,
  PPP_CONNECTING,
  PPP_CONNECTED,
  PPP_DISCONNECTED,
  PPP_ERROR
};

// Forward declaration for friend function
static uint32_t ppp_output_callback(void *param, uint8_t *data, uint32_t len);

class PPPoSManager {
private:
  // Allow callback to access private members
  friend uint32_t ppp_output_callback(void *param, uint8_t *data, uint32_t len);
  HardwareSerial *modemSerial;
  esp_netif_t *ppp_netif;
  PPPState state;
  String localIP;
  String apn;
  bool initialized;

  // PPP event handlers (static for C callbacks)
  static void onPPPEvent(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
  static void onIPEvent(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

  // UART event task
  static void uartEventTask(void *arg);

  // Helper functions
  bool sendATCommand(const String &cmd, uint32_t timeout = 2000);
  String readModemResponse(uint32_t timeout);
  void clearSerialBuffer();

public:
  PPPoSManager();
  ~PPPoSManager();

  // Initialize PPP with modem serial and APN
  bool init(HardwareSerial *serial, const String &apn);

  // Configure PDP context and dial PPP
  bool connect(uint32_t timeout_ms = 30000);

  // Disconnect PPP
  bool disconnect();

  // Check if PPP is connected
  bool isConnected();

  // Get current state
  PPPState getState();

  // Get local IP address
  String getLocalIP();

  // Process background tasks (call in loop)
  void loop();
};

#endif
