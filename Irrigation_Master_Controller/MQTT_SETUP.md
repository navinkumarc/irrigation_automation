# MQTT Setup - HiveMQ Cloud

## Overview

This project uses **HiveMQ Cloud** with **MQTT v3.1.1** over TLS/SSL for IoT communication.

## Why HiveMQ with MQTT v3.1.1?

### Previous Issue with EMQX
- EMQX Cloud only supports MQTT v5
- MQTT v5 requires `CONFIG_MQTT_PROTOCOL_5` to be enabled at compile time
- Enabling MQTT v5 in Arduino IDE requires modifying Arduino ESP32 core files
- This was complex and error-prone for Arduino IDE users

### Solution: HiveMQ with MQTT v3.1.1
✅ **MQTT v3.1.1 is enabled by default** in Arduino ESP32 core
✅ **No Arduino IDE configuration changes needed**
✅ **Works out of the box** - just upload and run!
✅ **Fully compatible** with ESP-IDF MQTT client
✅ **HiveMQ Cloud** supports both v3.1.1 and v5

## Current Configuration

### Broker Details
- **Provider**: HiveMQ Cloud
- **Host**: `39aff691b9b5421ab98adc2addedbd83.s1.eu.hivemq.cloud`
- **Port**: `8883` (MQTT over TLS/SSL)
- **Protocol**: MQTT v3.1.1
- **TLS/SSL**: Enabled (certificate verification skipped for testing)
- **Username**: `sekaranfarm`
- **Password**: `Welcome123`
- **Client ID**: `irrigation_controller_001`

### MQTT Topics
```
irrigation/status      - System status updates
irrigation/commands    - Incoming commands
irrigation/telemetry   - Sensor data and metrics
irrigation/alerts      - Alert notifications
```

## Configuration Files

### Config.h (Lines 128-136)
```cpp
// ========== MQTT Settings ==========
// HiveMQ Cloud Broker - MQTT v3.1.1 over TLS/SSL
#define MQTT_BROKER "39aff691b9b5421ab98adc2addedbd83.s1.eu.hivemq.cloud"
#define MQTT_PORT 8883
#define MQTT_USE_SSL 1
#define MQTT_CLIENT_ID "irrigation_controller_001"
#define MQTT_USER "sekaranfarm"
#define MQTT_PASS "Welcome123"
// Using ESP-IDF MQTT client with MQTT v3.1.1 (enabled by default in Arduino ESP32)
```

### MQTTComm.cpp (Line 46)
```cpp
mqtt_cfg.session.protocol_ver = MQTT_PROTOCOL_V_3_1_1;  // MQTT v3.1.1 (enabled by default)
```

## Expected Serial Output

When the ESP32 connects successfully, you should see:

```
[MQTT] Initializing MQTT v3.1.1 client...
[MQTT] Broker URI: mqtts://39aff691b9b5421ab98adc2addedbd83.s1.eu.hivemq.cloud:8883
[MQTT] → Configuring TLS/SSL...
[MQTT] ⚠ TLS certificate validation disabled (testing mode)
[MQTT] ✓ TLS/SSL configured
[MQTT] ✓ MQTT v3.1.1 client initialized
[MQTT] Broker: 39aff691b9b5421ab98adc2addedbd83.s1.eu.hivemq.cloud:8883
[MQTT] Protocol: MQTT v3.1.1 over TLS/SSL
[MQTT] Client ID: irrigation_controller_001
[MQTT] Note: Using HiveMQ Cloud with MQTT v3.1.1
[MQTT] Starting MQTT client...
[MQTT] ✓ MQTT client started (will connect asynchronously)
[MQTT] → Connecting to broker...
[MQTT] ✓ Connected to broker
[MQTT] Session present: 0
[MQTT] Auto-subscribing to commands topic, msg_id=1
[MQTT] ✓ Subscribed, msg_id=1
```

✅ **No more "MQTT_PROTOCOL_5 feature" errors!**

## Arduino IDE Setup

### Requirements
- Arduino IDE 2.x or 1.8.19+
- ESP32 Arduino Core 3.0.0 or higher
- Board: Heltec WiFi LoRa 32(V3)

