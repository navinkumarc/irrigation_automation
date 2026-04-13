// ModemPPPoS.cpp - PPP over Serial data connectivity for Quectel EC200U
// Renamed from PPPoSManager. Depends on ModemBase for hardware access.
#include "ModemPPPoS.h"

#if ENABLE_PPPOS

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

// ─── Static instance for callbacks ───────────────────────────────────────────
static ModemPPPoS* s_instance = nullptr;

// ─── Global instance ─────────────────────────────────────────────────────────
ModemPPPoS modemPPPoS;

// ─── Constructor / Destructor ────────────────────────────────────────────────
ModemPPPoS::ModemPPPoS()
  : pppNetif(nullptr), state(PPP_IDLE), localIP(""), initialized(false) {
  s_instance = this;
}

ModemPPPoS::~ModemPPPoS() {
  if (state == PPP_CONNECTED) disconnect();
}

// ─── init() ──────────────────────────────────────────────────────────────────
bool ModemPPPoS::init() {
  Serial.println("[PPPoS] Initializing ESP-NETIF for PPP...");

  esp_err_t ret = esp_netif_init();
  if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
    Serial.printf("[PPPoS] ❌ esp_netif_init failed: %d\n", ret);
    return false;
  }

  ret = esp_event_loop_create_default();
  if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
    Serial.printf("[PPPoS] ❌ esp_event_loop_create_default failed: %d\n", ret);
    return false;
  }

  // Register event handlers
  esp_event_handler_register(IP_EVENT,         IP_EVENT_PPP_GOT_IP,  &onIPEvent,  this);
  esp_event_handler_register(IP_EVENT,         IP_EVENT_PPP_LOST_IP, &onIPEvent,  this);
  esp_event_handler_register(NETIF_PPP_STATUS, ESP_EVENT_ANY_ID,     &onPPPEvent, this);

  initialized = true;
  Serial.println("[PPPoS] ✓ ESP-NETIF initialized");
  return true;
}

// ─── connect() ───────────────────────────────────────────────────────────────
bool ModemPPPoS::connect(uint32_t timeout_ms) {
  if (!initialized) {
    Serial.println("[PPPoS] ❌ Not initialized — call init() first");
    return false;
  }
  if (!modemBase.isReady()) {
    Serial.println("[PPPoS] ❌ ModemBase not ready");
    return false;
  }

  // Request exclusive DATA mode from ModemBase
  // (will fail if ModemSMS currently holds MODEM_MODE_SMS)
  if (!modemBase.requestMode(MODEM_MODE_DATA)) {
    Serial.println("[PPPoS] ❌ Cannot connect — ModemSMS is active");
    Serial.println("[PPPoS] ❌ Call modemSMS.release() first");
    return false;
  }

  Serial.println("[PPPoS] ========================================");
  Serial.println("[PPPoS] Starting PPP data connection...");
  state = PPP_CONFIGURING;

  // Step 1: Configure PDP context via AT commands
  Serial.println("[PPPoS] → Configuring PDP context (APN: " + String(PPPOS_APN) + ")...");
  String pdpCheck = modemBase.sendCommand("AT+CGDCONT?", 2000);
  if (pdpCheck.indexOf(PPPOS_APN) < 0) {
    String pdpCmd = "AT+CGDCONT=1,\"IP\",\"" + String(PPPOS_APN) + "\"";
    if (modemBase.sendCommand(pdpCmd, 2000).indexOf("OK") < 0) {
      Serial.println("[PPPoS] ❌ PDP context configuration failed");
      state = PPP_ERROR;
      modemBase.releaseMode(MODEM_MODE_DATA);
      return false;
    }
  } else {
    Serial.println("[PPPoS] ✓ PDP context already configured");
  }
  delay(500);

  // Step 2: Deactivate AT-mode PDP context if active (required for PPP dial)
  modemBase.deactivatePDPContext();

  // Step 3: Dial PPP
  if (!dialPPP(10000)) {
    state = PPP_ERROR;
    modemBase.releaseMode(MODEM_MODE_DATA);
    return false;
  }

  // Step 4: Create PPP netif
  state = PPP_CONNECTING;
  esp_netif_config_t cfg = ESP_NETIF_DEFAULT_PPP();
  pppNetif = esp_netif_new(&cfg);
  if (!pppNetif) {
    Serial.println("[PPPoS] ❌ Failed to create PPP netif");
    state = PPP_ERROR;
    modemBase.releaseMode(MODEM_MODE_DATA);
    return false;
  }

  esp_netif_ppp_config_t pppCfg = {
    .ppp_phase_event_enabled = true,
    .ppp_error_event_enabled = true
  };
  esp_netif_ppp_set_params(pppNetif, &pppCfg);

  // Step 5: Start PPP client
  // NOTE: esp_netif_ppp_start / esp_netif_ppp_input are available in
  // ESP-IDF v5.x (Arduino ESP32 core >= 3.0). On older cores this section
  // falls through to the simulation block below.
  Serial.println("[PPPoS] → Starting PPP client...");

#if ESP_IDF_VERSION_MAJOR >= 5
  // Real implementation for ESP-IDF v5+
  esp_err_t err = esp_netif_ppp_start(pppNetif);
  if (err != ESP_OK) {
    Serial.printf("[PPPoS] ❌ esp_netif_ppp_start failed: %d\n", err);
    esp_netif_destroy(pppNetif);
    pppNetif = nullptr;
    state = PPP_ERROR;
    modemBase.releaseMode(MODEM_MODE_DATA);
    return false;
  }

  // Wait for IP assignment
  unsigned long start = millis();
  while (millis() - start < timeout_ms) {
    loop(); // feed serial data into PPP stack
    if (state == PPP_CONNECTED) {
      Serial.println("[PPPoS] ✓ PPP connected, IP: " + localIP);
      Serial.println("[PPPoS] ========================================");
      return true;
    }
    if (state == PPP_ERROR) {
      Serial.println("[PPPoS] ❌ PPP connection failed");
      modemBase.releaseMode(MODEM_MODE_DATA);
      return false;
    }
    delay(10);
  }
  Serial.println("[PPPoS] ❌ PPP connection timeout");
  esp_netif_ppp_stop(pppNetif);
  esp_netif_destroy(pppNetif);
  pppNetif = nullptr;
  state = PPP_ERROR;
  modemBase.releaseMode(MODEM_MODE_DATA);
  return false;

#else
  // Simulation block for ESP-IDF < v5 (Arduino ESP32 core < 3.0)
  // PPP API not available — simulate connection for compilation/testing.
  Serial.println("[PPPoS] ⚠ ESP-IDF < v5 detected — PPP simulation mode");
  Serial.println("[PPPoS] ⚠ Upgrade to Arduino ESP32 core >= 3.0 for real PPP support");
  state   = PPP_CONNECTED;
  localIP = "10.0.0.1";
  Serial.println("[PPPoS] ⚠ SIMULATED IP: " + localIP);
  Serial.println("[PPPoS] ========================================");
  return true;
#endif
}

