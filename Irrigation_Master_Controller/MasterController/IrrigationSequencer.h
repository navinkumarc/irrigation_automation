// IrrigationSequencer.h  —  Irrigation sequence execution engine
//
// Owns the full state machine for executing an irrigation schedule.
// Called every loop() via process(). Drives NodeCommunication for valve
// commands and IPController for pump start/stop.
//
// ── Responsibilities ──────────────────────────────────────────────────────────
//   • Execute a sequence of SeqSteps (node + valve + duration)
//   • Enforce overlap rule: next valve OPEN before current valve CLOSE
//     → guarantees continuous water flow, no pressure drop
//   • Track which valves are open across all nodes/valves
//   • Report open valve count to IPController at every state change
//   • Enforce pump start condition: start pump only after minOpenValves are open
//   • Enforce pump stop condition: keep pump on until last valve is about to close,
//     then close last valve and stop pump atomically (no pressure shock)
//   • Handle AUTO_CLOSE notifications from nodes
//   • Monitor tank empty state — stop immediately if tank runs dry
//     (dry-run protection: fault after dryRunTimeoutMs of empty state)
//
// ── NodeCommunication contract ─────────────────────────────────────────────────
//   NodeCommunication is ONLY used for sendValveOpen() / sendValveClose().
//   Zero control logic lives in NodeCommunication — it is pure transport.
//
// ── State machine ────────────────────────────────────────────────────────────
//   IDLE          → no sequence running
//   OPENING_FIRST → waiting for first valve to open (before pump start)
//   PUMP_STARTING → first valve open, waiting IPC_PUMP_START_DELAY_MS
//   RUNNING       → pump on, cycling through sequence steps
//   OVERLAP       → next valve opened, waiting IPC_VALVE_OVERLAP_MS before closing prev
//   PUMP_STOPPING → last valve closing, pump stops after IPC_PUMP_STOP_DELAY_MS
//   DONE          → sequence complete, ready for IDLE
//
// ── Limits ─────────────────────────────────────────────────────────────────────
//   Nodes:  IPC_MIN_NODES (1) … IPC_MAX_NODES (15)
//   Valves: 1 … IPC_MAX_VALVES_PER_NODE (4) per node

#ifndef IRRIGATION_SEQUENCER_H
#define IRRIGATION_SEQUENCER_H

#include <Arduino.h>
#include <vector>
#include "Config.h"
#include "MessageFormats.h"

// Forward declarations — no direct hardware includes
class NodeCommunication;
class IPController;
class UserCommunication;
class TankManager;

// ─── Valve tracking ───────────────────────────────────────────────────────────
struct ValveState {
  uint8_t  nodeId;
  uint8_t  valveId;
  bool     isOpen       = false;
  uint32_t openedAtMs   = 0;
  uint32_t durationMs   = 0;    // 0 = manual close only
};

// ─── Sequencer state ──────────────────────────────────────────────────────────
enum class SeqState {
  IDLE,
  OPENING_FIRST,   // First valve OPEN sent; waiting for valveCount >= minOpen
  PUMP_STARTING,   // Valve open; waiting IPC_PUMP_START_DELAY_MS before pump on
  RUNNING,         // Pump on; executing steps
  OVERLAP,         // Next valve opened; waiting IPC_VALVE_OVERLAP_MS before prev close
  PUMP_STOPPING,   // All steps done; waiting IPC_PUMP_STOP_DELAY_MS before pump off
  DONE             // Sequence complete
};

class IrrigationSequencer {
  // Dependencies — injected via init(), never hard-coded
  NodeCommunication *nodeComm  = nullptr;
  IPController      *ipCtrl    = nullptr;
  UserCommunication *userComm  = nullptr;
  TankManager       *_tank     = nullptr;   // Optional — supply tank empty check

  // ── Tank dry-run protection ────────────────────────────────────────────
  uint32_t      _dryRunTimeoutMs  = 30000;  // Stop if tank empty this long
  unsigned long _tankEmptySince   = 0;      // millis() when empty first detected
  bool          _tankEmptyActive  = false;  // currently seeing empty condition
  unsigned long _lastTankCheckMs  = 0;
  uint32_t      _tankCheckIntervalMs = 3000;

