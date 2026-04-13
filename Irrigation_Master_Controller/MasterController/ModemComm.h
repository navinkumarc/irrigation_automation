// ModemComm.h
// NOTE: ModemComm is a legacy class kept for reference only.
// For SMS use ModemSMS. For PPPoS data use PPPoSManager.
// Do not instantiate ModemComm directly — it is not fully implemented.
#ifndef MODEM_COMM_H
#define MODEM_COMM_H

#include <Arduino.h>
#include "Config.h"
#include "Utils.h"
#include "ModemBase.h"
#include "MessageQueue.h"

class ModemComm {
private:
  bool mqttConnected;
  bool modemReady;

  String sendCommand(const String &cmd, uint32_t timeout = 2000);

public:
  ModemComm();
  bool init();
  bool configureMQTT();
  bool publish(const String &topic, const String &payload);
  void processBackground();
  bool isMQTTReady();
};

// No global instance — ModemComm is not used in the current architecture.
// Use modemBase (ModemBase) + modemSMS (ModemSMS) instead.

#endif
