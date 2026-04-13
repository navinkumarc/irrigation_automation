// ModemSMS.cpp - SMS communication for Quectel EC200U
// Uses modemBase (ModemBase) for AT command sending — no inheritance.


#include "ModemSMS.h"
#include "ModemBase.h"  // for modemBase, SerialAT

ModemSMS modemSMS;

ModemSMS::ModemSMS() : smsReady(false), lastSMSCheck(0), smsCheckInterval(10000) {
  pendingMessageIndices.clear();
}

bool ModemSMS::configure() {
  if (!modemBase.isReady()) {
    Serial.println("[SMS] Modem not ready — verifying communication...");
    String resp = modemBase.sendCommand("AT", 2000);
    if (resp.indexOf("OK") >= 0) {
      modemBase.setReady(true);
      Serial.println("[SMS] ✓ Modem responding");
      delay(2000);
    } else {
      Serial.println("[SMS] ❌ Modem not responding");
      return false;
    }
  }

  Serial.println("[SMS] Configuring...");

  // Route URCs to UART1 (not USB) so +CMTI arrives on ESP32 serial
  modemBase.sendCommand("AT+QURCCFG=\"urcport\",\"uart1\"", 2000);
  Serial.println("[SMS] ✓ URCs routed to UART1");

  // Ring indicator for incoming SMS
  modemBase.sendCommand("AT+QCFG=\"urc/ri/smsincoming\",\"pulse\",120", 2000);
  Serial.println("[SMS] ✓ SMS RI configured");

  if (!configureTextMode()) return false;

  // Prefer SIM storage, fall back to ME
  String resp = modemBase.sendCommand("AT+CPMS=\"SM\",\"SM\",\"SM\"", 2000);
  if (resp.indexOf("OK") < 0) {
    Serial.println("[SMS] ⚠ Trying ME storage");
    modemBase.sendCommand("AT+CPMS=\"ME\",\"ME\",\"ME\"", 2000);
  }

  // Enable new-SMS notifications
  String cnmiResp = modemBase.sendCommand("AT+CNMI=2,1,0,0,0", 2000);
  if (cnmiResp.indexOf("OK") >= 0) {
    Serial.println("[SMS] ✓ SMS notifications enabled");
  } else {
    Serial.println("[SMS] ❌ Failed to enable SMS notifications: " + cnmiResp);
  }

  String cnmiCheck = modemBase.sendCommand("AT+CNMI?", 2000);
  Serial.println("[SMS] CNMI: " + cnmiCheck);

  modemBase.sendCommand("AT+CSCS=\"GSM\"", 2000);

  // Check SMSC address
  String csca = modemBase.sendCommand("AT+CSCA?", 2000);
  Serial.println("[SMS] SMSC: " + csca);
  if (csca.indexOf("ERROR") >= 0 || csca.indexOf("\"\"") >= 0 || csca.indexOf("+CSCA: \"\"") >= 0) {
    Serial.println("[SMS] ⚠ SMSC not configured — sending will fail!");
    Serial.println("[SMS] ℹ Set with: AT+CSCA=\"+<carrier_smsc>\"");
  } else {
    Serial.println("[SMS] ✓ SMSC configured");
  }

  smsReady = true;
  Serial.println("[SMS] ✓ Configuration complete");

  // Clean up read messages
  String delResp = modemBase.sendCommand("AT+CMGD=1,1", 3000);
  Serial.println("[SMS] Cleanup: " + (delResp.indexOf("OK") >= 0 ? String("✓ done") : delResp));

  String storageCheck = modemBase.sendCommand("AT+CPMS?", 2000);
  Serial.println("[SMS] Storage: " + storageCheck);

  String listCmd = modemBase.sendCommand("AT+CMGL=\"ALL\"", 5000);
  Serial.println("[SMS] Existing messages: " + listCmd);

  return true;
}

bool ModemSMS::configureTextMode() {
  String resp = modemBase.sendCommand("AT+CMGF=1", 2000);
  if (resp.indexOf("OK") >= 0) {
    Serial.println("[SMS] ✓ Text mode enabled");
    return true;
  }
  Serial.println("[SMS] ❌ Failed to set text mode");
  return false;
}