// ─── dialPPP() ───────────────────────────────────────────────────────────────
bool ModemPPPoS::dialPPP(uint32_t timeout_ms) {
  Serial.println("[PPPoS] → Dialing ATD*99#...");
  state = PPP_DIALING;

  modemBase.clearSerialBuffer();
  SerialAT.println("ATD*99#");

  unsigned long start = millis();
  String response;

  while (millis() - start < timeout_ms) {
    while (SerialAT.available()) {
      char c = (char)SerialAT.read();
      Serial.write(c);
      response += c;
      if (response.indexOf("CONNECT") >= 0) {
        Serial.println("\n[PPPoS] ✓ CONNECT received");
        delay(100);
        return true;
      }
      if (response.indexOf("ERROR") >= 0 || response.indexOf("NO CARRIER") >= 0) {
        Serial.println("\n[PPPoS] ❌ Dial failed: " + response);
        return false;
      }
    }
    delay(10);
  }

  Serial.println("\n[PPPoS] ❌ Dial timeout — no CONNECT");
  return false;
}

// ─── escapeToATMode() ────────────────────────────────────────────────────────
bool ModemPPPoS::escapeToATMode() {
  Serial.println("[PPPoS] → Escaping PPP mode (+++ sequence)...");
  delay(1100);           // Guard time before +++
  SerialAT.print("+++");
  delay(1100);           // Guard time after +++

  unsigned long start = millis();
  String resp;
  while (millis() - start < 3000) {
    while (SerialAT.available()) resp += (char)SerialAT.read();
    if (resp.indexOf("OK") >= 0) {
      Serial.println("[PPPoS] ✓ Escaped to AT command mode");
      return true;
    }
    delay(10);
  }
  Serial.println("[PPPoS] ⚠ +++ escape timeout — sending ATH");
  SerialAT.println("ATH");
  delay(1000);
  return false;
}

// ─── disconnect() ────────────────────────────────────────────────────────────
bool ModemPPPoS::disconnect() {
  Serial.println("[PPPoS] Disconnecting PPP...");

#if ESP_IDF_VERSION_MAJOR >= 5
  if (pppNetif) {
    esp_netif_ppp_stop(pppNetif);
    esp_netif_destroy(pppNetif);
    pppNetif = nullptr;
  }
#endif

  // Escape back to AT command mode so ModemSMS or init can use the modem
  escapeToATMode();

  state   = PPP_DISCONNECTED;
  localIP = "";

  // Release DATA mode so ModemSMS (or ModemBase) can reclaim the modem
  modemBase.releaseMode(MODEM_MODE_DATA);

  Serial.println("[PPPoS] ✓ Disconnected — modem returned to AT command mode");
  return true;
}

