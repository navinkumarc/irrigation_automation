// ModemComm.cpp - Legacy modem communication class
// NOTE: This class is kept for reference only.
// Use ModemBase + ModemSMS for production code.
#include "ModemComm.h"
#include "ModemBase.h"  // For SerialAT

ModemComm::ModemComm() : mqttConnected(false), modemReady(false) {}

bool ModemComm::init() {
  Serial.println("[ModemComm] WARNING: ModemComm is legacy — use ModemBase + ModemSMS");

  // Power on EC200U
  pinMode(MODEM_PWRKEY, OUTPUT);
  pinMode(MODEM_RESET, OUTPUT);

  digitalWrite(MODEM_RESET, HIGH);
  delay(100);
  digitalWrite(MODEM_RESET, LOW);
  delay(100);
  digitalWrite(MODEM_RESET, HIGH);
  delay(2000);

  digitalWrite(MODEM_PWRKEY, HIGH);
  delay(500);
  digitalWrite(MODEM_PWRKEY, LOW);
  delay(2000);

  Serial.println("[ModemComm] Waiting for boot...");
  delay(5000);

  SerialAT.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);
  delay(1000);

  Serial.println("[ModemComm] Testing communication...");
  for (int i = 0; i < 5; i++) {
    String resp = sendCommand("AT", 1000);
    if (resp.indexOf("OK") >= 0) {
      Serial.println("[ModemComm] ✓ Communication OK");
      break;
    }
    delay(1000);
  }

  sendCommand("ATE0", 1000);

  String model = sendCommand("ATI", 1000);
  Serial.println("[ModemComm] Model: " + model);

  String simStatus = sendCommand("AT+CPIN?", 2000);
  if (simStatus.indexOf("READY") < 0) {
    Serial.println("[ModemComm] ❌ SIM not ready: " + simStatus);
    return false;
  }
  Serial.println("[ModemComm] ✓ SIM ready");

  sendCommand("AT+QCFG=\"nwscanmode\",3,1", 2000);
  sendCommand("AT+QICSGP=1,1,\"" + String(MODEM_APN) + "\",\"\",\"\",1", 2000);

  bool registered = false;
  for (int attempts = 0; attempts < 60 && !registered; attempts++) {
    String creg  = sendCommand("AT+CREG?",  1000);
    String cgreg = sendCommand("AT+CGREG?", 1000);
    if ((creg.indexOf(",1")  >= 0 || creg.indexOf(",5")  >= 0) ||
        (cgreg.indexOf(",1") >= 0 || cgreg.indexOf(",5") >= 0)) {
      registered = true;
      Serial.println("\n[ModemComm] ✓ Network registered");
    }
    if (!registered) {
      Serial.print(".");
      delay(1000);
    }
  }

  if (!registered) {
    Serial.println("\n[ModemComm] ❌ Network registration failed");
    return false;
  }

  sendCommand("AT+CSQ", 1000);
  sendCommand("AT+COPS?", 3000);
  sendCommand("AT+QIACT=1", 3000);
  delay(1000);

  modemReady = true;
  Serial.println("[ModemComm] ✓ Initialization complete");
  return true;
}

String ModemComm::sendCommand(const String &cmd, uint32_t timeout) {
  Serial.println("[ModemComm] TX: " + cmd);

  while (SerialAT.available()) SerialAT.read();
  SerialAT.println(cmd);

  unsigned long start = millis();
  String response = "";

  while (millis() - start < timeout) {
    if (SerialAT.available()) {
      char c = SerialAT.read();
      response += c;
      if (response.indexOf("OK\r\n") >= 0 || response.indexOf("ERROR") >= 0) break;
    }
    delay(1);
  }

  if (response.length() > 0) Serial.println("[ModemComm] RX: " + response);
  else                        Serial.println("[ModemComm] RX: (timeout)");
  return response;
}

bool ModemComm::configureMQTT() {
  if (!modemReady) {
    Serial.println("[ModemComm] ❌ Modem not ready for MQTT");
    return false;
  }

  Serial.println("[ModemComm] Configuring MQTT...");

  sendCommand("AT+QMTCFG=\"version\",0,4", 2000);
  sendCommand("AT+QMTCFG=\"keepalive\",0,120", 2000);
  sendCommand("AT+QMTCFG=\"session\",0,0", 2000);
  sendCommand("AT+QMTCFG=\"timeout\",0,30,3,0", 2000);

  String openCmd  = "AT+QMTOPEN=0,\"" + String(MQTT_BROKER) + "\"," + String(MQTT_PORT);
  String openResp = sendCommand(openCmd, 5000);
  if (openResp.indexOf("OK") < 0) {
    Serial.println("[ModemComm] ❌ Failed to open MQTT connection");
    return false;
  }
  delay(2000);

  String connectCmd = "AT+QMTCONN=0,\"" + String(MQTT_CLIENT_ID) + "\"";
  if (strlen(MQTT_USER) > 0) {
    connectCmd += ",\"" + String(MQTT_USER) + "\",\"" + String(MQTT_PASS) + "\"";
  }
  String connectResp = sendCommand(connectCmd, 5000);
  if (connectResp.indexOf("OK") < 0) {
    Serial.println("[ModemComm] ❌ Failed to connect to MQTT broker");
    return false;
  }
  delay(3000);

  mqttConnected = true;
  Serial.println("[ModemComm] ✓ MQTT connected");
  return true;
}

bool ModemComm::publish(const String &topic, const String &payload) {
  if (!mqttConnected) {
    Serial.println("[ModemComm] ❌ MQTT not connected");
    return false;
  }

  String pubCmd = "AT+QMTPUB=0,0,0,0,\"" + topic + "\",\"" + payload + "\"";
  String resp   = sendCommand(pubCmd, 3000);

  if (resp.indexOf("OK") >= 0) {
    Serial.println("[ModemComm] ✓ Published");
    return true;
  }
  Serial.println("[ModemComm] ❌ Publish failed");
  return false;
}

void ModemComm::processBackground() {
  while (SerialAT.available()) {
    String urc = SerialAT.readStringUntil('\n');
    urc.trim();
    if (urc.length() > 0) {
      Serial.println("[ModemComm] URC: " + urc);
      if (urc.indexOf("+QMTSTAT") >= 0 && urc.indexOf(",2") >= 0) {
        Serial.println("[ModemComm] MQTT disconnected, reconnecting...");
        mqttConnected = false;
        delay(1000);
        configureMQTT();
      }
    }
  }
}

bool ModemComm::isMQTTReady() {
  return modemReady && mqttConnected;
}
