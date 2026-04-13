// LoRaComm.cpp - FINAL WORKING VERSION
#include "LoRaComm.h"

// Static member initialization
char LoRaComm::txBuffer[LORA_BUFFER_SIZE];
char LoRaComm::rxBuffer[LORA_BUFFER_SIZE];
char LoRaComm::rxBufferSafe[LORA_BUFFER_SIZE];
volatile bool LoRaComm::rxFlag = false;
volatile bool LoRaComm::txDoneFlag = false;
volatile uint16_t LoRaComm::rxSize = 0;
int16_t LoRaComm::lastRssi = 0;
int8_t LoRaComm::lastSnr = 0;
String LoRaComm::lastRxMessage = "";

static RadioEvents_t RadioEvents;

LoRaComm::LoRaComm() {}

// ========== Interrupt Handlers ==========
void LoRaComm::onTxDone(void) {
  Serial.println("[LoRa] TX Done");
  txDoneFlag = true;
  Radio.Rx(0);
}

void LoRaComm::onTxTimeout(void) {
  Serial.println("[LoRa] TX Timeout");
  txDoneFlag = true;
  Radio.Rx(0);
}

void LoRaComm::onRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr) {
  // Validate payload pointer and size
  if (payload == nullptr || size == 0) {
    Serial.println("[LoRa] ⚠ Invalid payload in onRxDone");
    return;
  }

  if (size >= LORA_BUFFER_SIZE) size = LORA_BUFFER_SIZE - 1;
  memcpy(rxBuffer, payload, size);
  rxBuffer[size] = '\0';
  lastRxMessage = String(rxBuffer);
  rxSize = size;
  rxFlag = true;
  lastRssi = rssi;
  lastSnr = snr;
  Serial.printf("[LoRa] RX: %s (RSSI=%d, SNR=%d)\n", rxBuffer, rssi, snr);
}

// ========== Initialize LoRa ==========
bool LoRaComm::init() {
  Serial.println("[LoRa] Initializing...");

  Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);

  //static RadioEvents_t RadioEvents;  // CRITICAL: Must be static!
  RadioEvents.TxDone = onTxDone;
  RadioEvents.TxTimeout = onTxTimeout;
  RadioEvents.RxDone = onRxDone;

  Radio.Init(&RadioEvents);
  Radio.SetChannel(RF_FREQUENCY);
  
  Radio.SetTxConfig(MODEM_LORA, TX_OUTPUT_POWER, 0, LORA_BANDWIDTH,
                    LORA_SPREADING_FACTOR, LORA_CODINGRATE,
                    LORA_PREAMBLE_LENGTH, LORA_FIX_LENGTH_PAYLOAD_ON,
                    true, 0, 0, LORA_IQ_INVERSION_ON, 3000);
  
  Radio.SetRxConfig(MODEM_LORA, LORA_BANDWIDTH, LORA_SPREADING_FACTOR,
                    LORA_CODINGRATE, 0, LORA_PREAMBLE_LENGTH,
                    LORA_SYMBOL_TIMEOUT, LORA_FIX_LENGTH_PAYLOAD_ON,
                    0, true, 0, 0, LORA_IQ_INVERSION_ON, true);
  
  Radio.Rx(0);
  
  Serial.println("[LoRa] Init OK, listening...");
  return true;
}

// ========== Send LoRa Packet ==========
void LoRaComm::sendRaw(const String &cmd) {
  snprintf(txBuffer, LORA_BUFFER_SIZE, "%s", cmd.c_str());
  Serial.printf("[LoRa] TX: %s\n", txBuffer);
  
  txDoneFlag = false;
  Radio.Send((uint8_t *)txBuffer, strlen(txBuffer));
  
  // Wait for TX to complete
  unsigned long start = millis();
  while (!txDoneFlag && (millis() - start < 3000)) {
    delay(10);
    Radio.IrqProcess();
  }
  
  if (!txDoneFlag) {
    Serial.println("[LoRa] ⚠ TX didn't complete in time");
  }
}

