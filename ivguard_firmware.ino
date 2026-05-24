/*
 * IVGuard — ESP32-S3 Firmware
 * ─────────────────────────────────────────────────────────────
 * Hardware:
 *   - IR break-beam sensor on GPIO 14 (drip counter)
 *   - Servo motor on GPIO 13 (valve: 0° = open, 90° = locked)
 *   - WiFi via built-in radio
 *
 * Protocol:
 *   Publishes JSON sensor data to MQTT: ivguard/{ROOM_ID}/sensor
 *   Subscribes to valve commands:       ivguard/{ROOM_ID}/valve
 *   Also POSTs to REST as fallback:     POST /api/sensor-data
 *
 * Libraries required (install via Arduino Library Manager):
 *   - PubSubClient  (Nick O'Leary)
 *   - ArduinoJson   (Benoit Blanchon)
 *   - ESP32Servo    (Kevin Harrington)
 *   - HTTPClient    (built-in ESP32 Arduino core)
 * ─────────────────────────────────────────────────────────────
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>
#include <HTTPClient.h>

/* ── Configuration ─────────────────────────────────────────── */
const char* WIFI_SSID     = "YOUR_SSID";
const char* WIFI_PASSWORD = "YOUR_PASSWORD";

const char* MQTT_BROKER   = "192.168.1.100";   // IP of your MQTT broker
const int   MQTT_PORT     = 1883;
const char* MQTT_USER     = "";                 // leave empty if no auth
const char* MQTT_PASS     = "";

const char* BACKEND_URL   = "http://192.168.1.100:8080/api/sensor-data";

const char* ROOM_ID       = "201";
const float TOTAL_VOLUME  = 500.0;              // ml — full bottle

/* ── Pin Definitions ────────────────────────────────────────── */
#define IR_SENSOR_PIN  14    // IR break-beam OUT → GPIO 14 (INPUT_PULLUP)
#define SERVO_PIN      13    // Servo signal wire → GPIO 13

/* ── Drip Counting ──────────────────────────────────────────── */
volatile uint32_t dropCount       = 0;
volatile uint32_t lastDropMicros  = 0;
uint32_t          lastPublishMs   = 0;
const uint32_t    PUBLISH_INTERVAL_MS = 1000;   // publish every 1 second

/* ── Volume Model ───────────────────────────────────────────── */
// Simple model: 1 drop ≈ 0.05 ml  (standard IV set: 20 drops/ml)
const float ML_PER_DROP = 0.05;
float currentVolume     = TOTAL_VOLUME;

/* ── Servo / Valve ──────────────────────────────────────────── */
Servo valveServo;
bool  valveLocked = false;

/* ── MQTT client ────────────────────────────────────────────── */
WiFiClient   wifiClient;
PubSubClient mqttClient(wifiClient);

char mqttPubTopic[64];   // ivguard/{ROOM_ID}/sensor
char mqttSubTopic[64];   // ivguard/{ROOM_ID}/valve

/* ── ISR: count each drip ────────────────────────────────────── */
void IRAM_ATTR onDrop() {
  uint32_t now = micros();
  // debounce: ignore interrupts < 50 ms apart
  if ((now - lastDropMicros) > 50000) {
    dropCount++;
    lastDropMicros = now;
  }
}

/* ── WiFi ────────────────────────────────────────────────────── */
void connectWifi() {
  Serial.printf("[WiFi] Connecting to %s", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); Serial.print(".");
  }
  Serial.printf("\n[WiFi] Connected — IP: %s\n", WiFi.localIP().toString().c_str());
}

/* ── MQTT callback: handle incoming valve commands ──────────── */
void onMqttMessage(char* topic, byte* payload, unsigned int length) {
  char cmd[16] = {0};
  memcpy(cmd, payload, min((unsigned int)15, length));
  Serial.printf("[MQTT] ← %s : %s\n", topic, cmd);

  if (strncmp(cmd, "LOCK", 4) == 0) {
    lockValve();
  } else if (strncmp(cmd, "UNLOCK", 6) == 0) {
    unlockValve();
  }
}

