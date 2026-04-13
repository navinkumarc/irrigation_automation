#include "BLEComm.h"
#include "MessageQueue.h"

// Server callbacks
class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) override {
    bleComm.setConnected(true);
    Serial.println("[BLE] ✓ Client connected");

    // Stop advertising when connected (reduce BLE overhead)
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    if (pAdvertising) {
      pAdvertising->stop();
      Serial.println("[BLE] → Advertising stopped (client connected)");
    }

    Serial.println("[BLE] Connection established, MTU negotiation in progress");
  }

  void onDisconnect(BLEServer* pServer) override {
    bleComm.setConnected(false);
    Serial.println("[BLE] ⚠ Client disconnected");

    delay(500);
    BLEDevice::startAdvertising();
    Serial.println("[BLE] → Advertising restarted");
  }
};

// Characteristic callbacks
class MyCharacteristicCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pChar) override {
    auto value = pChar->getValue();
    String payload = String(value.c_str());
    payload.trim();
    
    if (payload.length() == 0) {
      Serial.println("[BLE] ⚠ Empty payload received");
      return;
    }
    
    Serial.println("[BLE] RX: " + payload);
    
    String response = "";
    
    // Check if it's a simple command: <node> <command>
    int space = payload.indexOf(' ');
    if (space > 0 && !payload.startsWith("SCH|") && !payload.startsWith("{")) {
      // It's a simple command like "1 PING"
      int node = payload.substring(0, space).toInt();
      String cmd = payload.substring(space + 1);
      cmd.toUpperCase();
      cmd.trim();
      
      if (node > 0 && node <= 255 && cmd.length() > 0) {
        Serial.printf("[BLE] → Command for Node %d: %s\n", node, cmd.c_str());
        
        // Use callback instead of direct LoRa access
        if (bleComm.commandCallback != nullptr) {
          bleComm.commandCallback(node, cmd);
          response = "OK|Command sent to node " + String(node);
        } else {
          response = "ERROR|No command handler registered";
          Serial.println("[BLE] ❌ Command callback not set!");
        }
      } else {
        response = "ERROR|Invalid format. Use: <node> <command>";
        Serial.printf("[BLE] ❌ Invalid command format: node=%d, cmd=%s\n", node, cmd.c_str());
      }
    }
    // It's a schedule or other message - queue it
    else {
      if (payload.indexOf("SRC=") < 0) {
        payload += ",SRC=BT";
      }
      
      if (incomingQueue.enqueue(payload)) {
        response = "QUEUED|Message queued for processing";
        Serial.println("[BLE] → Message queued");
      } else {
        response = "ERROR|Queue full";
        Serial.println("[BLE] ❌ Message queue full!");
      }
    }
    
    // Send response
    if (response.length() > 0 && bleComm.isConnected()) {
      if (!bleComm.notify(response)) {
        Serial.println("[BLE] ❌ Failed to send response");
      }
    }
  }
};

BLEComm::BLEComm() : server(nullptr), txChar(nullptr), rxChar(nullptr), connected(false), commandCallback(nullptr) {}