bool ModemSMS::isValidPhoneNumber(const String &phoneNumber) {
  if (phoneNumber.length() < 7) return false;
  if (phoneNumber.charAt(0) != '+') return false;

  int digitCount = 0;
  for (unsigned int i = 1; i < phoneNumber.length(); i++) {
    if (isdigit(phoneNumber.charAt(i))) {
      digitCount++;
    } else if (phoneNumber.charAt(i) != ' ' && phoneNumber.charAt(i) != '-') {
      return false;
    }
  }
  if (digitCount < 7 || digitCount > 15) return false;
  if (phoneNumber.startsWith("+0000") || phoneNumber.startsWith("+0987")) return false;
  return true;
}

bool ModemSMS::sendSMS(const String &phoneNumber, const String &message) {
  if (!smsReady) {
    Serial.println("[SMS] ❌ Not ready");
    return false;
  }
  if (!isValidPhoneNumber(phoneNumber)) {
    Serial.println("[SMS] ❌ Invalid number: " + phoneNumber);
    return false;
  }

  Serial.println("[SMS] Sending to: " + phoneNumber);
  Serial.println("[SMS] Message:    " + message);

  modemBase.clearSerialBuffer();

  String cmd = "AT+CMGS=\"" + phoneNumber + "\"";
  SerialAT.println(cmd);
  Serial.println("[SMS] TX: " + cmd);

  if (!waitForPrompt('>', 5000)) {
    Serial.println("[SMS] ❌ No '>' prompt");
    return false;
  }

  SerialAT.print(message);
  delay(100);
  SerialAT.write(0x1A);  // Ctrl+Z
  Serial.println("[SMS] Waiting for send confirmation...");

  unsigned long start = millis();
  String response = "";
  bool success = false;
  bool errorDetected = false;

  while (millis() - start < 30000) {
    if (SerialAT.available()) {
      char c = SerialAT.read();
      response += c;
      if (response.indexOf("+CMGS:") >= 0 && response.indexOf("OK") >= 0) {
        success = true; break;
      }
      if (response.indexOf("ERROR") >= 0) {
        errorDetected = true;
        delay(500);
        while (SerialAT.available()) response += (char)SerialAT.read();
        break;
      }
    }
    delay(10);
  }

  Serial.println("[SMS] Response: " + response);

  if (success) {
    Serial.println("[SMS] ✓ Sent successfully");
    return true;
  }

  Serial.println("[SMS] ❌ Send failed");
  if (errorDetected) {
    int cmsPos = response.indexOf("+CMS ERROR:");
    if (cmsPos >= 0) {
      int codeStart = cmsPos + 12;
      int codeEnd   = response.indexOf("\n", codeStart);
      if (codeEnd < 0) codeEnd = response.length();
      String errorCode = response.substring(codeStart, codeEnd);
      errorCode.trim();
      Serial.println("[SMS] CMS Error: " + errorCode);

      int code = errorCode.toInt();
      switch (code) {
        case 310: Serial.println("[SMS] SIM not inserted");     break;
        case 311: Serial.println("[SMS] SIM PIN required");     break;
        case 320: Serial.println("[SMS] Memory failure");       break;
        case 322: Serial.println("[SMS] Memory full");          break;
        case 330: Serial.println("[SMS] SMSC unknown");         break;
        case 331: Serial.println("[SMS] No network service");   break;
        case 332: Serial.println("[SMS] Network timeout");      break;
        case 500: Serial.println("[SMS] Unknown error");        break;
        case 521: Serial.println("[SMS] No SMSC address");      break;
        case 530: Serial.println("[SMS] Invalid destination");  break;
        default:  Serial.println("[SMS] Error code: " + errorCode); break;
      }
      if (code == 330 || code == 521) Serial.println("[SMS] ℹ Set SMSC: AT+CSCA=\"+number\"");
      if (code == 331) Serial.println("[SMS] ℹ Check: AT+CREG?");
    }
  }
  return false;
}

bool ModemSMS::waitForPrompt(char ch, unsigned long timeout) {
  unsigned long start = millis();
  while (millis() - start < timeout) {
    if (SerialAT.available()) {
      char c = SerialAT.read();
      Serial.print(c);
      if (c == ch) return true;
    }
    delay(10);
  }
  return false;
}

