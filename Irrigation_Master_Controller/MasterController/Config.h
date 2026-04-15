// Config.h - Complete configuration with all constants
#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <vector>
#include <cstdint>
#include <ctime>

// ========== Feature Enables ==========
// Each flag is fully independent — enable only what you need.
//
// IMPORTANT — Modem mode constraint:
//   ENABLE_SMS and ENABLE_PPPOS are MUTUALLY EXCLUSIVE.
//   The EC200U modem can only be in AT command mode (SMS) OR PPP data mode
//   at any one time. Set at most ONE of these to 1.
//
//   ENABLE_SMS  = 1  →  Modem used for SMS (AT command mode)
//   ENABLE_PPPOS = 1  →  Modem used for cellular data (PPP mode)
//
// ── Hardware always present on this board ──────────────────────────────────
#define ENABLE_LORA    1    // LoRa radio — primary node + user channel
#define ENABLE_BLE     1    // Bluetooth hardware compiled in (runtime OFF by default)
#define ENABLE_DISPLAY 1    // OLED/LCD display
#define ENABLE_RTC     1    // Real-time clock (DS3231)

// ── Active communication channel — choose ONE ───────────────────────────────
// Default: SMS.  Switch to MQTT or HTTP via: SET CHANNEL MQTT / SET CHANNEL HTTP
#define ENABLE_SMS     1    // SMS via modem AT commands (default active channel)
#define ENABLE_MQTT    0    // MQTT over internet bearer (WiFi or PPPoS)
#define ENABLE_HTTP    0    // HTTP REST API over internet bearer

// ── Internet bearer — only needed when ENABLE_MQTT=1 or ENABLE_HTTP=1 ───────
#define ENABLE_WIFI    0    // WiFi bearer (disabled when SMS is active channel)
#define ENABLE_PPPOS   0    // PPPoS cellular data bearer — MUTUALLY EXCLUSIVE with SMS

// Compile-time enforcement of the SMS / PPPoS mutual-exclusion rule
#if ENABLE_SMS && ENABLE_PPPOS
  #error "ENABLE_SMS and ENABLE_PPPOS cannot both be 1. The modem supports only one mode at a time."
#endif

// Warn if internet services enabled without a bearer
#if (ENABLE_MQTT || ENABLE_HTTP) && !ENABLE_WIFI && !ENABLE_PPPOS
  #warning "ENABLE_MQTT/HTTP is set but neither ENABLE_WIFI nor ENABLE_PPPOS is enabled. No internet bearer available."
#endif

// ========== Modem Hardware ==========
// Derived automatically — do not edit.
#if ENABLE_SMS || ENABLE_PPPOS
  #define ENABLE_MODEM 1
#else
  #define ENABLE_MODEM 0
#endif

// ========== SMS Sub-features ==========
#if ENABLE_SMS
  #define ENABLE_SMS_COMMANDS 1
  #define ENABLE_SMS_ALERTS   1
#else
  #define ENABLE_SMS_COMMANDS 0
  #define ENABLE_SMS_ALERTS   0
#endif

// ========== LoRa Feature Flags ==========
#define ENABLE_LORA_USER_COMM 1
#define ENABLE_LORA_NODE_COMM 1

// ========== Communication Channel Commands ==========
#define ENABLE_BLE_COMMANDS    1
#define ENABLE_WIFI_COMMANDS   1
#define ENABLE_HTTP_COMMANDS   1
#define ENABLE_SERIAL_COMM     1    // Serial monitor as a user communication channel
#define SERIAL_COMM_BAUD       115200  // Must match Serial.begin() in setup()

// ========== HTTP API Settings ==========
#define HTTP_SERVER_PORT 80

// ========== Buffer Sizes ==========
#define LORA_BUFFER_SIZE    256
#define INCOMING_QUEUE_SIZE 10

// ========== Pin Definitions ==========
#define LORA_SCK  9
#define LORA_MISO 11
#define LORA_MOSI 10
#define LORA_SS   8
#define LORA_RST  12
#define LORA_DIO0 14

#define MODEM_RX     45
#define MODEM_TX     46
#define MODEM_PWRKEY 4
#define MODEM_RESET  15

