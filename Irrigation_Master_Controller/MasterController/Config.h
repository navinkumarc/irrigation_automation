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
#define ENABLE_LORA    1    // LoRa radio hardware
#define ENABLE_BLE     1    // Bluetooth Low Energy
#define ENABLE_WIFI    1    // WiFi (required by MQTT and HTTP REST API)
#define ENABLE_SMS     1    // SMS via modem AT commands — set 0 if ENABLE_PPPOS = 1
#define ENABLE_MQTT    1    // MQTT via WiFi or PPPoS
#define ENABLE_HTTP    1    // REST API via WiFi WebServer
#define ENABLE_DISPLAY 1    // OLED/LCD display
#define ENABLE_RTC     1    // Real-time clock (DS3231)
#define ENABLE_PPPOS   0    // Cellular data via PPPoS — set 0 if ENABLE_SMS = 1

// Compile-time enforcement of the SMS / PPPoS mutual-exclusion rule
#if ENABLE_SMS && ENABLE_PPPOS
  #error "ENABLE_SMS and ENABLE_PPPOS cannot both be 1. The modem supports only one mode at a time."
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
#define ENABLE_BLE_COMMANDS  1
#define ENABLE_WIFI_COMMANDS 1
#define ENABLE_HTTP_COMMANDS 1

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

#define PUMP_PIN         25
#define PUMP_ACTIVE_HIGH true

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

#define MQTT_PRIMARY_BEARER  NET_BEARER_PPPOS   // Primary: cellular, Fallback: WiFi
#define HTTP_PRIMARY_BEARER  NET_BEARER_PPPOS   // Primary: cellular, Fallback: WiFi

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