// ========== Parse ACK ==========
bool LoRaComm::parseAck(const char* msg, uint32_t wantMid, const String &wantType,
                        int wantNode, const String &wantSched, int wantSeqIndex) {
  
  if (strncmp(msg, "ACK|", 4) != 0) return false;
  
  const char* pipe1 = strchr(msg + 4, '|');
  if (!pipe1) return false;
  
  const char* pipe2 = strchr(pipe1 + 1, '|');
  if (!pipe2) return false;
  
  const char* pipe3 = strchr(pipe2 + 1, '|');
  if (!pipe3) return false;
  
  // Parse MID
  char midBuf[16];
  int midLen = pipe1 - (msg + 4);
  if (midLen > 15 || midLen < 0) return false;
  strncpy(midBuf, msg + 4, midLen);
  midBuf[midLen] = '\0';
  
  if (strncmp(midBuf, "MID=", 4) != 0) return false;
  
  uint32_t mid = atoi(midBuf + 4);
  if (mid != wantMid) return false;
  
  // Parse Type
  char typeBuf[32];
  int typeLen = pipe2 - pipe1 - 1;
  if (typeLen > 31 || typeLen < 0) return false;
  strncpy(typeBuf, pipe1 + 1, typeLen);
  typeBuf[typeLen] = '\0';
  
  // Accept both exact match and PONG (for PING command)
  if (strcmp(typeBuf, wantType.c_str()) != 0 && strcmp(typeBuf, "PONG") != 0) {
    return false;
  }
  
  // Parse key-value pairs
  char kvBuf[128];
  int kvLen = pipe3 - pipe2 - 1;
  if (kvLen > 127 || kvLen < 0) return false;
  strncpy(kvBuf, pipe2 + 1, kvLen);
  kvBuf[kvLen] = '\0';
  
  int node = -1, idx = -1;
  char sched[32] = "";
  
  char* saveptr;
  char* token = strtok_r(kvBuf, ",", &saveptr);
  while (token != NULL) {
    while (*token == ' ') token++;
    
    if (strncmp(token, "N=", 2) == 0) {
      node = atoi(token + 2);
    } else if (strncmp(token, "I=", 2) == 0) {
      idx = atoi(token + 2);
    } else if (strncmp(token, "S=", 2) == 0) {
      strncpy(sched, token + 2, 31);
      sched[31] = '\0';
    }
    token = strtok_r(NULL, ",", &saveptr);
  }
  
  if (node != wantNode) return false;
  if (idx != wantSeqIndex) return false;
  if (strcmp(sched, wantSched.c_str()) != 0) return false;
  if (strstr(pipe3 + 1, "OK") == NULL) return false;
  
  return true;
}

// ========== Wait for ACK ==========
bool LoRaComm::waitForAck(int node, const String &type, const String &sched,
                          int seqIdx, uint32_t mid, uint32_t timeout) {
  unsigned long start = millis();
  
  Serial.printf("[LoRa] Waiting ACK: MID=%u, Node=%d, Type=%s\n", mid, node, type.c_str());
  
  while (millis() - start < timeout) {
    Radio.IrqProcess();
    
    if (rxFlag) {
      memcpy(rxBufferSafe, rxBuffer, rxSize);
      rxBufferSafe[rxSize] = '\0';
      int16_t rssi = lastRssi;
      int8_t snr = lastSnr;
      rxFlag = false;
      
      Serial.printf("[LoRa] Check: %s (RSSI=%d, SNR=%d)\n", rxBufferSafe, rssi, snr);
      
      if (parseAck(rxBufferSafe, mid, type, node, sched, seqIdx)) {
        Serial.println("[LoRa] ✓✓✓ ACK MATCHED!");
        return true;
      } else {
        Serial.println("[LoRa] Not matching ACK");
        
        // Queue non-ACK messages
        if (strlen(rxBufferSafe) > 0 && strncmp(rxBufferSafe, "ACK|", 4) != 0) {
          String msg = String(rxBufferSafe);
          if (msg.indexOf("SRC=") < 0) msg += ",SRC=LORA";
          incomingQueue.enqueue(msg);
        }
      }
    }
    
    delay(10);
  }
  
  Serial.println("[LoRa] ✗ ACK timeout");
  return false;
}

