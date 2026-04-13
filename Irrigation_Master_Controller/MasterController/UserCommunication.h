// UserCommunication.h - User ↔ Controller message exchange
//
// Responsibilities (ONLY these):
//   • Receive inbound ChannelMessages from any channel (SMS / MQTT / BLE / HTTP)
//   • Parse and execute user commands (schedule control, status, diagnostics)
//   • Send outbound alerts / responses / events through registered IChannelAdapters
//   • Maintain system status snapshot for reporting
//
// What UserCommunication does NOT do:
//   • Does not manage channel lifecycle (connect / reconnect / processBackground)
//   • Does not call any module API directly (ModemSMS, MQTTComm, BLEComm, etc.)
//   • Does not include any ENABLE_X guarded headers
//   • Does not control which channel is active
//
// Loose coupling:
//   Inbound:  CommManager channel pollers call onMessageReceived()
//   Outbound: calls IChannelAdapter::send() — knows nothing
//             about the underlying module
//
// Channel adapters are registered via registerAdapter().
// Any number of adapters can be registered; all available ones receive alerts.

#ifndef USER_COMMUNICATION_H
#define USER_COMMUNICATION_H

#include <Arduino.h>
#include <vector>
#include <functional>
#include "Config.h"
#include "IChannelAdapter.h"
#include "ChannelMessage.h"

// Forward declarations only — no module headers included here
struct Schedule;

// ─── Callback type for node commands ─────────────────────────────────────────
// Set by MasterController.ino to route NODE commands to NodeCommunication
using NodeCommandCallback = std::function<bool(int nodeId, const String &command)>;

// ─── Command result ───────────────────────────────────────────────────────────
struct CommandResult {
  bool   success;
  String response;
  String commandType;

  CommandResult() : success(false) {}
  CommandResult(bool ok, const String &type, const String &resp)
    : success(ok), commandType(type), response(resp) {}
};

// ─── System status snapshot ───────────────────────────────────────────────────
// Populated by CommManager::buildSystemStatus() and passed into status methods.
// UserCommunication formats and delivers it — does not gather it itself.
struct SystemStatus {
  bool     scheduleRunning    = false;
  String   currentScheduleId;
  int      enabledSchedules   = 0;
  int      totalSchedules     = 0;

  bool     loraUp             = false;
  bool     bleConnected       = false;
  bool     wifiConnected      = false;
  bool     ppposConnected     = false;
  bool     mqttConnected      = false;
  bool     smsReady           = false;
  bool     httpReady          = false;

  uint32_t freeHeapBytes      = 0;
  uint32_t totalHeapBytes     = 0;
  uint32_t uptimeSeconds      = 0;
  String   networkIP;
};

// ─── UserCommunication ────────────────────────────────────────────────────────

class UserCommunication {
private:
  // Registered outbound adapters — all available ones receive each alert
  std::vector<IChannelAdapter*> adapters;

  // Callback into NodeCommunication for NODE <id> <cmd> commands
  NodeCommandCallback nodeCommandCallback;

  // Admin phone — used by SMS adapter for direct replies
  String adminPhone;

  // ── Command handlers (pure business logic, no channel knowledge) ──────────
  CommandResult handleStatusCommand    (const SystemStatus &sys);
  CommandResult handleDiagnosticsCommand();
  CommandResult handleSchedulesCommand (const SystemStatus &sys);
  CommandResult handleStopCommand      (bool *scheduleRunning, bool *scheduleLoaded);
  CommandResult handleStartCommand     (const String &schedId);
  CommandResult handleCheckCommand     ();
  CommandResult handleNodeCommand      (const String &args);
  CommandResult handleHelpCommand      ();
  CommandResult handleStatsCommand     ();

  CommandResult dispatchCommand(const String &cmd,
                                bool *scheduleRunning,
                                bool *scheduleLoaded,
                                const SystemStatus &sys);

  // ── Outbound helpers ──────────────────────────────────────────────────────
  // Send to all available adapters
  void broadcast(const String &message);

  // Send to all adapters except one (used when replying on a specific channel)
  void broadcastExcept(const String &message, const char *excludeChannel);

  // Format helpers
  String formatStatusText (const SystemStatus &sys) const;
  String formatStatusBrief(const SystemStatus &sys) const;
  String formatStatusJSON (const SystemStatus &sys) const;

public:
  UserCommunication();

  // ── Setup ──────────────────────────────────────────────────────────────────

  void init(const String &adminPhoneNumber);

  // Register an outbound channel adapter.
  // Called by CommManager during initUserCommunication().
  // adapter must remain valid for the lifetime of UserCommunication.
  void registerAdapter(IChannelAdapter *adapter);

  // Register the callback used to execute NODE <id> <cmd> commands.
  void setNodeCommandCallback(NodeCommandCallback cb);

  // ── Inbound — called by CommManager channel pollers ──────────────────────

  // Primary entry point: receive a message from any channel.
  // Parses the command, executes it, and replies via msg.reply() if available.
  // Also broadcasts significant responses to all other available adapters.
  void onMessageReceived(const ChannelMessage &msg,
                         bool *scheduleRunning,
                         bool *scheduleLoaded,
                         const SystemStatus &sys);

  // ── Outbound — called by application code ────────────────────────────────

  // Send an alert on all available channels.
  // severity: "INFO" | "WARNING" | "ERROR"
  void sendAlert(const String &message, const String &severity = "INFO");

  // Broadcast a status snapshot to all available channels.
  void broadcastStatus(const SystemStatus &sys);

  // ── Application event hooks ───────────────────────────────────────────────

  void onScheduleStarted  (const String &scheduleId);
  void onScheduleCompleted(const String &scheduleId);
  void onScheduleFailed   (const String &scheduleId, const String &reason);
  void onValveAction      (int nodeId, const String &valve, const String &action);
  void onSystemError      (const String &errorMessage);
  void onSystemWarning    (const String &warningMessage);

  // ── Diagnostics (Serial only — not sent over channels) ───────────────────

  void printAdapterStatus()  const;
  void printBriefStatus     (const SystemStatus &sys) const;
  void printSystemStatus    (const SystemStatus &sys) const;
  void printSystemDiagnostics() const;

  // ── Status text getters ───────────────────────────────────────────────────

  String getStatusText (const SystemStatus &sys) const;
  String getStatusBrief(const SystemStatus &sys) const;
  String getStatusJSON (const SystemStatus &sys) const;
  String getHelpText   () const;

  // ── Health ────────────────────────────────────────────────────────────────

  bool   isSystemHealthy() const;
  String getHealthStatus()  const;
};

// Note: UserCommunication instance lives inside CommManager.
// Access via commMgr.getUserComm() if needed externally.

#endif // USER_COMMUNICATION_H