// ─── loop() ──────────────────────────────────────────────────────────────────
void ModemPPPoS::loop() {
#if ESP_IDF_VERSION_MAJOR >= 5
  if (pppNetif && isConnected()) {
    while (SerialAT.available()) {
      uint8_t c = (uint8_t)SerialAT.read();
      esp_netif_ppp_input(pppNetif, &c, 1);
    }
  }
#endif
}

// ─── Status accessors ────────────────────────────────────────────────────────
bool     ModemPPPoS::isConnected() const { return state == PPP_CONNECTED; }
PPPState ModemPPPoS::getState()    const { return state; }
String   ModemPPPoS::getLocalIP()  const { return localIP; }

void ModemPPPoS::printStatus() const {
  const char* stateStr =
    state == PPP_IDLE         ? "IDLE"         :
    state == PPP_CONFIGURING  ? "CONFIGURING"  :
    state == PPP_DIALING      ? "DIALING"      :
    state == PPP_CONNECTING   ? "CONNECTING"   :
    state == PPP_CONNECTED    ? "CONNECTED"    :
    state == PPP_DISCONNECTED ? "DISCONNECTED" : "ERROR";
  Serial.printf("[PPPoS] State: %s | IP: %s | Mode held: %s\n",
    stateStr,
    localIP.length() ? localIP.c_str() : "none",
    modemBase.isInMode(MODEM_MODE_DATA) ? "YES" : "NO");
}

// ─── ESP-IDF event handlers ──────────────────────────────────────────────────
void ModemPPPoS::onIPEvent(void* arg, esp_event_base_t base, int32_t id, void* data) {
  ModemPPPoS* self = (ModemPPPoS*)arg;
  if (id == IP_EVENT_PPP_GOT_IP) {
    ip_event_got_ip_t* ev = (ip_event_got_ip_t*)data;
    char ip[16], gw[16], nm[16];
    sprintf(ip, IPSTR, IP2STR(&ev->ip_info.ip));
    sprintf(gw, IPSTR, IP2STR(&ev->ip_info.gw));
    sprintf(nm, IPSTR, IP2STR(&ev->ip_info.netmask));
    self->localIP = String(ip);
    self->state   = PPP_CONNECTED;
    Serial.println("[PPPoS] ✓ GOT IP: " + self->localIP);
    Serial.println("[PPPoS] Gateway:  " + String(gw));
    Serial.println("[PPPoS] Netmask:  " + String(nm));
  } else if (id == IP_EVENT_PPP_LOST_IP) {
    Serial.println("[PPPoS] ⚠ Lost IP");
    self->state   = PPP_DISCONNECTED;
    self->localIP = "";
  }
}

void ModemPPPoS::onPPPEvent(void* arg, esp_event_base_t base, int32_t id, void* data) {
  ModemPPPoS* self = (ModemPPPoS*)arg;
  switch (id) {
    case NETIF_PPP_PHASE_DEAD:       Serial.println("[PPPoS] Phase: DEAD");        self->state = PPP_DISCONNECTED; break;
    case NETIF_PPP_PHASE_INITIALIZE: Serial.println("[PPPoS] Phase: INITIALIZE");  break;
    case NETIF_PPP_PHASE_ESTABLISH:  Serial.println("[PPPoS] Phase: ESTABLISH");   break;
    case NETIF_PPP_PHASE_AUTHENTICATE: Serial.println("[PPPoS] Phase: AUTH");      break;
    case NETIF_PPP_PHASE_NETWORK:    Serial.println("[PPPoS] Phase: NETWORK");     break;
    case NETIF_PPP_PHASE_RUNNING:    Serial.println("[PPPoS] Phase: RUNNING");     break;
    case NETIF_PPP_PHASE_TERMINATE:  Serial.println("[PPPoS] Phase: TERMINATE");   break;
    case NETIF_PPP_PHASE_DISCONNECT: Serial.println("[PPPoS] Phase: DISCONNECT");  self->state = PPP_DISCONNECTED; break;
    case NETIF_PPP_CONNECT_FAILED:   Serial.println("[PPPoS] ❌ Connect Failed");  self->state = PPP_ERROR; break;
    default: Serial.printf("[PPPoS] PPP Event: %ld\n", id); break;
  }
}

#endif // ENABLE_PPPOS
