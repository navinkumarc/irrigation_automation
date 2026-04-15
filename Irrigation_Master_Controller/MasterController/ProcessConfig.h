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

// ─── Irrigation group config ──────────────────────────────────────────────────
struct IrrGroupConfig {
  String  id;          // Group ID e.g. "IG1"
  String  pumpId;      // "G1" or "G2"
  uint8_t minValves  = 1;
  uint8_t maxNodes   = 15;
  uint8_t maxValves  = 4;
  bool    configured = false;

  bool isValid() const {
    return id.length() > 0 && pumpId.length() > 0;
  }
};

// ── Limits ────────────────────────────────────────────────────────────────────
#define MAX_WTT_GROUPS   2   // Max WaterToTank groups (W1/W2 pumps + T1/T2 tanks)
#define MAX_IRR_GROUPS   2   // Max irrigation groups (G1/G2 pumps)

#endif // PROCESS_CONFIG_H