void ModemSMS::handleNewMessageURC(int index) {
  for (int idx : pendingMessageIndices) {
    if (idx == index) return;
  }
  pendingMessageIndices.push_back(index);
  Serial.println("[SMS] 📨 Message queued at index " + String(index));
}

bool ModemSMS::checkNewMessages() {
  return !pendingMessageIndices.empty();
}

int ModemSMS::getUnreadCount() {
  return pendingMessageIndices.size();
}

std::vector<int> ModemSMS::getUnreadIndices() {
  std::vector<int> indices = pendingMessageIndices;
  pendingMessageIndices.clear();
  return indices;
}

bool ModemSMS::readSMS(int index, SMSMessage &sms) {
  if (!smsReady) return false;
  Serial.println("[SMS] Reading index: " + String(index));

  String sender, timestamp;
  String message = readSMSByIndex(index, sender, timestamp);

  if (message.length() > 0) {
    sms.index     = index;
    sms.sender    = sender;
    sms.timestamp = timestamp;
    sms.message   = message;
    Serial.println("[SMS] From: " + sender + " | " + message);
    return true;
  }
  return false;
}

String ModemSMS::readSMSByIndex(int index, String &sender, String &timestamp) {
  String cmd  = "AT+CMGR=" + String(index);
  String resp = modemBase.sendCommand(cmd, 3000);

  int cmgrPos = resp.indexOf("+CMGR:");
  if (cmgrPos < 0) {
    Serial.println("[SMS] ❌ Failed to read SMS");
    return "";
  }

  // Detect PDU mode (no quotes before first newline)
  int firstQuoteAfterCmgr   = resp.indexOf("\"", cmgrPos);
  int firstNewlineAfterCmgr = resp.indexOf("\n", cmgrPos);
  if (firstNewlineAfterCmgr > 0 &&
      (firstQuoteAfterCmgr < 0 || firstNewlineAfterCmgr < firstQuoteAfterCmgr)) {
    Serial.println("[SMS] ⚠ PDU mode detected — reconfiguring text mode...");
    smsReady = false;
    if (configureTextMode()) {
      smsReady = true;
      resp = modemBase.sendCommand(cmd, 3000);
      cmgrPos = resp.indexOf("+CMGR:");
      if (cmgrPos < 0) { Serial.println("[SMS] ❌ Still failed after reconfig"); return ""; }
    } else {
      Serial.println("[SMS] ❌ Text mode reconfig failed");
      return "";
    }
  }

  // Parse sender (3rd quoted field)
  int q1 = resp.indexOf("\"", cmgrPos + 7);
  int q2 = resp.indexOf("\"", q1 + 1);
  int q3 = resp.indexOf("\"", q2 + 1);
  int q4 = resp.indexOf("\"", q3 + 1);
  if (q1 >= 0 && q2 >= 0 && q3 >= 0 && q4 >= 0) {
    sender = resp.substring(q3 + 1, q4);
  }

  // Parse timestamp (5th quoted field)
  int q5 = resp.indexOf("\"", q4 + 1);
  int q6 = resp.indexOf("\"", q5 + 1);
  if (q5 >= 0 && q6 >= 0) {
    timestamp = resp.substring(q5 + 1, q6);
  }

  // Extract message body (after first newline past +CMGR header)
  int msgStart = resp.indexOf("\n", cmgrPos);
  if (msgStart >= 0) {
    msgStart++;
    int msgEnd = resp.indexOf("\n\nOK", msgStart);
    if (msgEnd < 0) msgEnd = resp.indexOf("\nOK", msgStart);
    if (msgEnd >= 0) {
      String message = resp.substring(msgStart, msgEnd);
      message.trim();
      return message;
    }
  }
  return "";
}

bool ModemSMS::deleteSMS(int index) {
  String resp = modemBase.sendCommand("AT+CMGD=" + String(index), 2000);
  bool ok = resp.indexOf("OK") >= 0;
  Serial.println("[SMS] Delete " + String(index) + ": " + (ok ? "✓" : "❌"));
  return ok;
}