### Installation Steps

1. **Open Arduino IDE**

2. **Open the sketch**:
   - File → Open → Navigate to `IrrigationController/IrrigationController.ino`

3. **Select board**:
   - Tools → Board → ESP32 Arduino → **Heltec WiFi LoRa 32(V3)**

4. **Configure board settings**:
   - Upload Speed: 921600
   - CPU Frequency: 240MHz (WiFi)
   - Flash Size: 8MB (64Mb)
   - Partition Scheme: Default 4MB with spiffs
   - Core Debug Level: None (or Info for debugging)

5. **Select COM port**:
   - Tools → Port → Select your ESP32 COM port

6. **Upload**:
   - Click Upload button or Ctrl+U
   - Wait for compilation and upload to complete

7. **Monitor serial output**:
   - Tools → Serial Monitor
   - Set baud rate to **115200**
   - You should see MQTT connection messages

### No Additional Configuration Needed!

Unlike MQTT v5, MQTT v3.1.1 requires **no special Arduino IDE configuration**. Just upload and run!

## Network Connectivity

This project supports two network modes:

### 1. PPPoS (Cellular Data) - Primary
- Uses EC200U 4G modem
- Automatic connection via PPP protocol
- Priority connection method

### 2. WiFi - Fallback
- SSID: `sekaranfarm`
- Password: `welcome123`
- Automatic fallback if PPPoS fails

The NetworkManager automatically handles connection and failover.

## Testing MQTT Connection

### Test with MQTT Explorer

1. Download MQTT Explorer: http://mqtt-explorer.com/
2. Create new connection:
   - Name: HiveMQ Irrigation
   - Protocol: mqtts://
   - Host: `39aff691b9b5421ab98adc2addedbd83.s1.eu.hivemq.cloud`
   - Port: 8883
   - Username: `sekaranfarm`
   - Password: `Welcome123`
   - Validate certificate: No (for testing)
3. Connect and subscribe to `irrigation/#`
4. You should see messages from your ESP32

### Test with mosquitto_pub/sub

Subscribe to all topics:
```bash
mosquitto_sub -h 39aff691b9b5421ab98adc2addedbd83.s1.eu.hivemq.cloud \
  -p 8883 \
  -u sekaranfarm \
  -P Welcome123 \
  -t "irrigation/#" \
  --cafile /etc/ssl/certs/ca-certificates.crt
```

Publish test command:
```bash
mosquitto_pub -h 39aff691b9b5421ab98adc2addedbd83.s1.eu.hivemq.cloud \
  -p 8883 \
  -u sekaranfarm \
  -P Welcome123 \
  -t "irrigation/commands" \
  -m '{"cmd":"status"}'
```

## TLS/SSL Configuration

### Current Setup (Testing Mode)
```cpp
mqtt_cfg.broker.verification.skip_cert_common_name_check = true;
mqtt_cfg.broker.verification.certificate = NULL;
```

This skips certificate verification for easier testing.

### Production Setup (Recommended)

For production, use proper certificate verification:

1. Download HiveMQ CA certificate
2. Store in `IrrigationController/hivemq_ca_cert.pem`
3. Update MQTTComm.cpp:
   ```cpp
   // Option 2: Use CA certificate (RECOMMENDED for production)
   mqtt_cfg.broker.verification.certificate = hivemq_ca_cert_pem_start;
   mqtt_cfg.broker.verification.skip_cert_common_name_check = false;
   ```
4. Add to IrrigationController.ino:
   ```cpp
   extern const char hivemq_ca_cert_pem_start[] asm("_binary_hivemq_ca_cert_pem_start");
   extern const char hivemq_ca_cert_pem_end[] asm("_binary_hivemq_ca_cert_pem_end");
   ```

## Troubleshooting

### Issue: "Failed to create MQTT client"

**Solution**:
- This was the old MQTT v5 error
- With MQTT v3.1.1, this should not occur
- If it does, check your Arduino ESP32 core version (should be 3.0.0+)

### Issue: "Connection timeout"

