// ProcessConfig.h  —  Persistent configuration for process groups
//
// Stores the setup configuration for WaterToTankController (WTT) groups
// and IrrigationGroup (IRR) groups in LittleFS.
//
// Setup is performed via Serial only (SerialConfigHandler).
// Once configured, groups are available on all channels for runtime ops.
// Group IDs are fixed after setup — cannot be changed via remote channels.
//
// ── Storage paths ──────────────────────────────────────────────────────────
//   /process/wtt_<id>.json   — WTT group config
//   /process/irr_<id>.json   — Irrigation group config
//
// ── WTT group config fields ────────────────────────────────────────────────
//   id       : user-defined (e.g. "FG1", "FG2") — immutable after setup
//   pumpId   : "W1" or "W2"
//   tankId   : "T1" or "T2"
//
// ── IRR group config fields ────────────────────────────────────────────────
//   id       : user-defined (e.g. "IG1", "IG2") — immutable after setup
//   pumpId   : "G1" or "G2"
//   minValves: minimum open valves required to run pump (default 1)
//   maxNodes : max nodes in this group (default 15)

#ifndef PROCESS_CONFIG_H
#define PROCESS_CONFIG_H

#include <Arduino.h>
#include "Config.h"

// ─── WTT group config ─────────────────────────────────────────────────────────
struct WTTGroupConfig {
  String id;          // Group ID e.g. "FG1"
  String pumpId;      // "W1" or "W2"
  String tankId;      // "T1" or "T2"
  bool   configured = false;

  bool isValid() const {
    return id.length() > 0 && pumpId.length() > 0 && tankId.length() > 0;
  }
};

// ─── Node valve entry — one node's valve membership in an irrigation group ────
struct IrrNodeEntry {
  uint8_t nodeId   = 0;
  uint8_t valves[4]= {0,0,0,0};  // valve IDs present in this group (0 = unused slot)
  uint8_t valveCount = 0;

  void addValve(uint8_t v) {
    if (valveCount < 4) valves[valveCount++] = v;
  }
  bool hasValve(uint8_t v) const {
    for (int i = 0; i < valveCount; i++) if (valves[i] == v) return true;
    return false;
  }
};

// ─── Irrigation group config ──────────────────────────────────────────────────
//
// An irrigation group defines:
//   • Which pump (G1/G2)
//   • Which nodes and which valves on each node participate
//   • Minimum open valves required to run the pump
//
// Node membership is stored separately in /process/irr_<id>_nodes.json.
// The IrrGroupConfig holds a runtime-loaded copy in nodes[].
//
// Setup sequence (Serial only):
//   SETUP NODE IG1,N:1,V:2,3        add node 1, valves 2 and 3 to group IG1
//   SETUP NODE IG1,N:2,V:4          add node 2, valve 4
//   SETUP NODE IG1,N:15,V:2,3       add node 15, valves 2 and 3
//   SETUP IRR ID:IG1,G:G1,M:1       create group (references above nodes)
//
// To remove a node:  SETUP NODE DEL IG1,N:1
// To view:          SETUP SHOW

#define MAX_NODES_PER_GROUP  15
#define MAX_VALVES_PER_NODE   4

struct IrrGroupConfig {
  String       id;                              // Group ID e.g. "IG1"
  String       pumpId;                          // "G1" or "G2"
  uint8_t      minValves  = 1;                  // Min open valves for pump
  uint8_t      nodeCount  = 0;
  IrrNodeEntry nodes[MAX_NODES_PER_GROUP];       // Loaded at runtime
  bool         configured = false;

  bool isValid() const {
    return id.length() > 0 && pumpId.length() > 0;
  }

  IrrNodeEntry* findNode(uint8_t nodeId) {
    for (int i = 0; i < nodeCount; i++)
      if (nodes[i].nodeId == nodeId) return &nodes[i];
    return nullptr;
  }

  // Build a flat SeqStep list with given duration per valve (for schedule use)
  // Returns step count; caller provides buffer
  int buildSteps(SeqStep *out, int maxSteps, uint32_t durationMs) const {
    int n = 0;
    for (int i = 0; i < nodeCount && n < maxSteps; i++) {
      for (int v = 0; v < nodes[i].valveCount && n < maxSteps; v++) {
        out[n].node_id    = nodes[i].nodeId;
        out[n].valve_id   = nodes[i].valves[v];
        out[n].duration_ms= durationMs;
        n++;
      }
    }
    return n;
  }
};

// ── Limits ────────────────────────────────────────────────────────────────────
#define MAX_WTT_GROUPS   2   // Max WaterToTank groups (W1/W2 pumps + T1/T2 tanks)
#define MAX_IRR_GROUPS   2   // Max irrigation groups (G1/G2 pumps)
#define MAX_TOTAL_STEPS  60  // 15 nodes × 4 valves

#endif // PROCESS_CONFIG_H
