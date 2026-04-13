// ModemBase.h - Core modem hardware manager for Quectel EC200U
//
// Responsibilities:
//   • UART initialization and hardware power sequencing
//   • AT command interface (sendCommand / clearSerialBuffer)
//   • SIM check and network registration
//   • Mode management: only ONE mode is active at a time
//       MODEM_MODE_NONE  — modem is idle / unconfigured
//       MODEM_MODE_SMS   — modem owned by ModemSMS (AT command mode)
//       MODEM_MODE_DATA  — modem owned by ModemPPPoS (PPP data mode)
//   • APN configuration and PDP context lifecycle
//   • Diagnostics: signal quality, operator, IMEI
//
// Dependent modules:
//   ModemSMS    — calls modemBase.requestMode(MODEM_MODE_SMS)  before configure()
//   ModemPPPoS  — calls modemBase.requestMode(MODEM_MODE_DATA) before connect()
//   Both must call modemBase.releaseMode() when done.

#ifndef MODEM_BASE_H
#define MODEM_BASE_H

#include <Arduino.h>
#include "Config.h"

// ─── Modem operating mode ─────────────────────────────────────────────────────

enum ModemMode {
  MODEM_MODE_NONE = 0,   // Idle — no module has claimed the modem
  MODEM_MODE_SMS,        // AT command mode  — owned by ModemSMS
  MODEM_MODE_DATA        // PPP data mode    — owned by ModemPPPoS
};

// ─── ModemBase ────────────────────────────────────────────────────────────────

class ModemBase {
private:
  static bool      modemReady;
  static ModemMode activeMode;

public:
  ModemBase();

  // ── Lifecycle ──────────────────────────────────────────────────────────────

  // Full hardware init: UART, power, SIM, network registration, APN.
  // Must be called once before any dependent module is used.
  bool init();

  // True if init() succeeded and modem is operational.
  bool isReady() const;

  // Allow dependent modules to update the ready flag.
  void setReady(bool ready);

  // ── Mode management ────────────────────────────────────────────────────────

  // Request exclusive use of the modem.
  // Returns true if granted (modem ready, no other module holds it,
  // or same module re-requests its own mode).
  // Returns false if a different module already holds the modem.
  bool requestMode(ModemMode mode);

  // Release the modem so another module can claim it.
  void releaseMode(ModemMode mode);

  // Mode queries
  ModemMode getActiveMode() const;
  bool      isInMode(ModemMode mode) const;

  // ── AT interface ───────────────────────────────────────────────────────────

  // Send an AT command and return the full response string.
  // Safe to call only in MODEM_MODE_SMS or MODEM_MODE_NONE (not in PPP mode).
  String sendCommand(const String &cmd, uint32_t timeout_ms = 2000);

  // Flush the serial receive buffer.
  void clearSerialBuffer();

  // ── Network helpers (called internally and by dependent modules) ───────────

  bool isNetworkRegistered();
  bool activatePDPContext();
  bool deactivatePDPContext();
  bool configureAPN(const String &apn = MODEM_APN);

  // ── Diagnostics ────────────────────────────────────────────────────────────
  String getSignalQuality();
  String getOperator();
  String getIMEI();
  void   printDiagnostics();
};

// ─── Globals ──────────────────────────────────────────────────────────────────

extern ModemBase      modemBase;
extern HardwareSerial SerialAT;

#endif // MODEM_BASE_H