**Solutions**:
1. Check network connectivity (WiFi or PPPoS)
2. Verify HiveMQ credentials are correct
3. Check firewall settings (port 8883 must be open)
4. Verify HiveMQ cluster is active in console

### Issue: "TLS handshake failed"

**Solutions**:
1. Check system time is correct (TLS requires accurate time)
2. NTP should sync time automatically
3. Verify ESP32 has internet access
4. Check if HiveMQ certificate is valid

### Issue: "Authentication failed"

**Solutions**:
1. Verify username: `sekaranfarm`
2. Verify password: `Welcome123`
3. Check HiveMQ Cloud console for access control settings
4. Ensure client ID is unique

### Issue: Not receiving messages

**Solutions**:
1. Check subscription in serial monitor
2. Verify topics match exactly (case-sensitive)
3. Use MQTT Explorer to test broker connectivity
4. Check QoS settings (default is 0)

## Changing MQTT Broker

To switch to a different broker:

1. Update `Config.h`:
   ```cpp
   #define MQTT_BROKER "your_broker.example.com"
   #define MQTT_PORT 8883
   #define MQTT_USER "your_username"
   #define MQTT_PASS "your_password"
   ```

2. If using different protocol:
   - MQTT v3.1.1: No changes needed (current)
   - MQTT v5: Change `MQTT_PROTOCOL_V_3_1_1` to `MQTT_PROTOCOL_V_5` in MQTTComm.cpp (requires Arduino IDE configuration)

3. Upload updated sketch

## HiveMQ Cloud Console

Access your HiveMQ cluster:
- URL: https://console.hivemq.cloud/
- Cluster ID: `39aff691b9b5421ab98adc2addedbd83`

From the console you can:
- Monitor connected clients
- View message statistics
- Configure access control
- Set up data integrations
- View logs and metrics

## Security Best Practices

### For Production:

1. ✅ Enable certificate verification
2. ✅ Use strong, unique passwords
3. ✅ Rotate credentials periodically
4. ✅ Use access control lists (ACLs) in HiveMQ
5. ✅ Monitor for unusual activity
6. ✅ Keep ESP32 firmware updated
7. ✅ Use unique client IDs per device

### Current Status:
- ⚠️ Certificate verification: Disabled (testing only)
- ⚠️ Credentials: Hardcoded (move to secure storage for production)
- ✅ TLS/SSL: Enabled
- ✅ Protocol: MQTT v3.1.1

## Technical Details

### ESP-IDF MQTT Client
- Library: `mqtt_client.h` (ESP-IDF native)
- Async operation: Event-driven
- Auto-reconnect: Built-in
- QoS support: 0, 1, 2
- TLS/SSL: Supported
- Keep-alive: 120 seconds
- Buffer size: 2048 bytes

### Protocol Comparison

| Feature | MQTT v3.1.1 | MQTT v5 |
|---------|-------------|---------|
| Arduino ESP32 Support | ✅ Default | ⚠️ Requires config |
| HiveMQ Support | ✅ Yes | ✅ Yes |
| Setup Complexity | ✅ Simple | ⚠️ Complex |
| Request/Response | ❌ Manual | ✅ Built-in |
| User Properties | ❌ No | ✅ Yes |
| Reason Codes | ❌ Limited | ✅ Detailed |
| **Recommendation** | ✅ **Use this** | ⚠️ Only if needed |

For most IoT applications, **MQTT v3.1.1 is sufficient** and much simpler to set up.

## Additional Resources

- HiveMQ Cloud Documentation: https://docs.hivemq.com/hivemq-cloud/
- MQTT v3.1.1 Specification: https://docs.oasis-open.org/mqtt/mqtt/v3.1.1/
- ESP-IDF MQTT Documentation: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/protocols/mqtt.html
- Arduino ESP32 Documentation: https://docs.espressif.com/projects/arduino-esp32/

## Support

If you encounter issues:
1. Check serial monitor output at 115200 baud
2. Verify network connectivity (WiFi or PPPoS)
3. Test broker with MQTT Explorer
4. Check HiveMQ Cloud console for errors
5. Review this documentation for troubleshooting steps
