/*
 ╔══════════════════════════════════════════════════════════╗
 ║  IV DRIP MONITORING SYSTEM — ESP32 Firmware              ║
 ║  File: esp32_monitor.ino                                 ║
 ║                                                          ║
 ║  Hardware:                                               ║
 ║    - ESP32 DevKit v1 (or equivalent)                     ║
 ║    - HX711 Load Cell Amplifier + 1kg load cell           ║
 ║    - SG90 Servo Motor (clamp actuator)                   ║
 ║    - Optional: 0.96" OLED I2C display (SSD1306)          ║
 ║                                                          ║
 ║  Libraries (install via Arduino Library Manager):        ║
 ║    - ArduinoJson  (Benoit Blanchon)                      ║
 ║    - HX711 Arduino Library (bogde)                       ║
 ║    - ESP32Servo                                          ║
 ║    - Adafruit SSD1306 (optional display)                 ║
 ║                                                          ║
 ║  Flash:                                                  ║
 ║    Tools > Board > "ESP32 Dev Module"                    ║
 ║    Upload Speed: 115200                                  ║
 ╚══════════════════════════════════════════════════════════╝
*/

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <HX711.h>
#include <ESP32Servo.h>

// ── CONFIGURATION ─────────────────────────────────────────
// Update these before flashing!

const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// Backend server IP — run `ipconfig` or `ifconfig` on your PC
const char* SERVER_IP     = "192.168.1.100";
const int   SERVER_PORT   = 5000;

// This ESP32's assigned patient ID in the database
const int   PATIENT_ID    = 1;
const char* DEVICE_ID     = "ESP32-001";

// ── PIN DEFINITIONS ────────────────────────────────────────
#define HX711_DATA_PIN   4    // DT pin of HX711
#define HX711_CLOCK_PIN  5    // SCK pin of HX711
#define SERVO_PIN        18   // Servo signal wire

// ── CALIBRATION ────────────────────────────────────────────
// Run calibration sketch once to find your load cell's factor
const float CALIBRATION_FACTOR = 420.0;  // adjust per your load cell

// ── TIMING ────────────────────────────────────────────────
const unsigned long POST_INTERVAL_MS = 5000;  // POST every 5 seconds
unsigned long lastPostTime = 0;

// ── OBJECTS ───────────────────────────────────────────────
HX711  scale;
Servo  clampServo;

// ── SERVO POSITIONS ───────────────────────────────────────
const int SERVO_OPEN_DEG  = 90;   // Flow allowed (clamp open)
const int SERVO_CLOSED_DEG = 0;   // Flow stopped (clamp closed)
String    currentServoState = "STOP";

// ── HELPERS ───────────────────────────────────────────────
String buildUrl(const char* path) {
  String url = "http://";
  url += SERVER_IP;
  url += ":";
  url += SERVER_PORT;
  url += path;
  return url;
}

void setServoState(const String& state) {
  if (state == currentServoState) return;  // no change, skip

  if (state == "FLOW") {
    clampServo.write(SERVO_OPEN_DEG);
    Serial.println("[Servo] → OPEN (FLOW)");
  } else {
    clampServo.write(SERVO_CLOSED_DEG);
    Serial.println("[Servo] → CLOSED (STOP)");
  }
  currentServoState = state;
}

// ── WIFI SETUP ─────────────────────────────────────────────
void connectWiFi() {
  Serial.print("[WiFi] Connecting to ");
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WiFi] Connected!");
    Serial.print("[WiFi] IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n[WiFi] FAILED — check credentials. Continuing offline.");
  }
}

// ── POST SENSOR DATA TO FLASK ──────────────────────────────
/**
 * Sends current weight reading to backend via HTTP POST.
 * Receives servo state in the JSON response.
 *
 * Endpoint: POST /api/sensor-data
 * Body:     { patient_id, weight_g, flow_rate, device_id }
 * Response: { SERVO_STATE: "FLOW"|"STOP", ... }
 */
