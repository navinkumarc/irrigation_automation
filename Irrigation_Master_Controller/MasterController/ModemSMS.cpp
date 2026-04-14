// ModemSMS.cpp - SMS communication for Quectel EC200U
// Depends on ModemBase for AT command access.
// Requests MODEM_MODE_SMS from modemBase before use.
//
// Config.h MUST be included first so ENABLE_SMS is defined before
// ModemSMS.h evaluates its #if ENABLE_SMS guard.
#include "Config.h"
#include "CommConfig.h"
#include "ModemSMS.h"
#include "ModemBase.h"

#if ENABLE_SMS  // entire implementation compiled only when SMS is enabled


ModemSMS modemSMS;

ModemSMS::ModemSMS() : smsReady(false), needsReconfigure(false), reconfigureAfter(0),
                        lastSMSCheck(0), smsCheckInterval(10000),
                        onReadyCallback(nullptr) {
  pendingMessageIndices.clear();
}

// ─── configure() ─────────────────────────────────────────────────────────────
bool ModemSMS::configure() {
  // Request exclusive SMS mode from ModemBase
  if (!modemBase.requestMode(MODEM_MODE_SMS)) {
    // requestMode() already printed the specific failure reason
    Serial.println("[SMS] ❌ Cannot acquire SMS mode — see Modem error above");
    return false;
  }

  if (!modemBase.isReady()) {
    Serial.println("[SMS] Modem not ready — verifying communication...");
    if (modemBase.sendCommand("AT", 2000).indexOf("OK") >= 0) {
      modemBase.setReady(true);
      Serial.println("[SMS] ✓ Modem responding");
      delay(2000);
    } else {
      Serial.println("[SMS] ❌ Modem not responding");
      modemBase.releaseMode(MODEM_MODE_SMS);
      return false;
    }
  }

  Serial.println("[SMS] Configuring SMS subsystem...");

  // Route URCs to UART1 so +CMTI arrives on ESP32 serial
  modemBase.sendCommand("AT+QURCCFG=\"urcport\",\"uart1\"", 2000);
  Serial.println("[SMS] ✓ URCs routed to UART1");

  modemBase.sendCommand("AT+QCFG=\"urc/ri/smsincoming\",\"pulse\",120", 2000);
  Serial.println("[SMS] ✓ SMS RI configured");

  if (!configureTextMode()) {
    modemBase.releaseMode(MODEM_MODE_SMS);
    return false;
  }

  // Prefer SIM storage
  String resp = modemBase.sendCommand("AT+CPMS=\"SM\",\"SM\",\"SM\"", 2000);
  if (resp.indexOf("OK") < 0) {
    Serial.println("[SMS] ⚠ Trying ME storage");
    modemBase.sendCommand("AT+CPMS=\"ME\",\"ME\",\"ME\"", 2000);
  }

  // Enable new-SMS notifications
  String cnmiResp = modemBase.sendCommand("AT+CNMI=2,1,0,0,0", 2000);
  if (cnmiResp.indexOf("OK") >= 0) Serial.println("[SMS] ✓ SMS notifications enabled");
  else                              Serial.println("[SMS] ❌ Failed to enable SMS notifications: " + cnmiResp);

  Serial.println("[SMS] CNMI: " + modemBase.sendCommand("AT+CNMI?", 2000));

  modemBase.sendCommand("AT+CSCS=\"GSM\"", 2000);

  // Check SMSC
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
  if (onReadyCallback) onReadyCallback();  // notify CommManager — sends confirmation SMS

  // Clean up read messages
  String delResp = modemBase.sendCommand("AT+CMGD=1,1", 3000);
  Serial.println("[SMS] Cleanup: " + (delResp.indexOf("OK") >= 0 ? String("✓ done") : delResp));

  Serial.println("[SMS] Storage: " + modemBase.sendCommand("AT+CPMS?", 2000));
  Serial.println("[SMS] Existing messages: " + modemBase.sendCommand("AT+CMGL=\"ALL\"", 5000));

  return true;
}

// ─── release() ───────────────────────────────────────────────────────────────
void ModemSMS::release() {
  smsReady = false;
  modemBase.releaseMode(MODEM_MODE_SMS);
  Serial.println("[SMS] ✓ SMS mode released");
}

// ─── configureTextMode() ─────────────────────────────────────────────────────
bool ModemSMS::configureTextMode() {
  String resp = modemBase.sendCommand("AT+CMGF=1", 2000);
  if (resp.indexOf("OK") >= 0) {
    Serial.println("[SMS] ✓ Text mode enabled");
    return true;
  }
  Serial.println("[SMS] ❌ Failed to set text mode");
  return false;
}

