// IrrigationSequencer.cpp  —  Irrigation sequence execution engine
#include "IrrigationSequencer.h"
#include "NodeCommunication.h"
#include "IPController.h"
#include "UserCommunication.h"

// ─── init() ──────────────────────────────────────────────────────────────────
void IrrigationSequencer::init(NodeCommunication *nc, IPController *ipc,
                                UserCommunication *uc) {
  nodeComm = nc;
  ipCtrl   = ipc;
  userComm = uc;
  memset(valveOpen, 0, sizeof(valveOpen));
  Serial.printf("[IrrigSeq] Init — maxNodes:%d maxValves:%d minOpen:%d overlap:%dms\n",
                maxNodes, maxValvesPerNode, minOpenValves, (int)overlapMs);
}

// ─── sendAlert() ─────────────────────────────────────────────────────────────
void IrrigationSequencer::sendAlert(const String &msg, const String &sev) {
  Serial.println("[IrrigSeq] " + msg);
  if (userComm) userComm->sendAlert(msg, sev);
}

// ─── setState() ──────────────────────────────────────────────────────────────
void IrrigationSequencer::setState(SeqState s) {
  state   = s;
  stateMs = millis();
}

// ─── countOpenValves() ───────────────────────────────────────────────────────
int IrrigationSequencer::countOpenValves() const {
  int n = 0;
  for (int node = 1; node <= maxNodes; node++)
    for (int v = 0; v < maxValvesPerNode; v++)
      if (valveOpen[node][v]) n++;
  return n;
}

// ─── reportValveCount() ──────────────────────────────────────────────────────
void IrrigationSequencer::reportValveCount() {
  if (ipCtrl) ipCtrl->setOpenValveCount(countOpenValves());
}

// ─── validateStep() ──────────────────────────────────────────────────────────
bool IrrigationSequencer::validateStep(const SeqStep &step) const {
  if (step.node_id < 1 || step.node_id > maxNodes) {
    Serial.printf("[IrrigSeq] ❌ Invalid node_id %d (max %d)\n", step.node_id, maxNodes);
    return false;
  }
  if (step.valve_id >= maxValvesPerNode) {
    Serial.printf("[IrrigSeq] ❌ Invalid valve_id %d (max %d)\n", step.valve_id, maxValvesPerNode - 1);
    return false;
  }
  if (step.duration_ms == 0) {
    Serial.printf("[IrrigSeq] ❌ Step duration is 0 for node %d valve %d\n",
                  step.node_id, step.valve_id);
    return false;
  }
  return true;
}

// ─── sendOpen() ──────────────────────────────────────────────────────────────
bool IrrigationSequencer::sendOpen(const SeqStep &step) {
  if (!nodeComm) return false;
  Serial.printf("[IrrigSeq] → OPEN  node:%d valve:%d dur:%lums\n",
                step.node_id, step.valve_id, step.duration_ms);
  bool ok = nodeComm->sendValveOpen(step.node_id, schedId, stepIdx, step.duration_ms);
  if (ok) {
    valveOpen[step.node_id][step.valve_id] = true;
    reportValveCount();
    sendAlert(MsgFmt::alertValveOpen(step.node_id, step.valve_id, step.duration_ms));
  } else {
    sendAlert("[WARNING] Valve open failed — node:" + String(step.node_id)
              + " valve:" + String(step.valve_id), SEV_WARNING);
  }
  return ok;
}

// ─── sendClose() ─────────────────────────────────────────────────────────────
bool IrrigationSequencer::sendClose(const SeqStep &step) {
  if (!nodeComm) return false;
  Serial.printf("[IrrigSeq] → CLOSE node:%d valve:%d\n", step.node_id, step.valve_id);
  bool ok = nodeComm->sendValveClose(step.node_id, schedId, stepIdx);
  if (ok) {
    valveOpen[step.node_id][step.valve_id] = false;
    reportValveCount();
    sendAlert(MsgFmt::alertValveClose(step.node_id, step.valve_id));
  }
  return ok;
}

// ─── start() ─────────────────────────────────────────────────────────────────
bool IrrigationSequencer::start(const std::vector<SeqStep> &steps,
                                 const String &scheduleId) {
  if (isRunning()) {
    sendAlert("Cannot start — sequence already running", SEV_WARNING);
    return false;
  }
  if (steps.empty()) {
    sendAlert("Cannot start — empty sequence", SEV_WARNING);
    return false;
  }

  // Validate all steps upfront
  for (auto &s : steps)
    if (!validateStep(s)) return false;

  seq      = steps;
  schedId  = scheduleId;
  stepIdx  = 0;
  memset(valveOpen, 0, sizeof(valveOpen));
  reportValveCount();

  sendAlert("[INFO] Irrigation sequence starting — " + String(steps.size())
            + " step(s) schedule:" + scheduleId);

  // Open first valve, then wait for pump start delay
  if (!sendOpen(seq[0])) {
    sendAlert("Failed to open first valve — aborting", SEV_ERROR);
    return false;
  }
  setState(SeqState::PUMP_STARTING);
  return true;
}

// ─── stop() — emergency stop ──────────────────────────────────────────────────
void IrrigationSequencer::stop(const String &reason) {
  if (!isRunning()) return;
  sendAlert("[WARNING] Sequence stopped: " + reason, SEV_WARNING);

  // Close all open valves
  for (int node = 1; node <= maxNodes; node++) {
    for (int v = 0; v < maxValvesPerNode; v++) {
      if (valveOpen[node][v]) {
        SeqStep s; s.node_id = node; s.valve_id = v; s.duration_ms = 1;
        sendClose(s);
      }
    }
  }

  // Stop pump immediately (emergency)
  if (ipCtrl) ipCtrl->stop(reason);
  memset(valveOpen, 0, sizeof(valveOpen));
  reportValveCount();
  setState(SeqState::IDLE);
}

