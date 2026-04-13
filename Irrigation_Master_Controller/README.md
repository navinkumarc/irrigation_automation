# Irrigation Main Controller

ESP32-based irrigation controller with MQTT communication, LoRa connectivity, and cellular backup.

## Hardware

- **Board**: Heltec WiFi LoRa 32 V3
- **MCU**: ESP32-S3
- **LoRa**: SX1276 (865 MHz for India)
- **Modem**: EC200U 4G/LTE (PPPoS)
- **RTC**: DS3231
- **Display**: OLED (optional)

## Features

✅ **MQTT Communication** - HiveMQ Cloud with MQTT v3.1.1 over TLS/SSL
✅ **Dual Network** - PPPoS (cellular) primary, WiFi fallback
✅ **LoRa Communication** - 865 MHz for irrigation nodes
✅ **BLE Configuration** - Mobile app control
✅ **HTTP REST API** - Local network control
✅ **Schedule Management** - Time-based irrigation schedules
✅ **Real-time Control** - Manual valve/pump control

## Quick Start

### Arduino IDE Setup

1. **Install Arduino IDE** (2.x or 1.8.19+)

2. **Install ESP32 Board Support**:
   - File → Preferences
   - Additional Board Manager URLs:
     ```
     https://espressif.github.io/arduino-esp32/package_esp32_index.json
     ```
   - Tools → Board → Boards Manager → Search "esp32" → Install

3. **Open Project**:
   - File → Open → `IrrigationController/IrrigationController.ino`

4. **Select Board**:
   - Tools → Board → ESP32 Arduino → **Heltec WiFi LoRa 32(V3)**

5. **Configure Settings**:
   - Upload Speed: 921600
   - CPU Frequency: 240MHz (WiFi)
   - Flash Size: 8MB (64Mb)

6. **Upload**:
   - Connect ESP32 via USB
   - Select COM port
   - Click Upload (Ctrl+U)

7. **Monitor**:
   - Tools → Serial Monitor (115200 baud)

### Configuration

Edit `IrrigationController/Config.h` to customize:

```cpp
// WiFi
#define WIFI_SSID "your_ssid"
#define WIFI_PASS "your_password"

// MQTT (HiveMQ Cloud)
#define MQTT_BROKER "your_broker.hivemq.cloud"
#define MQTT_USER "your_username"
#define MQTT_PASS "your_password"

// SMS Alerts
#define SMS_ALERT_PHONE_1 "+919944272647"
```

## MQTT Setup

This project uses **HiveMQ Cloud** with **MQTT v3.1.1**.

**Why MQTT v3.1.1?**
- ✅ Enabled by default in Arduino ESP32 core
- ✅ No complex Arduino IDE configuration needed
- ✅ Works out of the box

**Current Broker**: HiveMQ Cloud
- Host: `39aff691b9b5421ab98adc2addedbd83.s1.eu.hivemq.cloud`
- Port: 8883 (TLS/SSL)
- Protocol: MQTT v3.1.1

See **[MQTT_SETUP.md](MQTT_SETUP.md)** for complete configuration details.

## Network Connectivity

### PPPoS (Primary)
- EC200U 4G modem via UART
- APN: `airtelgprs.com`
- Automatic connection on boot

### WiFi (Fallback)
- SSID: `sekaranfarm`
- Automatic fallback if PPPoS fails

## Communication Channels

1. **MQTT** - Cloud communication (primary)
2. **LoRa** - Node communication (865 MHz)
3. **BLE** - Mobile app configuration
4. **HTTP** - Local REST API (port 80)
5. **SMS** - Alerts and fallback commands (when MQTT disabled)

## Project Structure

```
Irrigation_Main_Controller/
├── IrrigationController/           # Arduino sketch
│   ├── IrrigationController.ino   # Main sketch
│   ├── Config.h                   # Configuration
│   ├── MQTTComm.cpp/h            # MQTT communication
│   ├── LoRaComm.cpp/h            # LoRa communication
│   ├── ModemComm.cpp/h           # Modem control
│   ├── NetworkManager.cpp/h      # Network failover
│   ├── ScheduleManager.cpp/h     # Schedule execution
│   ├── StorageManager.cpp/h      # EEPROM storage
│   ├── UserComm.cpp/h            # Command processing
│   └── ... (other modules)
├── MQTT_SETUP.md                  # MQTT configuration guide
├── README.md                      # This file
└── PPPoS_IMPLEMENTATION_GUIDE.md  # PPPoS setup guide
```

## MQTT Topics

```
irrigation/status      → System status updates
irrigation/commands    → Incoming commands (subscribe)
irrigation/telemetry   → Sensor data and metrics
irrigation/alerts      → Alert notifications
```