  // ── Runtime config ────────────────────────────────────────────────────────
  int      minNodes            = IPC_MIN_NODES;
  int      maxNodes            = IPC_MAX_NODES;
  int      maxValvesPerNode    = IPC_MAX_VALVES_PER_NODE;
  int      minOpenValves       = IPC_MIN_OPEN_VALVES;
  uint32_t overlapMs           = IPC_VALVE_OVERLAP_MS;
  uint32_t pumpStartDelayMs    = IPC_PUMP_START_DELAY_MS;
  uint32_t pumpStopDelayMs     = IPC_PUMP_STOP_DELAY_MS;

  // ── Active sequence ───────────────────────────────────────────────────────
  std::vector<SeqStep> seq;
  String               schedId;
  int                  stepIdx     = 0;      // Current step being executed
  SeqState             state       = SeqState::IDLE;
  unsigned long        stateMs     = 0;      // millis() when state was entered

  // ── Valve state table: node × valve → open/closed ─────────────────────────
  // Indexed as nodeId (1-15) × valveId (0-3). Flat array for efficiency.
  bool  valveOpen[IPC_MAX_NODES + 1][IPC_MAX_VALVES_PER_NODE] = {};

  // ── Internal helpers ──────────────────────────────────────────────────────
  void setState         (SeqState s);
  int  countOpenValves  () const;
  void reportValveCount ();
  bool sendOpen         (const SeqStep &step);
  bool sendClose        (const SeqStep &step);
  void sendAlert        (const String &msg, const String &sev = SEV_INFO);
  bool validateStep     (const SeqStep &step) const;
  void finishSequence   ();
  void checkTankDryRun  (unsigned long now);  // tank empty → stop sequence

public:
  IrrigationSequencer() = default;

  // ── Setup ────────────────────────────────────────────────────────────────
  void init(NodeCommunication *nc, IPController *ipc, UserCommunication *uc);

  // ── Runtime config ────────────────────────────────────────────────────────
  void setMinOpenValves   (int n)  { minOpenValves    = max(1, n); }
  void setMaxNodes        (int n)  { maxNodes         = min(n, IPC_MAX_NODES); }
  void setMaxValvesPerNode(int n)  { maxValvesPerNode = min(n, IPC_MAX_VALVES_PER_NODE); }
  void setOverlapMs       (uint32_t ms) { overlapMs         = ms; }
  void setPumpStartDelay  (uint32_t ms) { pumpStartDelayMs  = ms; }
  void setPumpStopDelay   (uint32_t ms) { pumpStopDelayMs   = ms; }
  // Optional: attach a tank so irrigation stops if source runs dry
  void setTank            (TankManager *t)      { _tank             = t; }
  void setDryRunTimeout   (uint32_t ms)         { _dryRunTimeoutMs  = ms; }

  // ── Sequence control ──────────────────────────────────────────────────────
  // Start a new irrigation sequence. Returns false if already running or invalid.
  bool start(const std::vector<SeqStep> &steps, const String &scheduleId);

  // Stop the running sequence immediately (emergency stop).
  void stop(const String &reason = "manual");

  // Is a sequence currently executing?
  bool isRunning() const { return state != SeqState::IDLE && state != SeqState::DONE; }

  // ── Inbound from NodeCommunication ────────────────────────────────────────
  // Call when a node reports AUTO_CLOSE (valve closed itself).
  void onNodeAutoClose(int nodeId, const String &reason);

  // ── Background — call every loop() ────────────────────────────────────────
  void process();

  // ── Status ────────────────────────────────────────────────────────────────
  String statusString() const;
  int    currentStep()  const { return stepIdx; }
  int    totalSteps()   const { return (int)seq.size(); }
  SeqState getState()   const { return state; }
};

#endif // IRRIGATION_SEQUENCER_H
