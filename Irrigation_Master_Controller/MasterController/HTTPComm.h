// HTTPComm.h - HTTP API communication
#ifndef HTTP_COMM_H
#define HTTP_COMM_H

#include <Arduino.h>
#include <WebServer.h>
#include "Config.h"

// HTTP command structure
struct HTTPCommand {
  String command;
  String source;  // IP address of requester
  unsigned long timestamp;
};

class HTTPComm {
private:
  WebServer* server;
  bool initialized;
  std::vector<HTTPCommand> pendingCommands;

  // HTTP endpoint handlers
  void handleRoot();
  void handleStatus();
  void handleCommand();
  void handleSchedules();
  void handleNotFound();

public:
  HTTPComm();
  ~HTTPComm();

  // Initialize HTTP server
  bool init(uint16_t port = 80);

  // Check if initialized
  bool isReady();

  // Process incoming HTTP requests
  void processBackground();

  // Check if there are pending commands
  bool hasCommands();

  // Get pending commands
  std::vector<HTTPCommand> getCommands();

  // Clear pending commands
  void clearCommands();

  // Send response (for command results)
  void sendResponse(const String &message);
};

extern HTTPComm httpComm;

#endif