// ─── finishSequence() ─────────────────────────────────────────────────────────
void IrrigationSequencer::finishSequence() {
  sendAlert("[INFO] Irrigation sequence complete — schedule:" + schedId);
  setState(SeqState::DONE);
  // IDLE transition handled in process() on next call
}

// ─── onNodeAutoClose() ───────────────────────────────────────────────────────
void IrrigationSequencer::onNodeAutoClose(int nodeId, const String &reason) {
  Serial.printf("[IrrigSeq] AUTO_CLOSE node:%d reason:%s\n", nodeId, reason.c_str());
  // Mark all valves on this node as closed
  for (int v = 0; v < maxValvesPerNode; v++)
    valveOpen[nodeId][v] = false;
  reportValveCount();
  sendAlert(MsgFmt::alertAutoClose(nodeId, reason), SEV_WARNING);
}

// ─── process() — state machine ────────────────────────────────────────────────
void IrrigationSequencer::process() {
  unsigned long now = millis();

  switch (state) {

    case SeqState::IDLE:
    case SeqState::DONE:
      if (state == SeqState::DONE) setState(SeqState::IDLE);
      return;

    // ── First valve is open; wait pumpStartDelayMs then start pump ─────────
    case SeqState::PUMP_STARTING:
      if (now - stateMs >= pumpStartDelayMs) {
        int open = countOpenValves();
        if (open >= minOpenValves) {
          if (ipCtrl && ipCtrl->start("sequence")) {
            sendAlert("[INFO] Pump started — open valves: " + String(open));
            setState(SeqState::RUNNING);
          } else {
            sendAlert("Pump failed to start — aborting sequence", SEV_ERROR);
            stop("pump-start-failed");
          }
        } else {
          sendAlert("Not enough open valves (" + String(open) + "/" + String(minOpenValves)
                    + ") — pump start delayed", SEV_WARNING);
          // Stay in PUMP_STARTING; will retry next loop
          stateMs = now;  // reset delay
        }
      }
      break;

    // ── Pump running; advance sequence steps ──────────────────────────────
    case SeqState::RUNNING: {
      if (stepIdx >= (int)seq.size()) {
        // All steps done — begin pump-stop sequence
        setState(SeqState::PUMP_STOPPING);
        break;
      }

      const SeqStep &cur = seq[stepIdx];

      // Check if current valve duration has elapsed
      // We track duration via valveOpen[node][valve] presence; use stateMs as step start
      if (now - stateMs < cur.duration_ms) break;  // not yet

      // Duration elapsed — is there a next step?
      int nextIdx = stepIdx + 1;
      if (nextIdx < (int)seq.size()) {
        // ── OVERLAP RULE: open next valve BEFORE closing current ────────────
        const SeqStep &nxt = seq[nextIdx];
        Serial.printf("[IrrigSeq] Step %d done → overlap: opening next valve first\n", stepIdx);
        sendOpen(nxt);          // open next valve
        setState(SeqState::OVERLAP);
        // stateMs now = time next valve was opened; overlapMs later we close current
      } else {
        // Last step — go to pump stopping (close last valve after stop delay)
        Serial.printf("[IrrigSeq] Last step %d done — beginning pump stop\n", stepIdx);
        setState(SeqState::PUMP_STOPPING);
      }
      break;
    }

    // ── Overlap window: next valve is open, wait overlapMs then close prev ─
    case SeqState::OVERLAP:
      if (now - stateMs >= overlapMs) {
        // Close the previous valve now (next is already open)
        sendClose(seq[stepIdx]);
        stepIdx++;
        // Step timer starts now
        setState(SeqState::RUNNING);
      }
      break;

    // ── Pump stopping: wait pumpStopDelayMs then close last valve + pump off
    case SeqState::PUMP_STOPPING:
      if (now - stateMs >= pumpStopDelayMs) {
        // Close last open valve(s)
        for (int node = 1; node <= maxNodes; node++)
          for (int v = 0; v < maxValvesPerNode; v++)
            if (valveOpen[node][v]) {
              SeqStep s; s.node_id = node; s.valve_id = v; s.duration_ms = 1;
              sendClose(s);
            }
        // Stop pump
        if (ipCtrl) ipCtrl->stop("sequence-complete");
        memset(valveOpen, 0, sizeof(valveOpen));
        reportValveCount();
        finishSequence();
      }
      break;

    default: break;
  }
}

// ─── statusString() ──────────────────────────────────────────────────────────
String IrrigationSequencer::statusString() const {
  const char *stStr =
    state == SeqState::IDLE          ? "IDLE"         :
    state == SeqState::PUMP_STARTING ? "PUMP_STARTING":
    state == SeqState::RUNNING       ? "RUNNING"      :
    state == SeqState::OVERLAP       ? "OVERLAP"      :
    state == SeqState::PUMP_STOPPING ? "PUMP_STOPPING":
    state == SeqState::DONE          ? "DONE"         : "UNKNOWN";

  return "IrrigSeq:" + String(stStr)
       + " step:" + String(stepIdx) + "/" + String(seq.size())
       + " open:" + String(countOpenValves())
       + "/" + String(minOpenValves)
       + " sched:" + schedId;
}
