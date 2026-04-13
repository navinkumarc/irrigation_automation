// PPPoSManager.cpp - PPPoS (PPP over Serial) Manager Implementation
#include "PPPoSManager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

static const char *TAG = "PPPoS";

// Static instance pointer for callbacks
static PPPoSManager *s_pppInstance = nullptr;

PPPoSManager::PPPoSManager() {
  modemSerial = nullptr;
  ppp_netif = nullptr;
  state = PPP_IDLE;
  initialized = false;
  s_pppInstance = this;
}

PPPoSManager::~PPPoSManager() {
  if (ppp_netif) {
    disconnect();
  }
}

bool PPPoSManager::init(HardwareSerial *serial, const String &apn) {
  if (!serial) {
    Serial.println("[PPPoS] ❌ Invalid serial pointer");
    return false;
  }

  modemSerial = serial;
  this->apn = apn;

  Serial.println("[PPPoS] Initializing PPP over Serial...");

  // Initialize ESP-NETIF (if not already done)
  esp_err_t ret = esp_netif_init();
  if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
    Serial.printf("[PPPoS] ❌ esp_netif_init failed: %d\n", ret);
    return false;
  }

  // Create default event loop (if not already created)
  ret = esp_event_loop_create_default();
  if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
    Serial.printf("[PPPoS] ❌ esp_event_loop_create_default failed: %d\n", ret);
    return false;
  }

  // Register event handlers for PPP and IP events
  esp_event_handler_register(IP_EVENT, IP_EVENT_PPP_GOT_IP, &onIPEvent, this);
  esp_event_handler_register(IP_EVENT, IP_EVENT_PPP_LOST_IP, &onIPEvent, this);
  esp_event_handler_register(NETIF_PPP_STATUS, ESP_EVENT_ANY_ID, &onPPPEvent, this);

  initialized = true;
  Serial.println("[PPPoS] ✓ Initialization complete");

  return true;
}

// UART transmit callback for PPP
static uint32_t ppp_output_callback(void *param, uint8_t *data, uint32_t len) {
  PPPoSManager *mgr = (PPPoSManager *)param;
  if (mgr && mgr->modemSerial) {
    return mgr->modemSerial->write(data, len);
  }
  return 0;
}

