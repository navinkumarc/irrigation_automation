// ModemBase.h - Core modem hardware manager for Quectel EC200U
// Responsibility: UART init, SIM check, network registration, AT command utility.
// This class knows nothing about SMS, MQTT, or HTTP.
// Communication modules (ModemSMS, MQTTComm, HTTPComm) use this as a service,
// not as a base class.
#ifndef MODEM_BASE_H
#define MODEM_BASE_H

#include <Arduino.h>
#include "Config.h"

class ModemBase {
private:
  static bool modemReady;  // Set true after successful init()

public:
  ModemBase();

  // Initialize modem hardware: UART, SIM, network registration, APN, PDP context.
  // Must be called once before any communication module (SMS etc.) is configured.
  bool init();

  // Returns true if init() succeeded and modem is operational.
  bool isReady();

  // Allow communication modules to update the ready flag
  // (e.g. on modem restart detection).
  void setReady(bool ready);

  // Send an AT command and return the full response string.
  String sendCommand(const String &cmd, uint32_t timeout = 2000);

  // Flush the serial receive buffer.
  void clearSerialBuffer();

  // Helper diagnostics
  String getSignalQuality();
  String getOperator();
};

// Single global modem hardware instance — shared by all communication modules.
extern ModemBase modemBase;

// Shared UART for modem AT commands — defined in ModemBase.cpp.
extern HardwareSerial SerialAT;

#endif // MODEM_BASE_H
