#ifndef BLE_COMM_H
#define BLE_COMM_H

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "Config.h"
#include "Utils.h"

// Callback function type for commands
typedef void (*BLECommandCallback)(int node, String command);

class BLEComm {
private:
  BLEServer *server;
  BLECharacteristic *txChar;
  BLECharacteristic *rxChar;
  bool connected;
  BLECommandCallback commandCallback;

public:
  BLEComm();
  bool init();
  bool notify(const String &msg);
  bool isConnected();
  void setConnected(bool state);
  void setCommandCallback(BLECommandCallback callback);
  void printStatus();
  
  friend class MyCharacteristicCallbacks;
};

extern BLEComm bleComm;

#endif