bool ModemSMS::deleteAllSMS() {
  String resp = modemBase.sendCommand("AT+CMGD=1,4", 3000);
  bool ok = resp.indexOf("OK") >= 0;
  Serial.println("[SMS] Delete all: " + (ok ? String("✓") : resp));
  return ok;
}

bool ModemSMS::isReady() {
  return smsReady;
}

void ModemSMS::processBackground() {
  static unsigned long lastCheck = 0;
  bool log = (millis() - lastCheck > 5000);
  if (log) lastCheck = millis();

  int available = SerialAT.available();
  if (log) Serial.println("[SMS] processBackground — available: " + String(available));

  if (available <= 0) return;

  int linesRead = 0;
  while (SerialAT.available()) {
    String line = SerialAT.readStringUntil('\n');
    line.trim();
    if (line.length() > 0) {
      linesRead++;
      Serial.println("[SMS] URC line: '" + line + "'");
      if (line.startsWith("+") || line.indexOf("RDY") >= 0 ||
          line.indexOf("POWERED DOWN") >= 0 || line.indexOf("QIND") >= 0) {
        processURC(line);
      }
    }
  }
  if (linesRead > 0) Serial.println("[SMS] Read " + String(linesRead) + " line(s)");
}

void ModemSMS::processURC(const String &urc) {
  if (urc.indexOf("RDY") >= 0 || urc.indexOf("POWERED DOWN") >= 0) {
    Serial.println("[SMS] ⚠ Modem restart detected — marking not ready");
    smsReady = false;
    modemBase.setReady(false);
    return;
  }
  if (urc.indexOf("+CMTI:") >= 0) {
    Serial.println("[SMS] 📨 New SMS URC");
    int indexPos = urc.lastIndexOf(",");
    if (indexPos >= 0) {
      String indexStr = urc.substring(indexPos + 1);
      indexStr.trim();
      int index = indexStr.toInt();
      if (index >= 0) handleNewMessageURC(index);
    }
  }
  if (urc.indexOf("+CDS:")  >= 0) Serial.println("[SMS] 📬 Delivery report");
  if (urc.indexOf("+CMGS:") >= 0) Serial.println("[SMS] ✓ Send acknowledged");
}

void ModemSMS::printSMSDiagnostics() {
  Serial.println("\n[SMS] === Diagnostics ===");
  Serial.println("[SMS] Ready: " + String(smsReady ? "Yes" : "No"));
  if (!modemBase.isReady()) { Serial.println("[SMS] ⚠ Modem not ready"); return; }
  Serial.println("[SMS] Network: " + modemBase.sendCommand("AT+CREG?",  2000));
  Serial.println("[SMS] Signal:  " + modemBase.sendCommand("AT+CSQ",    2000));
  Serial.println("[SMS] Format:  " + modemBase.sendCommand("AT+CMGF?",  2000));
  Serial.println("[SMS] Storage: " + modemBase.sendCommand("AT+CPMS?",  2000));
  Serial.println("[SMS] SMSC:    " + modemBase.sendCommand("AT+CSCA?",  2000));
  Serial.println("[SMS] Charset: " + modemBase.sendCommand("AT+CSCS?",  2000));
  Serial.println("[SMS] URC cfg: " + modemBase.sendCommand("AT+QURCCFG=\"urcport\"", 2000));
  Serial.println("[SMS] CNMI:    " + modemBase.sendCommand("AT+CNMI?",  2000));
  Serial.println("[SMS] === End ===\n");
}

void ModemSMS::scanForNewMessages() {
  if (!smsReady) { Serial.println("[SMS] Cannot scan — not ready"); return; }
  Serial.println("[SMS] === Scanning for messages ===");
  Serial.println("[SMS] Storage: " + modemBase.sendCommand("AT+CPMS?", 2000));

  String listCmd = modemBase.sendCommand("AT+CMGL=\"REC UNREAD\"", 5000);
  if (listCmd.indexOf("+CMGL:") < 0) {
    listCmd = modemBase.sendCommand("AT+CMGL=\"ALL\"", 5000);
  }

  if (listCmd.indexOf("+CMGL:") >= 0) {
    int startPos = 0, found = 0;
    while (true) {
      int cmglPos = listCmd.indexOf("+CMGL: ", startPos);
      if (cmglPos < 0) break;
      int commaPos = listCmd.indexOf(",", cmglPos);
      if (commaPos > cmglPos) {
        String indexStr = listCmd.substring(cmglPos + 7, commaPos);
        indexStr.trim();
        int index = indexStr.toInt();
        if (index >= 0) { found++; handleNewMessageURC(index); }
      }
      startPos = cmglPos + 7;
    }
    Serial.println("[SMS] Found: " + String(found));
  } else {
    Serial.println("[SMS] No messages found");
  }
  Serial.println("[SMS] === End scan ===");
}