bool BLEComm::init() {
  Serial.println("[BLE] ========== Initializing BLE ==========");

  try {
    Serial.printf("[BLE] Device name: %s\n", BLE_DEVICE_NAME);
    BLEDevice::init(BLE_DEVICE_NAME);
    Serial.println("[BLE] ✓ BLE device initialized");

    server = BLEDevice::createServer();
    if (!server) {
      Serial.println("[BLE] ❌ BLE server creation failed");
      return false;
    }
    Serial.println("[BLE] ✓ BLE server created");

    server->setCallbacks(new MyServerCallbacks());
    Serial.println("[BLE] ✓ Server callbacks configured");

    Serial.printf("[BLE] Creating service with UUID: %s\n", SERVICE_UUID);
    BLEService *pService = server->createService(SERVICE_UUID);
    if (!pService) {
      Serial.println("[BLE] ❌ BLE service creation failed - check UUID format");
      return false;
    }
    Serial.println("[BLE] ✓ Service created successfully");

    // Create RX characteristic (FROM client TO device)
    Serial.printf("[BLE] Creating RX characteristic (UUID: %s)\n", CHARACTERISTIC_UUID_RX);
    rxChar = pService->createCharacteristic(
      CHARACTERISTIC_UUID_RX,
      BLECharacteristic::PROPERTY_WRITE |
      BLECharacteristic::PROPERTY_WRITE_NR |
      BLECharacteristic::PROPERTY_READ
    );

    if (!rxChar) {
      Serial.println("[BLE] ❌ RX characteristic creation failed");
      return false;
    }
    Serial.println("[BLE] ✓ RX characteristic created with WRITE/READ properties");
    rxChar->setCallbacks(new MyCharacteristicCallbacks());

    // Create TX characteristic (FROM device TO client)
    Serial.printf("[BLE] Creating TX characteristic (UUID: %s)\n", CHARACTERISTIC_UUID_TX);
    txChar = pService->createCharacteristic(
      CHARACTERISTIC_UUID_TX,
      BLECharacteristic::PROPERTY_NOTIFY |
      BLECharacteristic::PROPERTY_READ
    );

    if (!txChar) {
      Serial.println("[BLE] ❌ TX characteristic creation failed");
      return false;
    }
    Serial.println("[BLE] ✓ TX characteristic created with NOTIFY/READ properties");
    
    // Add CCCD descriptor for notifications
    txChar->addDescriptor(new BLE2902());
    Serial.println("[BLE] ✓ CCCD descriptor added to TX characteristic");
    
    // Initialize TX characteristic with a default value
    txChar->setValue("READY");
    Serial.println("[BLE] ✓ TX characteristic initialized");

    // Start service - FIXED: pService->start() returns void, not bool
    pService->start();
    Serial.println("[BLE] ✓ Service started");

    // Configure advertising with proper connection parameters
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    if (!pAdvertising) {
      Serial.println("[BLE] ❌ Failed to get advertising object");
      return false;
    }

    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);

    // Set connection interval preferences (in units of 1.25ms)
    pAdvertising->setMinPreferred(0x10);  // 20ms minimum
    pAdvertising->setMaxPreferred(0x20);  // 40ms maximum
    Serial.println("[BLE] ✓ Connection interval preferences set: 20-40ms");

    // Configure advertisement data
    BLEAdvertisementData advData;
    advData.setName(BLE_DEVICE_NAME);
    advData.setFlags(0x06);

    String mfr = String("\x01\x02\x03\x04");
    advData.setManufacturerData(mfr);
    Serial.println("[BLE] ✓ Advertisement data configured");

    pAdvertising->setAdvertisementData(advData);

    // Configure scan response
    BLEAdvertisementData scanResp;
    scanResp.setName(BLE_DEVICE_NAME);
    pAdvertising->setScanResponseData(scanResp);
    Serial.println("[BLE] ✓ Scan response configured");

    // Start advertising
    BLEDevice::startAdvertising();
    Serial.println("[BLE] ✓ Advertising started");

    Serial.println("[BLE] ========== BLE INITIALIZATION SUCCESS ==========");
    Serial.println("[BLE] Device: " + String(BLE_DEVICE_NAME));
    Serial.println("[BLE] Service UUID: " + String(SERVICE_UUID));
    Serial.println("[BLE] Status: Ready for connections");
    
    return true;

  } catch (...) {
    Serial.println("[BLE] ❌ Exception during BLE initialization");
    return false;
  }
}

bool BLEComm::notify(const String &msg) {
  if (!connected) {
    Serial.println("[BLE] ❌ Cannot notify - client not connected");
    return false;
  }

  if (!txChar) {
    Serial.println("[BLE] ❌ Cannot notify - txChar is null");
    return false;
  }

  if (msg.length() == 0) {
    Serial.println("[BLE] ⚠ Attempt to send empty message");
    return false;
  }

  String m = msg;
  
  // Log if truncation occurs
  if (m.length() > 200) {
    Serial.printf("[BLE] ⚠ Message truncated from %d to 200 bytes\n", m.length());
    m = m.substring(0, 200);
  }

  try {
    txChar->setValue((uint8_t*)m.c_str(), m.length());
    
    // FIXED: notify() returns void, not bool
    // Just call it without checking return value
    txChar->notify();
    
    Serial.println("[BLE] TX: " + m);
    vTaskDelay(pdMS_TO_TICKS(10));
    return true;
    
  } catch (...) {
    Serial.println("[BLE] ❌ Exception during notify");
    return false;
  }
}

bool BLEComm::isConnected() {
  return connected;
}

void BLEComm::setConnected(bool state) {
  connected = state;
}

void BLEComm::setCommandCallback(BLECommandCallback callback) {
  commandCallback = callback;
  if (callback != nullptr) {
    Serial.println("[BLE] ✓ Command callback registered");
  } else {
    Serial.println("[BLE] ⚠ Command callback cleared (set to null)");
  }
}

void BLEComm::printStatus() {
  Serial.println("[BLE] ========== BLE Status ==========");
  Serial.printf("[BLE] Connected: %s\n", connected ? "YES" : "NO");
  Serial.printf("[BLE] Server: %s\n", server != nullptr ? "OK" : "NULL");
  Serial.printf("[BLE] TX Characteristic: %s\n", txChar != nullptr ? "OK" : "NULL");
  Serial.printf("[BLE] RX Characteristic: %s\n", rxChar != nullptr ? "OK" : "NULL");
  Serial.printf("[BLE] Callback: %s\n", commandCallback != nullptr ? "REGISTERED" : "NOT SET");
  Serial.println("[BLE] ==================================");
}