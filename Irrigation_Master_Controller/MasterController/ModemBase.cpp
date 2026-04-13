// ModemBase.cpp - Core modem hardware manager for Quectel EC200U
#include "ModemBase.h"

// ─── Shared UART ─────────────────────────────────────────────────────────────
// Serial1 on the ESP32 — used by ModemBase, ModemSMS, and ModemPPPoS.
HardwareSerial SerialAT(1);

// ─── Static members ───────────────────────────────────────────────────────────
bool      ModemBase::modemReady  = false;
ModemMode ModemBase::activeMode  = MODEM_MODE_NONE;

// ─── Global instance ──────────────────────────────────────────────────────────
ModemBase modemBase;

// ─── Constructor ──────────────────────────────────────────────────────────────
ModemBase::ModemBase() {}

// ─── init() ───────────────────────────────────────────────────────────────────
bool ModemBase::init() {
  Serial.println("[Modem] ========== Initializing EC200U ==========");

  // GPIO setup — do not toggle power key; modem is assumed already on.
  pinMode(MODEM_PWRKEY, OUTPUT);
  pinMode(MODEM_RESET,  OUTPUT);
  digitalWrite(MODEM_RESET,  HIGH);   // de-assert reset
  digitalWrite(MODEM_PWRKEY, LOW);    // idle

  Serial.println("[Modem] Using existing modem session (no hardware reset)");
  delay(1000);

  // Start UART
  SerialAT.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);
  delay(1000);

  // Verify AT communication
  Serial.println("[Modem] Testing AT communication...");
  bool atOk = false;
  for (int i = 0; i < 10 && !atOk; i++) {
    if (sendCommand("AT", 1000).indexOf("OK") >= 0) atOk = true;
    else                                            delay(1000);
  }
  if (!atOk) { Serial.println("[Modem] ❌ AT communication failed"); return false; }
  Serial.println("[Modem] ✓ AT communication OK");

  // Disable echo
  sendCommand("ATE0", 1000);

  // Print module info
  Serial.println("[Modem] Model: " + sendCommand("ATI", 1000));

  // SIM check
  Serial.println("[Modem] Checking SIM...");
  bool simReady = false;
  for (int retry = 0; retry < 5 && !simReady; retry++) {
    String s = sendCommand("AT+CPIN?", 2000);
    if (s.indexOf("READY") >= 0) {
      simReady = true;
      Serial.println("[Modem] ✓ SIM ready");
    } else {
      delay((s.indexOf("+CME ERROR: 14") >= 0) ? 1000 : 500);
    }
  }
  if (!simReady) Serial.println("[Modem] ⚠ SIM check failed — continuing anyway");

  // LTE only mode
  sendCommand("AT+QCFG=\"nwscanmode\",3,1", 2000);

  // Configure APN
  if (!configureAPN()) {
    Serial.println("[Modem] ⚠ APN configuration warning — continuing");
  }

  // Network registration
  if (!isNetworkRegistered()) {
    Serial.println("[Modem] ⚠ Network registration timeout — SMS may still work");
  }

  // Diagnostics
  Serial.println("[Modem] Signal: "   + getSignalQuality());
  Serial.println("[Modem] Operator: " + getOperator());

  // Try to activate PDP context (needed for data; SMS works without it)
  activatePDPContext();

  // Default mode: SMS (AT command mode)
  activeMode  = MODEM_MODE_NONE;
  modemReady  = true;
  Serial.println("[Modem] ✓ Modem ready — mode: NONE (unclaimed)");
  return true;
}

// ─── isReady / setReady ───────────────────────────────────────────────────────
bool ModemBase::isReady() const   { return modemReady; }
void ModemBase::setReady(bool r)  { modemReady = r; }

// ─── Mode management ──────────────────────────────────────────────────────────
bool ModemBase::requestMode(ModemMode mode) {
  if (!modemReady) {
    Serial.println("[Modem] ❌ requestMode failed — modem not ready");
    return false;
  }
  if (activeMode == mode) {
    // Same module re-requesting its own mode — allowed
    return true;
  }
  if (activeMode != MODEM_MODE_NONE) {
    // Another module holds the modem
    const char* holder = (activeMode == MODEM_MODE_SMS)  ? "ModemSMS"   :
                         (activeMode == MODEM_MODE_DATA) ? "ModemPPPoS" : "unknown";
    Serial.printf("[Modem] ❌ Mode conflict: requested %s but modem is held by %s\n",
                  (mode == MODEM_MODE_SMS) ? "SMS" : "DATA", holder);
    return false;
  }

  activeMode = mode;
  Serial.printf("[Modem] ✓ Mode granted: %s\n",
                (mode == MODEM_MODE_SMS) ? "SMS (AT command mode)" : "DATA (PPP mode)");
  return true;
}

void ModemBase::releaseMode(ModemMode mode) {
  if (activeMode == mode) {
    activeMode = MODEM_MODE_NONE;
    Serial.printf("[Modem] ✓ Mode released: %s — modem now idle\n",
                  (mode == MODEM_MODE_SMS) ? "SMS" : "DATA");
  } else {
    Serial.println("[Modem] ⚠ releaseMode called by non-owner — ignored");
  }
}

