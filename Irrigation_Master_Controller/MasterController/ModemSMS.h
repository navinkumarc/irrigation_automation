// ModemSMS.h - SMS communication for Quectel EC200U
//
// Dependency: ModemBase must be initialized before calling configure().
// Mode:       Requests MODEM_MODE_SMS from ModemBase on configure(),
//             releases it when no longer needed.
// Constraint: Cannot be active simultaneously with ModemPPPoS.
//             Only one of ENABLE_SMS / ENABLE_PPPOS should be 1 at a time.
//
// Safe to remove this file (and ModemSMS.cpp) when ENABLE_SMS = 0.

#ifndef MODEM_SMS_H
#define MODEM_SMS_H

#if ENABLE_SMS

#include <Arduino.h>
#include <map>
#include <vector>
#include "Config.h"

struct SMSMessage {
  int    index;
  String sender;
  String timestamp;
  String message;
};

class ModemSMS {
private:
  bool          smsReady;
  bool          needsReconfigure;    // Set when RDY/restart URC received
  unsigned long reconfigureAfter;    // millis() timestamp when safe to reconfigure
  unsigned long lastSMSCheck;
  unsigned long smsCheckInterval;
  std::vector<int>                pendingMessageIndices;
  std::map<String, unsigned long> lastAlertTime;

  bool   waitForPrompt(char ch, unsigned long timeout = 5000);
  String readSMSByIndex(int index, String &sender, String &timestamp);
  bool   configureTextMode();
  bool   isValidPhoneNumber(const String &phoneNumber);
  void   handleNewMessageURC(int index);
  void   processURC(const String &urc);
  bool   isNetworkProviderMessage(const String &sender);
  bool   shouldSendAlert(const String &alertKey);

public:
  ModemSMS();

  // Call after ModemBase::init() succeeds.
  // Internally calls modemBase.requestMode(MODEM_MODE_SMS).
  bool configure();

  // Release the modem mode (call when SMS is no longer needed,
  // e.g. before switching to PPPoS data mode).
  void release();

  // Send an SMS to an E.164 international number.
  bool sendSMS(const String &phoneNumber, const String &message);

  // Incoming message queue management
  bool              checkNewMessages();
  int               getUnreadCount();
  std::vector<int>  getUnreadIndices();
  bool              readSMS(int index, SMSMessage &sms);
  bool              deleteSMS(int index);
  bool              deleteAllSMS();

  // Call from main loop — reads URCs and queues incoming messages.
  void processBackground();

  bool isReady();

  // Diagnostics
  void printSMSDiagnostics();
  void scanForNewMessages();

  // Process queued messages; returns user command messages
  // (network provider messages are forwarded to admin).
  std::vector<SMSMessage> processIncomingMessages(const String &adminPhone);

  // Rate-limited alert helpers
  bool sendNotification(const String &message, const String &alertKey = "");
  bool sendNotificationToPhones(const String &message,
                                const std::vector<String> &phoneNumbers,
                                const String &alertKey = "");
};

// Global instance — defined in ModemSMS.cpp.
extern ModemSMS modemSMS;

#endif // ENABLE_SMS
#endif // MODEM_SMS_H