// ─── isValidPhoneNumber() ────────────────────────────────────────────────────
bool ModemSMS::isValidPhoneNumber(const String &phoneNumber) {
  if (phoneNumber.length() < 7 || phoneNumber.charAt(0) != '+') return false;
  int digitCount = 0;
  for (unsigned int i = 1; i < phoneNumber.length(); i++) {
    if (isdigit(phoneNumber.charAt(i)))                           digitCount++;
    else if (phoneNumber.charAt(i) != ' ' && phoneNumber.charAt(i) != '-') return false;
  }
  if (digitCount < 7 || digitCount > 15) return false;
  if (phoneNumber.startsWith("+0000") || phoneNumber.startsWith("+0987")) return false;
  return true;
}

// ─── sendSMS() ───────────────────────────────────────────────────────────────
bool ModemSMS::sendSMS(const String &phoneNumber, const String &message) {
  if (!smsReady) { Serial.println("[SMS] ❌ Not ready");             return false; }
  if (!modemBase.isInMode(MODEM_MODE_SMS)) {
    Serial.println("[SMS] ❌ Modem not in SMS mode"); return false;
  }
  if (!isValidPhoneNumber(phoneNumber)) {
    Serial.println("[SMS] ❌ Invalid number: " + phoneNumber); return false;
  }

  Serial.println("[SMS] Sending to: " + phoneNumber + " | " + message);

  modemBase.clearSerialBuffer();
  String cmd = "AT+CMGS=\"" + phoneNumber + "\"";
  SerialAT.println(cmd);
  Serial.println("[SMS] TX: " + cmd);

  if (!waitForPrompt('>', 5000)) { Serial.println("[SMS] ❌ No '>' prompt"); return false; }

  SerialAT.print(message);
  delay(100);
  SerialAT.write(0x1A); // Ctrl+Z
  Serial.println("[SMS] Waiting for send confirmation...");

  unsigned long start = millis();
  String response;
  bool success = false, errorDetected = false;

  while (millis() - start < 30000) {
    if (SerialAT.available()) {
      char c = SerialAT.read();
      response += c;
      if (response.indexOf("+CMGS:") >= 0 && response.indexOf("OK") >= 0) { success = true; break; }
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
  if (success) { Serial.println("[SMS] ✓ Sent successfully"); return true; }

  Serial.println("[SMS] ❌ Send failed");
  if (errorDetected) {
    int cmsPos = response.indexOf("+CMS ERROR:");
    if (cmsPos >= 0) {
      int codeStart = cmsPos + 12, codeEnd = response.indexOf("\n", codeStart);
      if (codeEnd < 0) codeEnd = response.length();
      String ec = response.substring(codeStart, codeEnd); ec.trim();
      Serial.println("[SMS] CMS Error: " + ec);
      int code = ec.toInt();
      switch (code) {
        case 310: Serial.println("[SMS] SIM not inserted");   break;
        case 311: Serial.println("[SMS] SIM PIN required");   break;
        case 320: Serial.println("[SMS] Memory failure");     break;
        case 322: Serial.println("[SMS] Memory full");        break;
        case 330: Serial.println("[SMS] SMSC unknown");       break;
        case 331: Serial.println("[SMS] No network service"); break;
        case 332: Serial.println("[SMS] Network timeout");    break;
        case 500: Serial.println("[SMS] Unknown error");      break;
        case 521: Serial.println("[SMS] No SMSC address");    break;
        case 530: Serial.println("[SMS] Invalid destination");break;
        default:  Serial.println("[SMS] Error code: " + ec);  break;
      }
      if (code == 330 || code == 521) Serial.println("[SMS] ℹ Set SMSC: AT+CSCA=\"+number\"");
      if (code == 331)                Serial.println("[SMS] ℹ Check: AT+CREG?");
    }
  }
  return false;
}

// ─── waitForPrompt() ─────────────────────────────────────────────────────────
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

// ─── URC / inbox management ───────────────────────────────────────────────────
void ModemSMS::handleNewMessageURC(int index) {
  for (int idx : pendingMessageIndices) if (idx == index) return;
  pendingMessageIndices.push_back(index);
  Serial.println("[SMS] 📨 Message queued at index " + String(index));
}

bool ModemSMS::checkNewMessages()          { return !pendingMessageIndices.empty(); }
int  ModemSMS::getUnreadCount()            { return pendingMessageIndices.size(); }
std::vector<int> ModemSMS::getUnreadIndices() {
  std::vector<int> idx = pendingMessageIndices;
  pendingMessageIndices.clear();
  return idx;
}

bool ModemSMS::readSMS(int index, SMSMessage &sms) {
  if (!smsReady) return false;
  Serial.println("[SMS] Reading index: " + String(index));
  String sender, timestamp;
  String message = readSMSByIndex(index, sender, timestamp);
  if (message.length() > 0) {
    sms.index = index; sms.sender = sender; sms.timestamp = timestamp; sms.message = message;
    Serial.println("[SMS] From: " + sender + " | " + message);
    return true;
  }
  return false;
}

String ModemSMS::readSMSByIndex(int index, String &sender, String &timestamp) {
  String cmd  = "AT+CMGR=" + String(index);
  String resp = modemBase.sendCommand(cmd, 3000);
  int cmgrPos = resp.indexOf("+CMGR:");
  if (cmgrPos < 0) { Serial.println("[SMS] ❌ Failed to read SMS"); return ""; }

  // Detect PDU mode
  int q1  = resp.indexOf("\"", cmgrPos);
  int nl  = resp.indexOf("\n",  cmgrPos);
  if (nl > 0 && (q1 < 0 || nl < q1)) {
    Serial.println("[SMS] ⚠ PDU mode detected — reconfiguring text mode...");
    smsReady = false;
    if (configureTextMode()) {
      smsReady = true;
      resp = modemBase.sendCommand(cmd, 3000);
      cmgrPos = resp.indexOf("+CMGR:");
      if (cmgrPos < 0) { Serial.println("[SMS] ❌ Still failed after reconfig"); return ""; }
    } else { Serial.println("[SMS] ❌ Text mode reconfig failed"); return ""; }
  }

  // Parse sender (3rd quoted field)
  int qA = resp.indexOf("\"", cmgrPos + 7), qB = resp.indexOf("\"", qA + 1);
  int qC = resp.indexOf("\"", qB + 1),       qD = resp.indexOf("\"", qC + 1);
  if (qA >= 0 && qD >= 0) sender = resp.substring(qC + 1, qD);

  // Parse timestamp (5th quoted field)
  int qE = resp.indexOf("\"", qD + 1), qF = resp.indexOf("\"", qE + 1);
  if (qE >= 0 && qF >= 0) timestamp = resp.substring(qE + 1, qF);

  // Message body
  int msgStart = resp.indexOf("\n", cmgrPos);
  if (msgStart >= 0) {
    msgStart++;
    int msgEnd = resp.indexOf("\n\nOK", msgStart);
    if (msgEnd < 0) msgEnd = resp.indexOf("\nOK", msgStart);
    if (msgEnd >= 0) { String m = resp.substring(msgStart, msgEnd); m.trim(); return m; }
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

bool ModemSMS::isReady() { return smsReady; }

// ─── processBackground() ─────────────────────────────────────────────────────
void ModemSMS::processBackground() {
  // Auto-reconfigure after modem restart (RDY URC)
  if (needsReconfigure && millis() >= reconfigureAfter) {
    Serial.println("[SMS] Auto-reconfiguring after modem restart...");
    needsReconfigure = false;
    if (configure()) {
      Serial.println("[SMS] ✓ Reconfigured successfully");
      if (onReadyCallback) onReadyCallback();
    } else {
      Serial.println("[SMS] ❌ Reconfigure failed — will retry in 10s");
      needsReconfigure = true;
      reconfigureAfter = millis() + 10000;
    }
    return;
  }

  // Only process if we hold the SMS mode
  if (!modemBase.isInMode(MODEM_MODE_SMS)) return;

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
      Serial.println("[SMS] URC: '" + line + "'");
      if (line.startsWith("+") || line.indexOf("RDY") >= 0 ||
          line.indexOf("POWERED DOWN") >= 0 || line.indexOf("QIND") >= 0)
        processURC(line);
    }
  }
  if (linesRead > 0) Serial.println("[SMS] Read " + String(linesRead) + " line(s)");
}

void ModemSMS::processURC(const String &urc) {
  if (urc.indexOf("RDY") >= 0) {
    // RDY = modem boot-complete URC. The modem is NOW ready for AT commands.
    // Release the mode lock (it was held by SMS before the restart),
    // mark modem as ready, and schedule SMS reconfiguration.
    Serial.println("[SMS] Modem boot complete (RDY) — will reconfigure SMS in 3s");
    smsReady = false;
    modemBase.releaseMode(MODEM_MODE_SMS);  // reset mode lock to NONE
    modemBase.setReady(true);               // modem IS ready — RDY says so
    needsReconfigure = true;
    reconfigureAfter = millis() + 3000;     // short settle before AT commands
    return;
  }
  if (urc.indexOf("POWERED DOWN") >= 0) {
    Serial.println("[SMS] ⚠ Modem powered down — waiting for restart");
    smsReady = false;
    modemBase.releaseMode(MODEM_MODE_SMS);
    modemBase.setReady(false);              // not ready until next RDY
    needsReconfigure = true;
    reconfigureAfter = millis() + 8000;     // wait for full power cycle + RDY
    return;
  }
  if (urc.indexOf("+CMTI:") >= 0) {
    Serial.println("[SMS] 📨 New SMS URC");
    int p = urc.lastIndexOf(",");
    if (p >= 0) {
      String s = urc.substring(p + 1); s.trim();
      int idx = s.toInt();
      if (idx >= 0) handleNewMessageURC(idx);
    }
  }
  if (urc.indexOf("+CDS:")  >= 0) Serial.println("[SMS] 📬 Delivery report");
  if (urc.indexOf("+CMGS:") >= 0) Serial.println("[SMS] ✓ Send acknowledged");
}

// ─── Diagnostics ─────────────────────────────────────────────────────────────
void ModemSMS::printSMSDiagnostics() {
  Serial.println("\n[SMS] === Diagnostics ===");
  Serial.println("[SMS] Ready:   " + String(smsReady ? "Yes" : "No"));
  Serial.println("[SMS] Mode:    " + String(modemBase.isInMode(MODEM_MODE_SMS) ? "SMS (active)" : "Not held"));
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
  Serial.println("[SMS] === Scanning ===");
  Serial.println("[SMS] Storage: " + modemBase.sendCommand("AT+CPMS?", 2000));
  String listCmd = modemBase.sendCommand("AT+CMGL=\"REC UNREAD\"", 5000);
  if (listCmd.indexOf("+CMGL:") < 0)
    listCmd = modemBase.sendCommand("AT+CMGL=\"ALL\"", 5000);
  if (listCmd.indexOf("+CMGL:") >= 0) {
    int spos = 0, found = 0;
    while (true) {
      int p = listCmd.indexOf("+CMGL: ", spos);
      if (p < 0) break;
      int c = listCmd.indexOf(",", p);
      if (c > p) {
        String s = listCmd.substring(p + 7, c); s.trim();
        int idx = s.toInt();
        if (idx >= 0) { found++; handleNewMessageURC(idx); }
      }
      spos = p + 7;
    }
    Serial.println("[SMS] Found: " + String(found));
  } else {
    Serial.println("[SMS] No messages found");
  }
  Serial.println("[SMS] === End ===");
}

bool ModemSMS::isNetworkProviderMessage(const String &sender) {
  if (sender.length() == 0 || sender.charAt(0) != '+' || sender.length() < 9) return true;
  String u = sender; u.toUpperCase();
  return (u.indexOf("AIRTEL") >= 0 || u.indexOf("VODAFONE") >= 0 ||
          u.indexOf("JIO") >= 0    || u.indexOf("BSNL") >= 0     ||
          u.indexOf("VI") >= 0     || u.indexOf("IDEA") >= 0);
}

std::vector<SMSMessage> ModemSMS::processIncomingMessages(const String &adminPhone) {
  std::vector<SMSMessage> commandMessages;
  if (!smsReady || !checkNewMessages()) return commandMessages;
  std::vector<int> indices = getUnreadIndices();
  Serial.println("[SMS] Processing " + String(indices.size()) + " message(s)");
  for (int index : indices) {
    SMSMessage msg;
    if (readSMS(index, msg)) {
      if (isNetworkProviderMessage(msg.sender)) {
        if (adminPhone.length()) sendSMS(adminPhone, "Network Msg from " + msg.sender + ":\n" + msg.message);
        deleteSMS(msg.index);
      } else {
        commandMessages.push_back(msg);
      }
    } else {
      deleteSMS(index);
    }
  }
  return commandMessages;
}

bool ModemSMS::shouldSendAlert(const String &alertKey) {
  if (alertKey.length() == 0) return true;
  unsigned long now = millis();
  if (lastAlertTime.find(alertKey) == lastAlertTime.end()) { lastAlertTime[alertKey] = now; return true; }
  if (now - lastAlertTime[alertKey] >= SMS_ALERT_RATE_LIMIT_MS) { lastAlertTime[alertKey] = now; return true; }
  Serial.println("[SMS] Rate-limited: " + alertKey);
  return false;
}

bool ModemSMS::sendNotification(const String &message, const String &alertKey) {
#if ENABLE_SMS_ALERTS
  if (!isReady() || !shouldSendAlert(alertKey)) return false;
  bool sent = false;
  // Use runtime config phones (commCfg) rather than compile-time macros
  if (commCfg.smsPhone1.length() > 0) sent |= sendSMS(commCfg.smsPhone1, message);
  if (commCfg.smsPhone2.length() > 0) sent |= sendSMS(commCfg.smsPhone2, message);
  return sent;
#else
  return false;
#endif
}

bool ModemSMS::sendNotificationToPhones(const String &message,
                                        const std::vector<String> &phoneNumbers,
                                        const String &alertKey) {
  if (!isReady() || !shouldSendAlert(alertKey)) return false;
  bool sent = false;
  for (const String &phone : phoneNumbers)
    if (phone.length() && sendSMS(phone, message)) sent = true;
  return sent;
}

#endif // ENABLE_SMS