// ── Heltec WiFi LoRa 32 V3 (ESP32-S3) — GPIO allocation for pump relays ──
//
// RESERVED by onboard hardware (do NOT use):
//   LoRa SX1262 : 8 9 10 11 12 13 14
//   OLED I2C    : 17 18 21
//   USB         : 19 20
//   Battery/ADC : 1 36 37
//   Onboard LED : 35
//   Boot button : 0
//   Modem EC200U: 4 15 45 46
//   RTC DS3231  : 41 42
//   Strapping   : 0 3 45 46
//   NOT on S3   : 25-34  ← these are ESP32 original only, absent on S3!
//
// FREE GPIO for relay outputs: 5 6 7 38 39 40 47 48
//
// Pump relay assignments (connect relay IN pin → these GPIO):
//   G1 IPC  (Irrigation Pump group 1)  → GPIO 5
//   G2 IPC  (Irrigation Pump group 2)  → GPIO 6
//   W1 WSP  (Well pump 1)              → GPIO 7
//   W2 WSP  (Well pump 2)              → GPIO 38
//   W3 WSP  (Well pump 3)              → GPIO 39
//
// Tank sensors (W1 example — adjust per installation):
//   W1 TANK EMPTY                      → GPIO 40  (active LOW)
//   W1 TANK FULL                       → GPIO 47  (active HIGH)
//
// All relays: ACTIVE_HIGH = true (relay IN HIGH → pump ON)
// Use optocoupler relay modules; do NOT drive pump directly from GPIO.

// ── Irrigation Pump Controller (IPC) relay pins ──────────────────────────
#define PUMP_PIN         5     // legacy alias — same as IPC_PIN (G1)
#define PUMP_ACTIVE_HIGH true
#define IPC_PIN          5     // G1 — Irrigation group 1 relay → GPIO 5
#define IPC_ACTIVE_HIGH  true
#define IPC2_PIN         6     // G2 — Irrigation group 2 relay → GPIO 6
#define IPC2_ACTIVE_HIGH true

// ── Water Source Pump Controller (WSPC) relay pins ───────────────────────
#define WSP_PIN          7     // W1 — Well pump 1 relay → GPIO 7
#define WSP_ACTIVE_HIGH  true
#define WSP2_PIN         38    // W2 — Well pump 2 relay → GPIO 38
#define WSP2_ACTIVE_HIGH true
#define WSP3_PIN         39    // W3 — Well pump 3 relay → GPIO 39
#define WSP3_ACTIVE_HIGH true

// ── Tank level sensor pins ─────────────────────────────────────────────────
// Sensor type: float switch or conductive probe.
// EMPTY sensor: NC (Normally Closed) float — opens when water drops below probe.
//   Wire: GPIO → 10kΩ pull-up to 3.3V → sensor → GND
//   Logic: LOW = tank empty (sensor open, pull-up reading low? No —
//          use INPUT_PULLUP, sensor pulls to GND when submerged = HIGH = full)
//
// Recommended wiring for float switch (NC type):
//   EMPTY: GPIO + INPUT_PULLUP; float NC→GND; LOW = switch open = tank empty
//   FULL:  GPIO + INPUT_PULLUP; float NC→GND; LOW = switch closed = tank full
//
// Set pin to 0 to disable that sensor (runs without it in MANUAL/SCHEDULE mode).

// W1 sensors → GPIO 40 (empty) and GPIO 47 (full)
#define WSP_TANK_EMPTY_PIN    40   // W1 tank empty → GPIO 40 (INPUT_PULLUP, LOW=empty)
#define WSP_TANK_FULL_PIN     47   // W1 tank full  → GPIO 47 (INPUT_PULLUP, LOW=full)

// W2 sensors → GPIO 16 (empty) and GPIO 22 (full)
#define WSP2_TANK_EMPTY_PIN   16   // W2 tank empty → GPIO 16
#define WSP2_TANK_FULL_PIN    22   // W2 tank full  → GPIO 22

// W3 sensors → GPIO 23 (empty) and GPIO 24 (full)
#define WSP3_TANK_EMPTY_PIN   23   // W3 tank empty → GPIO 23
#define WSP3_TANK_FULL_PIN    24   // W3 tank full  → GPIO 24

// ── IPC node and valve limits ─────────────────────────────────────────────
#define IPC_MIN_NODES         1    // Minimum nodes supported
#define IPC_MAX_NODES        15    // Maximum nodes supported
#define IPC_MAX_VALVES_PER_NODE 4  // Maximum valves per node

// ── IPC pump safety thresholds ────────────────────────────────────────────
#define IPC_MIN_OPEN_VALVES   1    // Min open valves to start/keep pump running
#define IPC_VALVE_OVERLAP_MS  500  // Overlap time — next valve opens before prev closes
#define IPC_PUMP_START_DELAY_MS 2000  // Wait after first valve opens before pump starts
#define IPC_PUMP_STOP_DELAY_MS  2000  // Wait after last valve closes before pump stops

#define RTC_SDA 41
#define RTC_SCL 42

#define DISPLAY_SDA 21
#define DISPLAY_SCL 22

// ========== LoRa Settings ==========
#define LORA_FREQUENCY         865E6
#define RF_FREQUENCY           865000000
#define LORA_SYNC_WORD         0x12
#define LORA_SPREADING_FACTOR  10
#define LORA_BANDWIDTH         0
#define LORA_CODINGRATE        1
#define LORA_PREAMBLE_LENGTH   8
#define LORA_SYMBOL_TIMEOUT    0
#define LORA_FIX_LENGTH_PAYLOAD_ON false
#define LORA_IQ_INVERSION_ON   false
#define TX_OUTPUT_POWER        14
#define LORA_TX_POWER          14

