// ModemBase.cpp - Core modem hardware manager for Quectel EC200U
#include "ModemBase.h"

// Shared UART — used by ModemBase and any module that sends AT commands (e.g. ModemSMS).
HardwareSerial SerialAT(1);

// Static member definition
bool ModemBase::modemReady = false;

// Global singleton instance
ModemBase modemBase;

ModemBase::ModemBase() {}

bool ModemBase::init() {
  Serial.println("[Modem] Initializing EC200U...");

  // Initialize GPIO pins — do not toggle (modem expected to be already powered).
  pinMode(MODEM_PWRKEY, OUTPUT);
  pinMode(MODEM_RESET,  OUTPUT);
  digitalWrite(MODEM_RESET,  HIGH);  // Keep reset de-asserted
  digitalWrite(MODEM_PWRKEY, LOW);   // Keep power key idle

  Serial.println("[Modem] Skipping hardware reset — using existing modem session");
  delay(1000);

  // Start UART
  SerialAT.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);
  delay(1000);

  // Verify AT communication
  Serial.println("[Modem] Testing communication...");
  bool atOk = false;
  for (int i = 0; i < 10; i++) {
    if (sendCommand("AT", 1000).indexOf("OK") >= 0) {
      Serial.println("[Modem] ✓ Communication OK");
      atOk = true;
      break;
    }
    delay(1000);
  }
  if (!atOk) {
    Serial.println("[Modem] ❌ Communication failed");
    return false;
  }

  // Disable echo
  sendCommand("ATE0", 1000);

  // Module info
  String model = sendCommand("ATI", 1000);
  Serial.println("[Modem] Model: " + model);

  // SIM check
  Serial.println("[Modem] Checking SIM...");
  bool simReady = false;
  for (int retry = 0; retry < 5; retry++) {
    String simStatus = sendCommand("AT+CPIN?", 2000);
    if (simStatus.indexOf("READY") >= 0) {
      simReady = true;
      Serial.println("[Modem] ✓ SIM ready");
      break;
    }
    if (simStatus.indexOf("+CME ERROR: 14") >= 0) {
      delay(1000);
    } else if (retry < 4) {
      delay(500);
    }
  }
  if (!simReady) {
    Serial.println("[Modem] ⚠ SIM check failed — continuing anyway");
  }

  // LTE only
  sendCommand("AT+QCFG=\"nwscanmode\",3,1", 2000);

  // APN
  Serial.println("[Modem] Configuring APN...");
  sendCommand("AT+QICSGP=1,1,\"" + String(MODEM_APN) + "\",\"\",\"\",1", 2000);

  // Network registration
  Serial.println("[Modem] Checking network registration...");
  bool registered = false;
  for (int attempts = 0; attempts < 10; attempts++) {
    String creg  = sendCommand("AT+CREG?",  1000);
    String cgreg = sendCommand("AT+CGREG?", 1000);
    if ((creg.indexOf(",1") >= 0 || creg.indexOf(",5") >= 0) ||
        (cgreg.indexOf(",1") >= 0 || cgreg.indexOf(",5") >= 0)) {
      registered = true;
      Serial.println("[Modem] ✓ Network registered");
      break;
    }
    delay(1000);
  }
  if (!registered) {
    Serial.println("[Modem] ⚠ Network registration timeout — SMS may still work");
  }

  // Signal and operator diagnostics
  Serial.println("[Modem] Signal quality: " + getSignalQuality());
  Serial.println("[Modem] Operator: "       + getOperator());

  // PDP context (needed for data; SMS works without it)
  Serial.println("[Modem] Checking data connection...");
  String qiact = sendCommand("AT+QIACT?", 2000);
  if (qiact.indexOf("1,1") < 0) {
    Serial.println("[Modem] → Activating PDP context...");
    sendCommand("AT+QIACT=1", 3000);
    delay(1000);
    qiact = sendCommand("AT+QIACT?", 2000);
    if (qiact.indexOf("1,1") >= 0) {
      Serial.println("[Modem] ✓ PDP context active");
    } else {
      Serial.println("[Modem] ⚠ PDP activation failed — SMS will still work");
    }
  } else {
    Serial.println("[Modem] ✓ PDP context already active");
  }

  modemReady = true;
  Serial.println("[Modem] ✓ Modem ready");
  return true;
}

bool ModemBase::isReady() {
  return modemReady;
}

void ModemBase::setReady(bool ready) {
  modemReady = ready;
}

String ModemBase::sendCommand(const String &cmd, uint32_t timeout) {
  Serial.println("[Modem] TX: " + cmd);
  clearSerialBuffer();
  SerialAT.println(cmd);

  unsigned long start = millis();
  String response = "";

  while (millis() - start < timeout) {
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

  if (response.length() > 0) {
    Serial.println("[Modem] RX: " + response);
  } else {
    Serial.println("[Modem] RX: (timeout)");
  }
  return response;
}

void ModemBase::clearSerialBuffer() {
  while (SerialAT.available()) SerialAT.read();
}

String ModemBase::getSignalQuality() {
  String csq = sendCommand("AT+CSQ", 1000);
  int rssiStart = csq.indexOf("+CSQ: ");
  if (rssiStart >= 0) {
    int commaPos = csq.indexOf(',', rssiStart);
    int rssi = csq.substring(rssiStart + 6, commaPos).toInt();
    if (rssi == 99) Serial.println("[Modem] ⚠ No signal!");
    else            Serial.printf("[Modem] Signal: %d/31\n", rssi);
  }
  return csq;
}

String ModemBase::getOperator() {
  return sendCommand("AT+COPS?", 3000);
}