## Command Examples

### MQTT Commands

Publish to `irrigation/commands`:

```json
{"cmd":"START","sch_id":"SCH001"}
{"cmd":"STOP"}
{"cmd":"STATUS"}
{"cmd":"ADDSCH","rec":"D","time":"06:00","seq":[{"n":1,"v":1,"d":600000}]}
```

### BLE Commands

Connect via BLE and send:

```
START SCH001
STOP
STATUS
ADDSCH D 06:00 1:1:600000
```

### HTTP API

```bash
# Get status
curl http://192.168.1.100/status

# Start schedule
curl -X POST http://192.168.1.100/start -d '{"sch_id":"SCH001"}'

# Stop irrigation
curl -X POST http://192.168.1.100/stop
```

## Schedule Format

Schedules can be created via MQTT, BLE, or HTTP:

```json
{
  "cmd": "ADDSCH",
  "rec": "D",              // D=Daily, W=Weekly, M=Monthly, O=Once
  "time": "06:00",         // Start time (HH:MM)
  "seq": [                 // Sequence of valve operations
    {"n": 1, "v": 1, "d": 600000},   // Node 1, Valve 1, 10 minutes
    {"n": 1, "v": 2, "d": 900000},   // Node 1, Valve 2, 15 minutes
    {"n": 2, "v": 1, "d": 1200000}   // Node 2, Valve 1, 20 minutes
  ]
}
```

## LoRa Configuration

**Frequency**: 865 MHz (India ISM band)
**Spreading Factor**: 10
**Bandwidth**: 125 kHz
**TX Power**: 14 dBm

Communicates with irrigation nodes to control valves.

## Troubleshooting

### MQTT Not Connecting

1. Check network (WiFi or PPPoS)
2. Verify credentials in Config.h
3. Check HiveMQ Cloud console
4. Monitor serial output for errors

### PPPoS Issues

1. Check SIM card is active
2. Verify APN: `airtelgprs.com`
3. Check modem power and connections
4. Review PPPoS_IMPLEMENTATION_GUIDE.md

### LoRa Communication Failed

1. Check antenna connection
2. Verify frequency: 865 MHz
3. Ensure nodes are powered on
4. Check node IDs match configuration

### Build Errors

1. Check ESP32 board support installed
2. Verify Arduino IDE version (2.x or 1.8.19+)
3. Select correct board: Heltec WiFi LoRa 32(V3)
4. Install required libraries

## Required Libraries

All libraries auto-installed by Arduino IDE:

- RTClib (Adafruit)
- Adafruit BusIO
- LoRa (Sandeep Mistry)
- ArduinoJson
- WiFi Kit Series (Heltec)

ESP-IDF components (built-in):
- mqtt_client.h
- esp_ppp.h
- esp_netif.h

## Pin Assignments

See `Config.h` for complete pin definitions:

```cpp
// LoRa
LORA_SS    = 8
LORA_RST   = 12
LORA_DIO0  = 14

// Modem
MODEM_TX   = 46
MODEM_RX   = 45
MODEM_PWRKEY = 4

// Pump
PUMP_PIN   = 25

// RTC
RTC_SDA    = 41
RTC_SCL    = 42
```

## Development

### Enabling Features

Edit `Config.h`:

```cpp
#define ENABLE_LORA 1        // LoRa communication
#define ENABLE_MODEM 1       // 4G modem
#define ENABLE_MQTT 1        // MQTT (0 = use SMS instead)
#define ENABLE_BLE 1         // BLE configuration
#define ENABLE_WIFI 1        // WiFi connectivity
#define ENABLE_HTTP 1        // HTTP REST API
#define ENABLE_PPPOS 1       // PPPoS cellular data
```

### Debug Output

Uncomment in `Config.h`:

```cpp
#define DEBUG_LORA
#define DEBUG_MQTT
#define DEBUG_SMS
#define DEBUG_SCHEDULER
```

## License

[Your License Here]

## Support

For issues and questions:
- Check serial monitor output (115200 baud)
- Review documentation in this repository
- Check connection status in HiveMQ console

## Version History

### v1.2.0 (Current)
- Switched to HiveMQ Cloud with MQTT v3.1.1
- Simplified Arduino IDE setup (no config changes needed)
- Removed MQTT v5 requirements
- Updated documentation

### v1.1.0
- Added PPPoS support for cellular data
- Implemented NetworkManager with automatic failover
- Added HTTP REST API
- Improved BLE connectivity

### v1.0.0
- Initial release
- MQTT, LoRa, BLE communication
- Schedule management
- SMS alerts