void postSensorData(float weightGrams) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[HTTP] Not connected — skipping POST");
    return;
  }

  HTTPClient http;
  String url = buildUrl("/api/sensor-data");
  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  // Build JSON payload
  StaticJsonDocument<256> doc;
  doc["patient_id"] = PATIENT_ID;
  doc["weight_g"]   = (int)weightGrams;
  doc["flow_rate"]  = 42;      // ← TODO: compute from weight delta / time
  doc["device_id"]  = DEVICE_ID;

  String payload;
  serializeJson(doc, payload);

  Serial.print("[HTTP] POST → weight=");
  Serial.print(weightGrams, 1);
  Serial.println("g");

  int httpCode = http.POST(payload);

  if (httpCode == HTTP_CODE_OK) {
    String response = http.getString();

    // Parse response to get servo command
    StaticJsonDocument<256> resp;
    DeserializationError err = deserializeJson(resp, response);

    if (!err) {
      String servoCmd = resp["SERVO_STATE"].as<String>();
      Serial.print("[HTTP] Server says SERVO_STATE = ");
      Serial.println(servoCmd);
      setServoState(servoCmd);
    } else {
      Serial.println("[HTTP] JSON parse error in response");
    }
  } else {
    Serial.print("[HTTP] POST failed. HTTP code: ");
    Serial.println(httpCode);
  }

  http.end();
}

// ── SETUP ──────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("══════════════════════════════════");
  Serial.println("  IV Drip Monitor — ESP32 Firmware");
  Serial.println("══════════════════════════════════");

  // Servo init
  ESP32PWM::allocateTimer(0);
  clampServo.setPeriodHertz(50);          // Standard 50Hz servo
  clampServo.attach(SERVO_PIN, 500, 2400);
  clampServo.write(SERVO_CLOSED_DEG);     // Start closed (safe default)
  Serial.println("[Servo] Initialized → CLOSED");

  // HX711 init
  scale.begin(HX711_DATA_PIN, HX711_CLOCK_PIN);
  scale.set_scale(CALIBRATION_FACTOR);
  scale.tare();  // Zero out with empty bag holder
  Serial.println("[Scale] HX711 ready, tared to zero.");

  // WiFi
  connectWiFi();
}

// ── MAIN LOOP (NON-BLOCKING) ───────────────────────────────
void loop() {
  unsigned long now = millis();

  // Reconnect WiFi if dropped
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] Reconnecting…");
    WiFi.reconnect();
    delay(3000);
    return;
  }

  // Non-blocking interval check
  if (now - lastPostTime >= POST_INTERVAL_MS) {
    lastPostTime = now;

    // Read weight from HX711
    // Average of 5 readings for stability
    float weightGrams = 0;
    if (scale.is_ready()) {
      weightGrams = scale.get_units(5);  // average of 5
      if (weightGrams < 0) weightGrams = 0;  // clamp negatives

      Serial.print("[Scale] Weight = ");
      Serial.print(weightGrams, 2);
      Serial.println(" g");
    } else {
      Serial.println("[Scale] HX711 not ready — using last reading");
    }

    // Send to backend, get servo command
    postSensorData(weightGrams);
  }

  // Small yield to prevent watchdog trigger
  yield();
}

/*
 ════════════════════════════════════════════════════
 FIRST-TIME SETUP GUIDE
 ════════════════════════════════════════════════════

 1. WIRING:
    HX711 → ESP32:
      VCC   → 3.3V
      GND   → GND
      DT    → GPIO 4
      SCK   → GPIO 5

    Load Cell → HX711:
      Red    → E+
      Black  → E-
      White  → A-
      Green  → A+

    Servo → ESP32:
      Signal → GPIO 18
      VCC    → 5V (external, not from ESP32 3.3V!)
      GND    → GND (shared with ESP32)

 2. CALIBRATION:
    a. Upload this sketch once with any CALIBRATION_FACTOR.
    b. Open Serial Monitor at 115200 baud.
    c. Place a known weight (e.g. 200g) on the load cell.
    d. Note the reading. Divide known weight / reading to get factor.
    e. Update CALIBRATION_FACTOR and re-flash.

 3. ARDUINO IDE SETTINGS:
    Board: "ESP32 Dev Module"
    Upload Speed: 115200
    CPU Frequency: 240 MHz
    Partition: Default 4MB

 4. LIBRARIES INSTALL:
    Sketch > Include Library > Manage Libraries…
    Search and install:
      - ArduinoJson (latest v6.x)
      - HX711 Arduino Library (by bogde)
      - ESP32Servo (by Kevin Harrington)

 5. TESTING WITHOUT ESP32:
    Run this Python script to simulate sensor data:
    
    import requests, time
    while True:
        r = requests.post("http://localhost:5000/api/sensor-data", json={
            "patient_id": 1,
            "weight_g": 320,
            "flow_rate": 42,
            "device_id": "ESP32-001"
        })
        print(r.json())
        time.sleep(5)

 ════════════════════════════════════════════════════
*/
