#ifndef LORA_COMM_H
#define LORA_COMM_H

#include "LoRaWan_APP.h"
#include "Config.h"
#include "Utils.h"
#include "MessageQueue.h"

class LoRaComm {
private:
  static char txBuffer[LORA_BUFFER_SIZE];
  static char rxBuffer[LORA_BUFFER_SIZE];
  static char rxBufferSafe[LORA_BUFFER_SIZE];
  static volatile bool rxFlag;
  static volatile bool txDoneFlag;
  static volatile uint16_t rxSize;
  static int16_t lastRssi;
  static int8_t lastSnr;
  static String lastRxMessage;
  
  static void onTxDone(void);
  static void onTxTimeout(void);
  static void onRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr);
  
  bool parseAck(const char* msg, uint32_t wantMid, const String &wantType,
                int wantNode, const String &wantSched, int wantSeqIndex);
  bool waitForAck(int node, const String &type, const String &sched, 
                  int seqIdx, uint32_t mid, uint32_t timeout);
  void sendRaw(const String &cmd);

public:
  LoRaComm();
  bool init();
  bool sendWithAck(const String &cmdType, int node, const String &schedId,
                   int seqIndex, uint32_t durationMs = 0);
  void processIncoming();
};

extern LoRaComm loraComm;
#endif