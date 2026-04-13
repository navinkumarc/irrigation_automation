// HTTPComm.cpp - HTTP API communication
#include "HTTPComm.h"

HTTPComm::HTTPComm() : server(nullptr), initialized(false) {}

HTTPComm::~HTTPComm() {
  if (server != nullptr) {
    delete server;
  }
}

bool HTTPComm::init(uint16_t port) {
  Serial.println("[HTTP] Initializing HTTP server on port " + String(port) + "...");

  server = new WebServer(port);

  if (server == nullptr) {
    Serial.println("[HTTP] ❌ Failed to create server");
    return false;
  }

  // Set up route handlers
  server->on("/", [this]() { this->handleRoot(); });
  server->on("/status", [this]() { this->handleStatus(); });
  server->on("/command", HTTP_POST, [this]() { this->handleCommand(); });
  server->on("/schedules", [this]() { this->handleSchedules(); });
  server->onNotFound([this]() { this->handleNotFound(); });

  server->begin();
  initialized = true;

  Serial.println("[HTTP] ✓ HTTP server started on port " + String(port));
  return true;
}

bool HTTPComm::isReady() {
  return initialized && server != nullptr;
}

void HTTPComm::processBackground() {
  if (!isReady()) {
    return;
  }

  server->handleClient();
}

bool HTTPComm::hasCommands() {
  return !pendingCommands.empty();
}

std::vector<HTTPCommand> HTTPComm::getCommands() {
  return pendingCommands;
}

void HTTPComm::clearCommands() {
  pendingCommands.clear();
}

void HTTPComm::sendResponse(const String &message) {
  if (!isReady()) {
    return;
  }

  // Response will be sent in handleCommand()
  // This is a placeholder for future enhancement
}

// ========== HTTP Endpoint Handlers ==========

void HTTPComm::handleRoot() {
  String html = "<!DOCTYPE html><html><head><title>Irrigation Controller</title></head><body>";
  html += "<h1>Irrigation Controller API</h1>";
  html += "<h2>Available Endpoints:</h2>";
  html += "<ul>";
  html += "<li>GET /status - Get system status</li>";
  html += "<li>POST /command - Send command (body: {\"command\":\"...\"})</li>";
  html += "<li>GET /schedules - Get schedules</li>";
  html += "</ul>";
  html += "</body></html>";

  server->send(200, "text/html", html);
  Serial.println("[HTTP] Root page requested from " + server->client().remoteIP().toString());
}

void HTTPComm::handleStatus() {
  String json = "{\"status\":\"ok\",\"uptime\":" + String(millis() / 1000) + "}";
  server->send(200, "application/json", json);
  Serial.println("[HTTP] Status requested from " + server->client().remoteIP().toString());
}

void HTTPComm::handleCommand() {
  if (!server->hasArg("plain")) {
    server->send(400, "application/json", "{\"error\":\"No body\"}");
    return;
  }

  String body = server->arg("plain");
  Serial.println("[HTTP] Command received from " + server->client().remoteIP().toString() + ": " + body);

  // Parse JSON to extract command
  // Simple parsing - look for "command":"value"
  int cmdPos = body.indexOf("\"command\"");
  if (cmdPos < 0) {
    server->send(400, "application/json", "{\"error\":\"No command field\"}");
    return;
  }

  int colonPos = body.indexOf(":", cmdPos);
  int quoteStart = body.indexOf("\"", colonPos);
  int quoteEnd = body.indexOf("\"", quoteStart + 1);

  if (quoteStart < 0 || quoteEnd < 0) {
    server->send(400, "application/json", "{\"error\":\"Invalid command format\"}");
    return;
  }

  String command = body.substring(quoteStart + 1, quoteEnd);

  // Queue command for processing
  HTTPCommand cmd;
  cmd.command = command;
  cmd.source = server->client().remoteIP().toString();
  cmd.timestamp = millis();
  pendingCommands.push_back(cmd);

  server->send(200, "application/json", "{\"success\":true,\"message\":\"Command queued\"}");
  Serial.println("[HTTP] Command queued: " + command);
}

void HTTPComm::handleSchedules() {
  String json = "{\"schedules\":[]}";  // Placeholder
  server->send(200, "application/json", json);
  Serial.println("[HTTP] Schedules requested from " + server->client().remoteIP().toString());
}

void HTTPComm::handleNotFound() {
  server->send(404, "application/json", "{\"error\":\"Not found\"}");
  Serial.println("[HTTP] 404 - Path not found: " + server->uri());
}