// ========== Send with ACK retry ==========
bool LoRaComm::sendWithAck(const String &cmdType, int node, const String &schedId,
                           int seqIndex, uint32_t durationMs) {
  
  if (cmdType.length() > 20 || schedId.length() > 50) {
    Serial.println("[LoRa] ❌ Parameters too long!");
    return false;
  }
  
  if (node <= 0 || node > 255) {
    Serial.println("[LoRa] ❌ Invalid node ID!");
    return false;
  }
  
  uint32_t mid = getNextMsgId();
  
  String cmd = String("CMD|MID=") + String(mid) + 
               String("|") + cmdType + 
               String("|N=") + String(node) + 
               String(",S=") + schedId + 
               String(",I=") + String(seqIndex);
  
  if (cmdType == "OPEN" && durationMs > 0) {
    cmd += String(",T=") + String(durationMs);
  }
  
  if (cmd.length() >= LORA_BUFFER_SIZE) {
    Serial.println("[LoRa] ❌ Command too long!");
    return false;
  }
  
  uint8_t attempt = 0;
  while (attempt < LORA_MAX_RETRIES) {
    Serial.printf("[LoRa] Attempt %d/%d\n", attempt + 1, LORA_MAX_RETRIES);
    
    sendRaw(cmd);
    
    if (waitForAck(node, cmdType, schedId, seqIndex, mid, LORA_ACK_TIMEOUT_MS)) {
      Serial.println("[LoRa] ✓✓✓ SUCCESS!");
      return true;
    }
    
    attempt++;
    if (attempt < LORA_MAX_RETRIES) {
      Serial.println("[LoRa] Retry...");
      delay(300);
    }
  }
  
  Serial.printf("[LoRa] ✗✗✗ FAILED after %d attempts\n", LORA_MAX_RETRIES);
  return false;
}

// ========== Process Incoming ==========
void LoRaComm::processIncoming() {
  Radio.IrqProcess();
  
  if (rxFlag) {
    memcpy(rxBufferSafe, rxBuffer, rxSize);
    rxBufferSafe[rxSize] = '\0';
    int16_t rssi = lastRssi;
    int8_t snr = lastSnr;
    rxFlag = false;
    
    if (strlen(rxBufferSafe) == 0) return;
    
    Serial.printf("[LoRa] ✓ RX: %s (RSSI=%d, SNR=%d)\n", rxBufferSafe, rssi, snr);
    
    // Skip ACKs (handled in waitForAck)
    if (strncmp(rxBufferSafe, "ACK|", 4) == 0) {
      Serial.println("[LoRa] ACK (handled in waitForAck)");
      return;
    }
    
    String payload = String(rxBufferSafe);
    
    // Accept STAT messages
    if (payload.startsWith("STAT|")) {
      Serial.println("[LoRa] ✓ STAT message - QUEUING!");
      if (payload.indexOf("SRC=") < 0) payload += ",SRC=LORA";
      incomingQueue.enqueue(payload);
      Serial.println("[LoRa] ✓ Queued STAT");
      return;
    }
    
    // Accept AUTO_CLOSE
    if (payload.startsWith("AUTO_CLOSE|")) {
      Serial.println("[LoRa] ✓ AUTO_CLOSE - QUEUING!");
      if (payload.indexOf("SRC=") < 0) payload += ",SRC=LORA";
      incomingQueue.enqueue(payload);
      Serial.println("[LoRa] ✓ Queued AUTO_CLOSE");
      return;
    }
    
    // Accept any other message
    Serial.println("[LoRa] ✓ Generic message - QUEUING!");
    if (payload.indexOf("SRC=") < 0) payload += ",SRC=LORA";
    incomingQueue.enqueue(payload);
    Serial.println("[LoRa] ✓ Queued");
  }
}