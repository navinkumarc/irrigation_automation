// NodeCommunication.cpp  —  All master ↔ node communication
// Zero direct transport knowledge — all channels via INodeTransport adapters.
#include "NodeCommunication.h"

// ─── registerTransport() ──────────────────────────────────────────────────────
void NodeCommunication::registerTransport(INodeTransport *t) {
  if (!t) return;
  transports.push_back(t);
  Serial.println("[NodeComm] Registered transport: " + String(t->transportName()));
}

// ─── begin() ──────────────────────────────────────────────────────────────────
bool NodeCommunication::begin() {
  if (transports.empty()) {
    Serial.println("[NodeComm] ❌ No transports registered");
    initialized = false;
    return false;
  }

  // Wire inbound callback into every transport
  for (auto *t : transports) {
    t->setInboundCallback([this](const String &raw, const char *src) {
      onRawMessage(raw, src);
    });
    Serial.printf("[NodeComm]  ✓ %s inbound wired\n", t->transportName());
  }

  initialized = true;
  Serial.printf("[NodeComm] ✓ Ready — %d transport(s) registered\n",
                (int)transports.size());
  return true;
}

// ─── Outbound helpers ─────────────────────────────────────────────────────────

bool NodeCommunication::sendValveOpen(int nodeId, const String &schedId,
                                      int seqIdx, uint32_t durationMs) {
  return sendCommand(CMD_OPEN, nodeId, schedId, seqIdx, durationMs);
}

bool NodeCommunication::sendValveClose(int nodeId, const String &schedId, int seqIdx) {
  return sendCommand(CMD_CLOSE, nodeId, schedId, seqIdx, 0);
}

bool NodeCommunication::sendPing(int nodeId) {
  return sendCommand(CMD_PING, nodeId, "", 0, 0);
}

bool NodeCommunication::requestStatus(int nodeId) {
  return sendCommand(CMD_STATUS, nodeId, "", 0, 0);
}

// ─── sendCommand() — tries transports in registration order ──────────────────
bool NodeCommunication::sendCommand(const String &cmdType, int nodeId,
                                    const String &schedId, int seqIdx,
                                    uint32_t durationMs) {
  if (!initialized) {
    Serial.println("[NodeComm] ❌ Not initialized");
    return false;
  }
  if (nodeId < 1 || nodeId > 255) {
    Serial.println("[NodeComm] ❌ Invalid node ID: " + String(nodeId));
    return false;
  }

  for (auto *t : transports) {
    if (!t->isAvailable()) {
      Serial.printf("[NodeComm]  skip %s (unavailable)\n", t->transportName());
      continue;
    }
    Serial.printf("[NodeComm] → %s | %s | node %d\n",
                  t->transportName(), cmdType.c_str(), nodeId);
    if (t->sendCommand(cmdType, nodeId, schedId, seqIdx, durationMs)) {
      Serial.printf("[NodeComm] ✓ Delivered via %s\n", t->transportName());
      return true;
    }
    Serial.printf("[NodeComm] ✗ %s failed — trying next\n", t->transportName());
  }

  Serial.println("[NodeComm] ❌ All transports failed for node " + String(nodeId));
  return false;
}

// ─── process() ────────────────────────────────────────────────────────────────
void NodeCommunication::process() {
  if (!initialized) return;
  for (auto *t : transports) {
    t->pollIncoming();
  }
}

// ─── onRawMessage() — inbound callback fired by transport adapters ─────────────
void NodeCommunication::onRawMessage(const String &raw, const char *transport) {
  Serial.printf("[NodeComm] ← %s | %s\n", transport, raw.substring(0, 60).c_str());
  NodeMessage nm = parseMessage(raw, transport);
  if (nm.type == NodeMessageType::UNKNOWN) {
    Serial.println("[NodeComm] ⚠ Unknown message type — ignored");
    return;
  }
  if (messageCallback) {
    messageCallback(nm);
  } else {
    Serial.println("[NodeComm] ⚠ No message callback set");
  }
}

// ─── parseMessage() ───────────────────────────────────────────────────────────
NodeMessage NodeCommunication::parseMessage(const String &raw,
                                            const char *transport) const {
  NodeMessage nm;
  nm.rawMessage = raw;
  nm.transport  = String(transport);

  if (raw.startsWith(MSG_STAT_PREFIX MSG_SEP)) {
    nm.type = NodeMessageType::TELEMETRY;

    String n = MsgFmt::extractField(raw, KEY_NODE_ID);
    if (n.length()) nm.nodeId = n.toInt();

    String b = MsgFmt::extractField(raw, KEY_BATTERY);
    if (b.length()) nm.batteryPercent = b.toInt();

    String bv = MsgFmt::extractField(raw, KEY_BATT_V);
    if (bv.length()) nm.batteryVoltage = bv.toFloat();

    String sv = MsgFmt::extractField(raw, KEY_SOLAR_V);
    if (sv.length()) nm.solarVoltage = sv.toFloat();

    nm.valveStates    = MsgFmt::extractField(raw, KEY_VALVES);
    nm.moistureLevels = MsgFmt::extractField(raw, KEY_MOISTURE);

  } else if (raw.startsWith(MSG_AUTO_CLOSE_PREFIX MSG_SEP)) {
    nm.type = NodeMessageType::AUTO_CLOSE;

    String n = MsgFmt::extractField(raw, KEY_NODE_ID);
    if (n.length()) nm.nodeId = n.toInt();

    String r = MsgFmt::extractField(raw, KEY_REASON);
    nm.reason = r.length() ? r : "Auto-close triggered";

  } else {
    nm.type = NodeMessageType::UNKNOWN;
  }

  return nm;
}

// ─── printStatus() ────────────────────────────────────────────────────────────
void NodeCommunication::printStatus() const {
  Serial.println("[NodeComm] ===== NodeCommunication Status =====");
  Serial.printf ("[NodeComm]  Initialized : %s\n", initialized ? "YES" : "NO");
  Serial.printf ("[NodeComm]  Transports  : %d registered\n", (int)transports.size());
  for (auto *t : transports) {
    Serial.printf("[NodeComm]    %-8s : %s\n",
                  t->transportName(), t->isAvailable() ? "available" : "unavailable");
  }
  Serial.println("[NodeComm] ========================================");
}

int NodeCommunication::availableTransportCount() const {
  int n = 0;
  for (auto *t : transports) if (t->isAvailable()) n++;
  return n;
}