#define LORA_GATEWAY_ID     255
#define LORA_BROADCAST_ID   0
#define MAX_RETRIES         3
#define LORA_MAX_RETRIES    3
#define ACK_TIMEOUT_MS      5000
#define LORA_ACK_TIMEOUT_MS 5000

// ========== WiFi Settings ==========
#define WIFI_SSID               "sekaranfarm"
#define WIFI_PASS               "welcome123"
#define WIFI_CONNECT_TIMEOUT_MS 15000

// ========== NTP Settings ==========
#define NTP_SERVER           "pool.ntp.org"
#define GMT_OFFSET_SEC       (5.5 * 3600)
#define DAYLIGHT_OFFSET_SEC  0
#define NTP_TIMEOUT_MS       10000
#define NTP_TIMEZONE_OFFSET  0

// ========== MQTT Settings ==========
#define MQTT_BROKER    "39aff691b9b5421ab98adc2addedbd83.s1.eu.hivemq.cloud"
#define MQTT_PORT      8883
#define MQTT_USE_SSL   1
#define MQTT_CLIENT_ID "irrigation_controller_001"
#define MQTT_USER      "sekaranfarm"
#define MQTT_PASS      "Welcome123"

#define MQTT_TOPIC_STATUS    "irrigation/status"
#define MQTT_TOPIC_COMMANDS  "irrigation/commands"
#define MQTT_TOPIC_TELEMETRY "irrigation/telemetry"
#define MQTT_TOPIC_ALERTS    "irrigation/alerts"

#define DEFAULT_MQTT_SERVER MQTT_BROKER
#define DEFAULT_MQTT_PORT   MQTT_PORT
#define DEFAULT_MQTT_USER   MQTT_USER
#define DEFAULT_MQTT_PASS   MQTT_PASS

// ========== PPPoS Settings ==========
// Used when ENABLE_PPPOS = 1.
// NOTE: Cannot be active at the same time as ENABLE_SMS = 1.
#define PPPOS_APN                 "airtelgprs.com"
#define PPPOS_CONNECT_TIMEOUT_MS  30000
// ========== NetworkRouter — Per-service bearer selection ==========
// NetworkRouter routes MQTT and HTTP/REST traffic to either PPPoS
// (cellular) or WiFi. Configure the PRIMARY bearer for each service
// here. The OTHER bearer is automatically used as fallback.
//
// NET_BEARER_PPPOS = 1  — cellular data via ModemPPPoS
// NET_BEARER_WIFI  = 2  — on-board WiFi via WiFiComm
//
// Examples:
//   MQTT primary PPPoS, fallback WiFi:
//     #define MQTT_PRIMARY_BEARER  NET_BEARER_PPPOS
//   HTTP primary WiFi, fallback PPPoS:
//     #define HTTP_PRIMARY_BEARER  NET_BEARER_WIFI
//
// Note: ENABLE_PPPOS and ENABLE_WIFI must be enabled for the
//       corresponding bearer to be available at runtime.
//       If a bearer is disabled (flag = 0), it will not be used
//       even if listed as primary or fallback.
#define NET_BEARER_PPPOS  1
#define NET_BEARER_WIFI   2

#define MQTT_PRIMARY_BEARER  NET_BEARER_WIFI    // Primary: WiFi, Fallback: PPPoS
#define HTTP_PRIMARY_BEARER  NET_BEARER_WIFI    // Primary: WiFi, Fallback: PPPoS

// ========== Modem / SMS Settings ==========
#define MODEM_APN       "airtelgprs.com"
#define DEFAULT_SIM_APN MODEM_APN

#define SMS_ALERT_PHONE_1         "+919944272647"
#define SMS_ALERT_PHONE_2         ""
#define DEFAULT_ADMIN_PHONE       SMS_ALERT_PHONE_1
#define DEFAULT_RECOV_TOK         "RECOVERY123"
#define DEFAULT_COUNTRY_CODE      "+91"

#define SMS_ALERT_ON_BOOT          true
#define SMS_ALERT_ON_LOW_BATTERY   true
#define SMS_ALERT_ON_SCHEDULE_FAIL true
#define SMS_ALERT_ON_COMMAND_FAIL  true
#define SMS_CHECK_INTERVAL_MS      2000
#define SMS_ALERT_RATE_LIMIT_MS    300000

// ========== BLE Settings ==========
#define BLE_DEVICE_NAME        "IrrigationController"
#define SERVICE_UUID           "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID_RX "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
#define CHARACTERISTIC_UUID_TX "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

