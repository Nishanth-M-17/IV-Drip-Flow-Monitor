#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>
#include <HTTPClient.h>

const char* WIFI_SSID     = "YOUR_SSID";
const char* WIFI_PASSWORD = "YOUR_PASSWORD";

const char* MQTT_BROKER   = "192.168.1.100";
const int   MQTT_PORT      = 1883;
const char* MQTT_USER      = "";
const char* MQTT_PASS      = "";

const char* BACKEND_URL   = "http://192.168.1.100:8080/api/sensor-data";

const char* ROOM_ID       = "201";
const float TOTAL_VOLUME  = 500.0;

#define IR_SENSOR_PIN  14
#define SERVO_PIN      13

volatile uint32_t dropCount       = 0;
volatile uint32_t lastDropMicros  = 0;
uint32_t          lastPublishMs   = 0;
const uint32_t    PUBLISH_INTERVAL_MS = 1000;

const float ML_PER_DROP = 0.05;
float currentVolume     = TOTAL_VOLUME;

Servo valveServo;
bool  valveLocked = false;

WiFiClient  wifiClient;
PubSubClient mqttClient(wifiClient);

char mqttPubTopic[64];
char mqttSubTopic[64];

void IRAM
