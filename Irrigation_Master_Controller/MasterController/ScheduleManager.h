#ifndef SCHEDULE_MANAGER_H
#define SCHEDULE_MANAGER_H

#include <Arduino.h>
#include <vector>
#include "Config.h"
#include "NodeCommunication.h"

// Forward declaration of UserCommunication
class UserCommunication;

// Note: SeqStep and Schedule are already defined in Config.h
// Don't redefine them here - just use them

/**
 * ScheduleManager - Manages irrigation schedules
 * 
 * FIXED: 
 * - Removed duplicate struct definitions (already in Config.h)
 * - Removed extern declaration from class (it's at global scope)
 * - Added UserCommunication pointer for notifications
 */
class ScheduleManager {
private:
  // Pointer to UserCommunication for sending notifications
  UserCommunication* userComm;

  // Pointer to NodeCommunication for valve commands
  NodeCommunication* nodeComm;

public:
  ScheduleManager();

  /**
   * Initialize with UserCommunication pointer
   * Must be called in setup() after UserComm is created
   */
  void init(UserCommunication* comm, NodeCommunication* nodeComm = nullptr);

  /**
   * Control pump
   */
  void setPump(bool on);

  /**
   * Send command to open a node
   */
  bool openNode(int node, int idx, uint32_t duration);

  /**
   * Send command to close a node
   */
  bool closeNode(int node, int idx);

  /**
   * Parse compact schedule format
   */
  bool parseCompact(const String &compact, Schedule &s);

  /**
   * Parse JSON schedule format
   */
  bool parseJSON(const String &json, Schedule &s);

  /**
   * Check if schedule is due and start if needed
   */
  void startIfDue();
  
  /**
   * Send notification/status message
   * Routes through UserCommunication if available
   */
  void notifyStatus(const String &message);
};

#endif