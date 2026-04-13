// ModemComm.h
#ifndef MODEM_COMM_H
#define MODEM_COMM_H

#include <Arduino.h>
#include "Config.h"
#include "Utils.h"
#include "ModemBase.h"
#include "MessageQueue.h"

class ModemComm {
private:
  HardwareSerial *serial;
  String lineBuffer;
  unsigned long lastActivity;
  unsigned long lastMqttURC;
  bool mqttReady;
  
  // Add these missing member variables:
  bool mqttConnected;
  bool modemReady;
  
  // Change sendAT to sendCommand to match implementation:
  String sendCommand(const String &cmd, uint32_t timeout = 2000);
  
  bool waitForPrompt(char ch, unsigned long timeout = 5000);
  String readSMSByIndex(int index, String &sender);
  bool isModemReadyForSMS();

public:
  ModemComm();
  bool init();
  bool configureMQTT();
  
  // Change parameter type from const char* to const String& to match implementation:
  bool publish(const String &topic, const String &payload);
  
  bool sendSMS(const String &num, const String &text);
  bool ntpSync();
  void processBackground();
  bool isMQTTReady();
};

extern ModemComm modemComm;

#endif