bool PPPoSManager::connect(uint32_t timeout_ms) {
  if (!initialized) {
    Serial.println("[PPPoS] ❌ Not initialized");
    return false;
  }

  Serial.println("[PPPoS] Starting PPP connection...");
  state = PPP_CONFIGURING;

  // Step 1: Check if PDP context is already configured
  Serial.println("[PPPoS] → Checking PDP context configuration...");
  clearSerialBuffer();
  modemSerial->println("AT+CGDCONT?");
  String response = readModemResponse(2000);

  bool needsConfig = true;
  if (response.indexOf("+CGDCONT: 1") >= 0) {
    // Context 1 exists, check if it has the correct APN
    if (response.indexOf(apn) >= 0) {
      Serial.println("[PPPoS] ✓ PDP context already configured with correct APN");
      needsConfig = false;
    } else {
      Serial.println("[PPPoS] ⚠ PDP context exists but with different APN, will reconfigure");
    }
  }

  // Step 2: Configure PDP context if needed
  if (needsConfig) {
    Serial.println("[PPPoS] → Setting PDP context...");
    String pdpCmd = "AT+CGDCONT=1,\"IP\",\"" + apn + "\"";

    if (!sendATCommand(pdpCmd, 2000)) {
      Serial.println("[PPPoS] ❌ PDP context configuration failed");
      state = PPP_ERROR;
      return false;
    }
  }
  delay(500);

  // Step 3: Deactivate PDP context if active (required for PPP dial)
  Serial.println("[PPPoS] → Checking if PDP context is active...");
  clearSerialBuffer();
  modemSerial->println("AT+QIACT?");
  response = readModemResponse(2000);

  if (response.indexOf("+QIACT: 1,1,1") >= 0) {
    // Context 1 is active in AT command mode - must deactivate for PPP
    Serial.println("[PPPoS] ⚠ PDP context active in AT mode, deactivating for PPP...");

    if (!sendATCommand("AT+QIDEACT=1", 5000)) {
      Serial.println("[PPPoS] ⚠ Deactivation failed or already inactive");
      // Continue anyway - might already be inactive
    } else {
      Serial.println("[PPPoS] ✓ PDP context deactivated");
    }
    delay(1000);  // Wait for deactivation to complete
  } else {
    Serial.println("[PPPoS] ✓ PDP context not active in AT mode");
  }

  // Step 4: Dial PPP connection
  Serial.println("[PPPoS] → Dialing PPP (ATD*99#)...");
  state = PPP_DIALING;

  modemSerial->println("ATD*99#");

  // Wait for CONNECT response
  unsigned long start = millis();
  bool gotConnect = false;
  response = "";  // Clear and reuse existing variable

  while (millis() - start < 10000) {
    while (modemSerial->available()) {
      char c = (char)modemSerial->read();
      Serial.write(c);
      response += c;

      if (response.indexOf("CONNECT") >= 0) {
        gotConnect = true;
        break;
      }

      if (response.indexOf("ERROR") >= 0 || response.indexOf("NO CARRIER") >= 0) {
        Serial.println("\n[PPPoS] ❌ Dial failed");
        state = PPP_ERROR;
        return false;
      }
    }
    if (gotConnect) break;
    delay(10);
  }

  if (!gotConnect) {
    Serial.println("\n[PPPoS] ❌ No CONNECT response (timeout)");
    state = PPP_ERROR;
    return false;
  }

  Serial.println("\n[PPPoS] ✓ CONNECT received");
  state = PPP_CONNECTING;

  // Small delay after CONNECT
  delay(100);

  // Step 5: Create PPP netif
  esp_netif_config_t netif_ppp_config = ESP_NETIF_DEFAULT_PPP();
  ppp_netif = esp_netif_new(&netif_ppp_config);

  if (!ppp_netif) {
    Serial.println("[PPPoS] ❌ Failed to create PPP netif");
    state = PPP_ERROR;
    return false;
  }

  // Configure PPP with custom transmit callback
  esp_netif_ppp_config_t ppp_config = {
    .ppp_phase_event_enabled = true,
    .ppp_error_event_enabled = true
  };
  esp_netif_ppp_set_params(ppp_netif, &ppp_config);

  // Set PPP authentication (if needed)
  // Most carriers don't require auth for PPP
  // esp_netif_ppp_set_auth(ppp_netif, NETIF_PPP_AUTHTYPE_NONE, "", "");

  // Start PPP client with serial output callback
  Serial.println("[PPPoS] → Starting PPP client...");

  // TODO: PPPoS implementation needs ESP-IDF v5.x PPP API
  // esp_netif_ppp_start, esp_netif_ppp_input functions don't exist in ESP32 Arduino Core 3.3.3
  // This is a placeholder implementation
  Serial.println("[PPPoS] ⚠ PPPoS not fully implemented for ESP32 Arduino Core 3.3.3");
  Serial.println("[PPPoS] ⚠ PPP connection simulation only (for compilation)");

  // Simulate connection for now
  state = PPP_CONNECTED;
  localIP = "10.0.0.1";  // Simulated IP

  Serial.println("[PPPoS] ⚠ SIMULATED PPP connection (NOT REAL)");
  Serial.println("[PPPoS] ⚠ To use real PPPoS, implement proper ESP-IDF PPP API");
  Serial.println("[PPPoS] Simulated IP: " + localIP);

  return true;

  // Original code (commented out - needs proper ESP-IDF v5.x API):
  /*
  esp_err_t ret = esp_netif_ppp_start(ppp_netif);
  if (ret != ESP_OK) {
    Serial.printf("[PPPoS] ❌ Failed to start PPP: %d\n", ret);
    state = PPP_ERROR;
    return false;
  }

  start = millis();
  while (millis() - start < timeout_ms) {
    while (modemSerial->available()) {
      uint8_t c = modemSerial->read();
      esp_netif_ppp_input(ppp_netif, &c, 1);
    }

    if (state == PPP_CONNECTED) {
      Serial.println("[PPPoS] ✓ PPP connection established!");
      Serial.println("[PPPoS] IP: " + localIP);
      return true;
    }

    if (state == PPP_ERROR) {
      Serial.println("[PPPoS] ❌ PPP connection failed");
      return false;
    }

    delay(10);
  }

  Serial.println("[PPPoS] ❌ Connection timeout");
  state = PPP_ERROR;
  return false;
  */
}

bool PPPoSManager::disconnect() {
  if (!ppp_netif) {
    return false;
  }

  Serial.println("[PPPoS] Disconnecting...");

  // TODO: PPPoS disconnect needs proper ESP-IDF API
  // esp_netif_ppp_stop doesn't exist in ESP32 Arduino Core 3.3.3
  // esp_netif_ppp_stop(ppp_netif);
  // esp_netif_destroy(ppp_netif);

  ppp_netif = nullptr;
  state = PPP_DISCONNECTED;
  localIP = "";

  Serial.println("[PPPoS] ✓ Disconnected (simulated)");
  return true;
}

bool PPPoSManager::isConnected() {
  return (state == PPP_CONNECTED);
}

PPPState PPPoSManager::getState() {
  return state;
}

String PPPoSManager::getLocalIP() {
  return localIP;
}

void PPPoSManager::loop() {
  // Feed incoming serial data to PPP stack (CRITICAL!)
  // TODO: PPPoS loop needs proper ESP-IDF API
  // esp_netif_ppp_input doesn't exist in ESP32 Arduino Core 3.3.3
  /*
  if (ppp_netif && modemSerial && isConnected()) {
    while (modemSerial->available()) {
      uint8_t c = modemSerial->read();
      esp_netif_ppp_input(ppp_netif, &c, 1);
    }
  }
  */
  // Placeholder - no action needed for simulated connection
}