/* ── MQTT connect / reconnect ───────────────────────────────── */
void connectMqtt() {
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(onMqttMessage);

  char clientId[32];
  snprintf(clientId, sizeof(clientId), "ivguard-esp32-%s", ROOM_ID);

  while (!mqttClient.connected()) {
    Serial.printf("[MQTT] Connecting to %s:%d …\n", MQTT_BROKER, MQTT_PORT);
    bool ok = (strlen(MQTT_USER) > 0)
      ? mqttClient.connect(clientId, MQTT_USER, MQTT_PASS)
      : mqttClient.connect(clientId);

    if (ok) {
      Serial.println("[MQTT] Connected");
      mqttClient.subscribe(mqttSubTopic, 2);
      Serial.printf("[MQTT] Subscribed to %s\n", mqttSubTopic);
    } else {
      Serial.printf("[MQTT] Failed (rc=%d) — retry in 5s\n", mqttClient.state());
      delay(5000);
    }
  }
}

/* ── Valve control ──────────────────────────────────────────── */
void lockValve() {
  valveServo.write(90);   // 90° = fully closed
  valveLocked = true;
  Serial.println("[VALVE] LOCKED (90°)");
}

void unlockValve() {
  valveServo.write(0);    // 0° = fully open
  valveLocked = false;
  Serial.println("[VALVE] UNLOCKED (0°)");
}

/* ── Publish sensor data ─────────────────────────────────────── */
void publishSensorData(float dpm) {
  /* Update volume estimate */
  currentVolume = max(0.0f, currentVolume - (dpm / 60.0f * ML_PER_DROP * (PUBLISH_INTERVAL_MS / 1000.0f)));

  /* Build JSON */
  StaticJsonDocument<256> doc;
  doc["roomId"]        = ROOM_ID;
  doc["currentVolume"] = currentVolume;
  doc["totalVolume"]   = TOTAL_VOLUME;
  doc["dpm"]           = dpm;
  doc["timestamp"]     = millis();

  char buf[256];
  serializeJson(doc, buf);

  /* 1. Try MQTT first */
  if (mqttClient.connected()) {
    mqttClient.publish(mqttPubTopic, buf, false);
    Serial.printf("[MQTT] → %s : %s\n", mqttPubTopic, buf);
  } else {
    /* 2. Fallback: HTTP POST */
    if (WiFi.status() == WL_CONNECTED) {
      HTTPClient http;
      http.begin(BACKEND_URL);
      http.addHeader("Content-Type", "application/json");
      int code = http.POST(String(buf));
      Serial.printf("[HTTP] POST %s → %d\n", BACKEND_URL, code);
      http.end();
    }
  }
}

/* ════════════════════════════════════════════════════════════
   SETUP
════════════════════════════════════════════════════════════ */
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== IVGuard ESP32-S3 Firmware ===");

  /* Build topic strings */
  snprintf(mqttPubTopic, sizeof(mqttPubTopic), "ivguard/%s/sensor", ROOM_ID);
  snprintf(mqttSubTopic, sizeof(mqttSubTopic), "ivguard/%s/valve",  ROOM_ID);

  /* Servo */
  valveServo.attach(SERVO_PIN, 500, 2400);  // min/max pulse µs
  unlockValve();

  /* IR sensor */
  pinMode(IR_SENSOR_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(IR_SENSOR_PIN), onDrop, FALLING);

  /* Network */
  connectWifi();
  connectMqtt();

  Serial.println("[BOOT] Ready. Counting drips…");
}

/* ════════════════════════════════════════════════════════════
   LOOP
════════════════════════════════════════════════════════════ */
void loop() {
  /* Keep MQTT alive */
  if (!mqttClient.connected()) connectMqtt();
  mqttClient.loop();

  uint32_t now = millis();

  /* Publish every PUBLISH_INTERVAL_MS */
  if (now - lastPublishMs >= PUBLISH_INTERVAL_MS) {
    lastPublishMs = now;

    /* Snapshot and reset drop counter atomically */
    noInterrupts();
    uint32_t drops = dropCount;
    dropCount = 0;
    interrupts();

    /* Convert drops in last second → DPM */
    float dpm = (float)drops * 60.0f;

    if (!valveLocked) {
      publishSensorData(dpm);
    }
  }

  delay(10);
}
