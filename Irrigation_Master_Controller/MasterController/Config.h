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

// ── Heltec WiFi LoRa 32 V3 (ESP32-S3) — GPIO pin assignments ─────────────
//
// Verified from official Heltec V3 datasheet (HTIT-WB32LA_V3) and schematic.
// Only GPIO physically present on Header J2 or Header J3 are used.
//
// ── Header J3 (left side) ────────────────────────────────────────────────
//  J3-1  GND          J3-10 GPIO39  J3-2  3V3
//  J3-3  3V3          J3-11 GPIO38  J3-4  GPIO37 ← ADC_Ctrl (INTERNAL)
//  J3-5  GPIO46 ←MODEM TX  J3-6  GPIO45 ←MODEM RX
//  J3-7  GPIO42 ←RTC SCL  J3-8  GPIO41 ←RTC SDA
//  J3-9  GPIO40        J3-12 GPIO1  ←VBAT ADC (INTERNAL)
//  J3-13 GPIO2         J3-14 GPIO3
//  J3-15 GPIO4 ←MODEM PWRKEY   J3-16 GPIO5
//  J3-17 GPIO6         J3-18 GPIO7
//
// ── Header J2 (right side) ───────────────────────────────────────────────
//  J2-1  GND           J2-2  5V
//  J2-3  Ve(3.3V out)  J2-4  Ve(3.3V out)
//  J2-5  GPIO44 ←Serial RX    J2-6  GPIO43 ←Serial TX
//  J2-7  RST           J2-8  GPIO0 ←BOOT button
//  J2-9  GPIO36 ←Vext_Ctrl(INTERNAL)  J2-10 GPIO35 ←LED(INTERNAL)
//  J2-11 GPIO34        J2-12 GPIO33
//  J2-13 GPIO47        J2-14 GPIO48
//  J2-15 GPIO26        J2-16 GPIO21 ←OLED RST
//  J2-17 GPIO20 ←USB D+       J2-18 GPIO19 ←USB D-
//
// ── Internal only (NOT on any header) ────────────────────────────────────
//  GPIO8-14   LoRa SX1262 SPI
//  GPIO17-18  OLED I2C (SDA/SCL)
//  GPIO15     Modem RESET
//
// ── Pump relay outputs (connect relay module IN → these pins) ─────────────
//  G1 irrigation pump → J3-16 GPIO5   (relay IN, active HIGH)
//  G2 irrigation pump → J3-17 GPIO6   (relay IN, active HIGH)
//  W1 well pump       → J3-18 GPIO7   (relay IN, active HIGH)
//  W2 well pump       → J3-14 GPIO3   (relay IN, active HIGH)
//
// ── Tank level sensors (NC float switch → INPUT_PULLUP, LOW = triggered) ──
//  W1 tank empty      → J2-13 GPIO47
//  W1 tank full       → J2-14 GPIO48
//  W2 tank empty      → J3-9  GPIO40
//  W2 tank full       → J3-10 GPIO39
//
// ── Spare GPIO (free for future use) ─────────────────────────────────────
//  GPIO2  (J3-13)   GPIO26 (J2-15)
//  GPIO33 (J2-12)   GPIO34 (J2-11)   GPIO38 (J3-11)

// ── Pin assignments — grouped by function, sequential on same header side ──
//
//  J3 side (left header) — ALL well pump wiring here:
//  ┌─────────┬────────┬───────────────────────────────────────┐
//  │ J3 Pin  │ GPIO   │ Function                              │
//  ├─────────┼────────┼───────────────────────────────────────┤
//  │ J3-18   │ GPIO7  │ W1 well pump relay (OUTPUT)           │
//  │ J3-17   │ GPIO6  │ W1 tank empty sensor (INPUT_PULLUP)   │
//  │ J3-16   │ GPIO5  │ W1 tank full sensor  (INPUT_PULLUP)   │
//  │ J3-15   │ GPIO4  │ ⚠ MODEM_PWRKEY — skip                │
//  │ J3-14   │ GPIO3  │ W2 well pump relay (OUTPUT)           │
//  │ J3-13   │ GPIO2  │ W2 tank empty sensor (INPUT_PULLUP)   │
//  │ J3-12   │ GPIO1  │ ⚠ VBAT ADC — skip                    │
//  │ J3-11   │ GPIO38 │ W2 tank full sensor  (INPUT_PULLUP)   │
//  └─────────┴────────┴───────────────────────────────────────┘
//
//  J2 side (right header) — ALL irrigation pump wiring here:
//  ┌─────────┬────────┬───────────────────────────────────────┐
//  │ J2 Pin  │ GPIO   │ Function                              │
//  ├─────────┼────────┼───────────────────────────────────────┤
//  │ J2-13   │ GPIO47 │ G1 irrigation pump relay (OUTPUT)     │
//  │ J2-14   │ GPIO48 │ G2 irrigation pump relay (OUTPUT)     │
//  │ J2-15   │ GPIO26 │ spare                                 │
//  │ J2-12   │ GPIO33 │ spare                                 │
//  │ J2-11   │ GPIO34 │ spare                                 │
//  └─────────┴────────┴───────────────────────────────────────┘
//
//  Also spare on J3: GPIO39 (J3-10), GPIO40 (J3-9)
//
// Sensor wiring (NC float switch):
//   GPIO + INPUT_PULLUP → float switch NC → GND
//   LOW = switch open = level condition met (empty or full)

// ── Irrigation Pump Controller (IPC) — J2 side ────────────────────────────
#define PUMP_PIN         47    // legacy alias — same as IPC_PIN
#define PUMP_ACTIVE_HIGH true
#define IPC_PIN          47    // G1 → J2-13 GPIO47
#define IPC_ACTIVE_HIGH  true
#define IPC2_PIN         48    // G2 → J2-14 GPIO48
#define IPC2_ACTIVE_HIGH true

// ── Water Source Pump Controller (WSPC) — J3 side ─────────────────────────
#define WSP_PIN          7     // W1 relay    → J3-18 GPIO7
#define WSP_ACTIVE_HIGH  true
#define WSP2_PIN         3     // W2 relay    → J3-14 GPIO3
#define WSP2_ACTIVE_HIGH true

// ── Tank level sensors — J3 side (adjacent to relay pins) ─────────────────
// Set to 0 to disable (pump runs in MANUAL/SCHEDULE without sensors).
#define WSP_TANK_EMPTY_PIN    6    // W1 tank empty → J3-17 GPIO6
#define WSP_TANK_FULL_PIN     5    // W1 tank full  → J3-16 GPIO5
#define WSP2_TANK_EMPTY_PIN   2    // W2 tank empty → J3-13 GPIO2
#define WSP2_TANK_FULL_PIN    38   // W2 tank full  → J3-11 GPIO38

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