ModemMode ModemBase::getActiveMode() const     { return activeMode; }
bool      ModemBase::isInMode(ModemMode m) const { return activeMode == m; }

// ─── AT interface ─────────────────────────────────────────────────────────────
String ModemBase::sendCommand(const String &cmd, uint32_t timeout_ms) {
  // Guard: do not send AT commands while in PPP data mode
  if (activeMode == MODEM_MODE_DATA) {
    Serial.println("[Modem] ⚠ sendCommand blocked — modem is in PPP data mode");
    return "";
  }

  Serial.println("[Modem] TX: " + cmd);
  clearSerialBuffer();
  SerialAT.println(cmd);

  unsigned long start = millis();
  String response;

  while (millis() - start < timeout_ms) {
    if (SerialAT.available()) {
      char c = SerialAT.read();
      response += c;
      if (response.indexOf("OK\r\n") >= 0) break;
      if (response.indexOf("ERROR") >= 0) {
        delay(200);
        while (SerialAT.available()) response += (char)SerialAT.read();
        break;
      }
    }
    delay(1);
  }

  Serial.println(response.length() ? "[Modem] RX: " + response : "[Modem] RX: (timeout)");
  return response;
}

void ModemBase::clearSerialBuffer() {
  while (SerialAT.available()) SerialAT.read();
}

// ─── Network helpers ──────────────────────────────────────────────────────────
bool ModemBase::isNetworkRegistered() {
  Serial.println("[Modem] Checking network registration...");
  for (int i = 0; i < 10; i++) {
    String creg  = sendCommand("AT+CREG?",  1000);
    String cgreg = sendCommand("AT+CGREG?", 1000);
    if ((creg.indexOf(",1")  >= 0 || creg.indexOf(",5")  >= 0) ||
        (cgreg.indexOf(",1") >= 0 || cgreg.indexOf(",5") >= 0)) {
      Serial.println("[Modem] ✓ Network registered");
      return true;
    }
    delay(1000);
  }
  return false;
}

bool ModemBase::configureAPN(const String &apn) {
  Serial.println("[Modem] Configuring APN: " + apn);
  String resp = sendCommand("AT+QICSGP=1,1,\"" + apn + "\",\"\",\"\",1", 2000);
  if (resp.indexOf("OK") >= 0) {
    Serial.println("[Modem] ✓ APN configured");
    return true;
  }
  Serial.println("[Modem] ⚠ APN configuration response: " + resp);
  return false;
}

bool ModemBase::activatePDPContext() {
  String qiact = sendCommand("AT+QIACT?", 2000);
  if (qiact.indexOf("1,1") >= 0) {
    Serial.println("[Modem] ✓ PDP context already active");
    return true;
  }
  Serial.println("[Modem] → Activating PDP context...");
  sendCommand("AT+QIACT=1", 3000);
  delay(1000);
  qiact = sendCommand("AT+QIACT?", 2000);
  if (qiact.indexOf("1,1") >= 0) {
    Serial.println("[Modem] ✓ PDP context active");
    return true;
  }
  Serial.println("[Modem] ⚠ PDP activation failed — SMS still works");
  return false;
}

bool ModemBase::deactivatePDPContext() {
  Serial.println("[Modem] → Deactivating PDP context for PPP...");
  String resp = sendCommand("AT+QIDEACT=1", 5000);
  if (resp.indexOf("OK") >= 0) {
    Serial.println("[Modem] ✓ PDP context deactivated");
    delay(1000);
    return true;
  }
  Serial.println("[Modem] ⚠ PDP deactivation: " + resp);
  return false;
}

// ─── Diagnostics ─────────────────────────────────────────────────────────────
String ModemBase::getSignalQuality() {
  String csq = sendCommand("AT+CSQ", 1000);
  int p = csq.indexOf("+CSQ: ");
  if (p >= 0) {
    int rssi = csq.substring(p + 6, csq.indexOf(',', p)).toInt();
    if (rssi == 99) Serial.println("[Modem] ⚠ No signal!");
    else            Serial.printf("[Modem] Signal: %d/31\n", rssi);
  }
  return csq;
}

String ModemBase::getOperator() {
  return sendCommand("AT+COPS?", 3000);
}

String ModemBase::getIMEI() {
  String resp = sendCommand("AT+GSN", 2000);
  resp.trim();
  return resp;
}

void ModemBase::printDiagnostics() {
  Serial.println("\n[Modem] ===== Diagnostics =====");
  Serial.println("[Modem] Ready:    " + String(modemReady ? "YES" : "NO"));
  Serial.println("[Modem] Mode:     " + String(activeMode == MODEM_MODE_SMS  ? "SMS"  :
                                                activeMode == MODEM_MODE_DATA ? "DATA" : "NONE"));
  Serial.println("[Modem] Signal:   " + getSignalQuality());
  Serial.println("[Modem] Operator: " + getOperator());
  Serial.println("[Modem] Network:  " + String(isNetworkRegistered() ? "Registered" : "Not registered"));
  Serial.println("[Modem] IMEI:     " + getIMEI());
  Serial.println("[Modem] ======================\n");
}