#define BLE_MIN_CONN_INTERVAL 0x40
#define BLE_MAX_CONN_INTERVAL 0xA0
#define BLE_MTU_SIZE          512

// ========== Display Settings ==========
#define DISPLAY_REFRESH_MS 1000

// ========== Storage Settings ==========
#define MAX_SCHEDULES      10
#define MAX_SEQUENCE_STEPS 20

// ========== Timing Defaults ==========
#define PUMP_ON_LEAD_DEFAULT_MS     3000
#define PUMP_OFF_DELAY_DEFAULT_MS   3000
#define LAST_CLOSE_DELAY_MS_DEFAULT 2000
#define VALVE_OPEN_DELAY_MS         500
#define SAVE_PROGRESS_INTERVAL_MS   60000

// ========== System Settings ==========
#define SERIAL_BAUD 115200
#define QUEUE_SIZE  10

// ========== Network Settings ==========
#define MQTT_CONNECT_TIMEOUT_MS         10000
#define SMS_SEND_TIMEOUT_MS             30000
#define NETWORK_REGISTRATION_TIMEOUT_S  120
#define NETWORK_RECONNECT_INTERVAL_MS   60000

#define MQTT_MAX_RECONNECT_ATTEMPTS 5
#define MQTT_RECONNECT_DELAY_MS     5000

// ========== Structures ==========
struct SystemConfig {
  char device_id[32];
  uint32_t lora_frequency;
  uint8_t  lora_sf;
  bool     enable_sms_broadcast;
  char     timezone[32];

  String mqttServer;
  int    mqttPort;
  String mqttUser;
  String mqttPass;
  String adminPhones;
  String simApn;
  String sharedTok;
  String recoveryTok;
};

struct SeqStep {
  uint8_t  node_id;
  uint8_t  valve_id;
  uint32_t duration_ms;
};

struct Schedule {
  String   id;
  char     rec;
  int      interval;
  time_t   start_time;
  time_t   start_epoch;
  time_t   next_run_epoch;
  String   timeStr;
  uint8_t  weekday_mask;
  uint32_t ts;
  bool     enabled;
  std::vector<SeqStep> seq;
  uint32_t pump_on_before_ms;
  uint32_t pump_off_after_ms;
};

// ========== Global State (defined in MasterController.ino) ==========
extern SystemConfig       sysConfig;
extern std::vector<Schedule> schedules;
extern String             currentScheduleId;
extern std::vector<SeqStep>  seq;
extern int                currentStepIndex;
extern unsigned long      stepStartMillis;
extern bool               scheduleLoaded;
extern bool               scheduleRunning;
extern time_t             scheduleStartEpoch;
extern uint32_t           pumpOnBeforeMs;
extern uint32_t           pumpOffAfterMs;
extern uint32_t           LAST_CLOSE_DELAY_MS;
extern uint32_t           DRIFT_THRESHOLD_S;
extern uint32_t           SYNC_CHECK_INTERVAL_MS;
extern bool               ENABLE_SMS_BROADCAST;

// ========== Debug Flags ==========
// #define DEBUG_LORA
// #define DEBUG_MQTT
// #define DEBUG_SMS
// #define DEBUG_SCHEDULER

#ifdef DEBUG_LORA
  #define DEBUG_LORA_PRINT(x)   Serial.print(x)
  #define DEBUG_LORA_PRINTLN(x) Serial.println(x)
#else
  #define DEBUG_LORA_PRINT(x)
  #define DEBUG_LORA_PRINTLN(x)
#endif

#ifdef DEBUG_MQTT
  #define DEBUG_MQTT_PRINT(x)   Serial.print(x)
  #define DEBUG_MQTT_PRINTLN(x) Serial.println(x)
#else
  #define DEBUG_MQTT_PRINT(x)
  #define DEBUG_MQTT_PRINTLN(x)
#endif

#ifdef DEBUG_SMS
  #define DEBUG_SMS_PRINT(x)   Serial.print(x)
  #define DEBUG_SMS_PRINTLN(x) Serial.println(x)
#else
  #define DEBUG_SMS_PRINT(x)
  #define DEBUG_SMS_PRINTLN(x)
#endif

#ifdef DEBUG_SCHEDULER
  #define DEBUG_SCH_PRINT(x)   Serial.print(x)
  #define DEBUG_SCH_PRINTLN(x) Serial.println(x)
#else
  #define DEBUG_SCH_PRINT(x)
  #define DEBUG_SCH_PRINTLN(x)
#endif

// ========== Forward Declarations ==========
class MessageQueue;
class StorageManager;
class LoRaComm;
class Preferences;

extern MessageQueue  incomingQueue;
extern StorageManager storage;
extern LoRaComm      loraComm;
extern Preferences   prefs;

#endif // CONFIG_H