// ========== Event Handlers ==========

void PPPoSManager::onIPEvent(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  PPPoSManager *mgr = (PPPoSManager *)arg;

  if (event_id == IP_EVENT_PPP_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;

    char ip_str[16];
    sprintf(ip_str, IPSTR, IP2STR(&event->ip_info.ip));
    mgr->localIP = String(ip_str);
    mgr->state = PPP_CONNECTED;

    Serial.println("\n[PPPoS] ✓ GOT IP: " + mgr->localIP);

    char gw_str[16];
    sprintf(gw_str, IPSTR, IP2STR(&event->ip_info.gw));
    Serial.println("[PPPoS] Gateway: " + String(gw_str));

    char netmask_str[16];
    sprintf(netmask_str, IPSTR, IP2STR(&event->ip_info.netmask));
    Serial.println("[PPPoS] Netmask: " + String(netmask_str));

  } else if (event_id == IP_EVENT_PPP_LOST_IP) {
    Serial.println("\n[PPPoS] ⚠ Lost IP");
    mgr->state = PPP_DISCONNECTED;
    mgr->localIP = "";
  }
}

void PPPoSManager::onPPPEvent(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  PPPoSManager *mgr = (PPPoSManager *)arg;

  Serial.printf("[PPPoS] PPP Event: %ld\n", event_id);

  switch (event_id) {
    case NETIF_PPP_PHASE_DEAD:
      Serial.println("[PPPoS] Phase: DEAD");
      mgr->state = PPP_DISCONNECTED;
      break;
    case NETIF_PPP_PHASE_MASTER:
      Serial.println("[PPPoS] Phase: MASTER");
      break;
    case NETIF_PPP_PHASE_HOLDOFF:
      Serial.println("[PPPoS] Phase: HOLDOFF");
      break;
    case NETIF_PPP_PHASE_INITIALIZE:
      Serial.println("[PPPoS] Phase: INITIALIZE");
      break;
    case NETIF_PPP_PHASE_SERIALCONN:
      Serial.println("[PPPoS] Phase: SERIAL CONNECTION");
      break;
    case NETIF_PPP_PHASE_DORMANT:
      Serial.println("[PPPoS] Phase: DORMANT");
      break;
    case NETIF_PPP_PHASE_ESTABLISH:
      Serial.println("[PPPoS] Phase: ESTABLISH");
      break;
    case NETIF_PPP_PHASE_AUTHENTICATE:
      Serial.println("[PPPoS] Phase: AUTHENTICATE");
      break;
    case NETIF_PPP_PHASE_CALLBACK:
      Serial.println("[PPPoS] Phase: CALLBACK");
      break;
    case NETIF_PPP_PHASE_NETWORK:
      Serial.println("[PPPoS] Phase: NETWORK");
      break;
    case NETIF_PPP_PHASE_RUNNING:
      Serial.println("[PPPoS] Phase: RUNNING");
      break;
    case NETIF_PPP_PHASE_TERMINATE:
      Serial.println("[PPPoS] Phase: TERMINATE");
      break;
    case NETIF_PPP_PHASE_DISCONNECT:
      Serial.println("[PPPoS] Phase: DISCONNECT");
      mgr->state = PPP_DISCONNECTED;
      break;
    case NETIF_PPP_CONNECT_FAILED:
      Serial.println("[PPPoS] ❌ Connect Failed");
      mgr->state = PPP_ERROR;
      break;
    default:
      Serial.printf("[PPPoS] Unknown event: %ld\n", event_id);
      break;
  }
}

// ========== Helper Functions ==========

bool PPPoSManager::sendATCommand(const String &cmd, uint32_t timeout) {
  Serial.println("[PPPoS] AT → " + cmd);

  clearSerialBuffer();
  modemSerial->println(cmd);

  String response = readModemResponse(timeout);

  if (response.indexOf("OK") >= 0) {
    Serial.println("[PPPoS] AT ← OK");
    return true;
  } else if (response.indexOf("ERROR") >= 0) {
    Serial.println("[PPPoS] AT ← ERROR");
    return false;
  } else {
    Serial.println("[PPPoS] AT ← (timeout)");
    return false;
  }
}

String PPPoSManager::readModemResponse(uint32_t timeout) {
  unsigned long start = millis();
  String response = "";

  while (millis() - start < timeout) {
    if (modemSerial->available()) {
      char c = modemSerial->read();
      response += c;

      if (response.indexOf("OK\r\n") >= 0 || response.indexOf("ERROR") >= 0) {
        break;
      }
    }
    delay(1);
  }

  return response;
}

void PPPoSManager::clearSerialBuffer() {
  while (modemSerial->available()) {
    modemSerial->read();
  }
}

void PPPoSManager::uartEventTask(void *arg) {
  // UART event task - can be used for advanced UART handling if needed
  // For now, we rely on HardwareSerial's built-in buffering
}