bool ModemSMS::isNetworkProviderMessage(const String &sender) {
  if (sender.length() == 0) return true;
  if (sender.charAt(0) != '+') return true;
  if (sender.length() < 9) return true;
  String u = sender;
  u.toUpperCase();
  if (u.indexOf("AIRTEL") >= 0 || u.indexOf("VODAFONE") >= 0 ||
      u.indexOf("JIO") >= 0    || u.indexOf("BSNL") >= 0 ||
      u.indexOf("VI") >= 0     || u.indexOf("IDEA") >= 0) return true;
  return false;
}

std::vector<SMSMessage> ModemSMS::processIncomingMessages(const String &adminPhone) {
  std::vector<SMSMessage> commandMessages;
  if (!smsReady) return commandMessages;
  if (!checkNewMessages()) return commandMessages;

  std::vector<int> indices = getUnreadIndices();
  Serial.println("[SMS] Processing " + String(indices.size()) + " message(s)");

  for (int index : indices) {
    SMSMessage msg;
    if (readSMS(index, msg)) {
      Serial.println("[SMS] From: " + msg.sender + " | " + msg.message);
      if (isNetworkProviderMessage(msg.sender)) {
        Serial.println("[SMS] → Network provider message — forwarding to admin");
        String fwd = "Network Msg from " + msg.sender + ":\n" + msg.message;
        if (adminPhone.length() > 0) sendSMS(adminPhone, fwd);
        deleteSMS(msg.index);
      } else {
        commandMessages.push_back(msg);
        Serial.println("[SMS] → User command queued");
      }
    } else {
      Serial.println("[SMS] ⚠ Unreadable message at " + String(index) + " — deleting");
      deleteSMS(index);
    }
  }
  return commandMessages;
}

bool ModemSMS::shouldSendAlert(const String &alertKey) {
  if (alertKey.length() == 0) return true;
  unsigned long now = millis();
  if (lastAlertTime.find(alertKey) == lastAlertTime.end()) {
    lastAlertTime[alertKey] = now;
    return true;
  }
  if (now - lastAlertTime[alertKey] >= SMS_ALERT_RATE_LIMIT_MS) {
    lastAlertTime[alertKey] = now;
    return true;
  }
  Serial.println("[SMS] Rate-limited: " + alertKey);
  return false;
}

bool ModemSMS::sendNotification(const String &message, const String &alertKey) {
#if ENABLE_SMS_ALERTS
  if (!isReady()) { Serial.println("[SMS] ⚠ Not ready for notification"); return false; }
  if (!shouldSendAlert(alertKey)) return false;

  bool sent = false;
#ifdef SMS_ALERT_PHONE_1
  if (String(SMS_ALERT_PHONE_1).length() > 0 && sendSMS(SMS_ALERT_PHONE_1, message)) sent = true;
#endif
#ifdef SMS_ALERT_PHONE_2
  if (String(SMS_ALERT_PHONE_2).length() > 0 && sendSMS(SMS_ALERT_PHONE_2, message)) sent = true;
#endif
  return sent;
#else
  return false;
#endif
}

bool ModemSMS::sendNotificationToPhones(const String &message,
                                        const std::vector<String> &phoneNumbers,
                                        const String &alertKey) {
  if (!isReady()) return false;
  if (!shouldSendAlert(alertKey)) return false;
  bool sent = false;
  for (const String &phone : phoneNumbers) {
    if (phone.length() > 0 && sendSMS(phone, message)) sent = true;
  }
  return sent;
}


