/*
 ╔══════════════════════════════════════════════════════════╗
 ║  IV DRIP MONITORING SYSTEM — ESP32 Firmware              ║
 ║  File: esp32_monitor.ino                                 ║
 ║                                                          ║
 ║  Hardware:                                               ║
 ║    - ESP32 DevKit v1 (or equivalent)                     ║
 ║    - HX711 Load Cell Amplifier + 1kg load cell           ║
 ║    - SG90 Servo Motor (clamp actuator)                   ║
 ║    - 16x2 I2C LCD Display (PCF8574 backpack)             ║
 ║                                                          ║
 ║  Libraries (install via Arduino Library Manager):        ║
 ║    - ArduinoJson  (Benoit Blanchon)                      ║
 ║    - HX711 Arduino Library (bogde)                       ║
 ║    - ESP32Servo                                          ║
 ║    - LiquidCrystal_I2C (Frank de Brabander)              ║
 ║                                                          ║
 ║  LCD Wiring (I2C):                                       ║
 ║    VCC  → 3.3V or 5V                                     ║
 ║    GND  → GND                                            ║
 ║    SDA  → GPIO 21  (ESP32 default I2C SDA)               ║
 ║    SCL  → GPIO 22  (ESP32 default I2C SCL)               ║
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
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

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

// ── BOTTLE CAPACITY ────────────────────────────────────────
// Fetched automatically from the backend at startup based on PATIENT_ID.
// Falls back to this default if the fetch fails (no WiFi, server down, etc.)
float MAX_WEIGHT_G = 500.0;  // runtime value — updated by fetchPatientConfig()

// ── PIN DEFINITIONS ────────────────────────────────────────
#define HX711_DATA_PIN   4    // DT pin of HX711
#define HX711_CLOCK_PIN  5    // SCK pin of HX711
#define SERVO_PIN        18   // Servo signal wire
// LCD I2C uses default ESP32 I2C pins: SDA=21, SCL=22

// ── LCD SETUP ──────────────────────────────────────────────
// Common I2C addresses: 0x27 or 0x3F
// If display is blank, try 0x3F
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ── CALIBRATION ────────────────────────────────────────────
const float CALIBRATION_FACTOR = 420.0;  // adjust per your load cell

// ── TIMING ────────────────────────────────────────────────
const unsigned long POST_INTERVAL_MS = 5000;  // POST every 5 seconds
unsigned long lastPostTime = 0;

// ── OBJECTS ───────────────────────────────────────────────
HX711  scale;
Servo  clampServo;

// ── SERVO POSITIONS ───────────────────────────────────────
const int SERVO_OPEN_DEG   = 90;   // Flow allowed (clamp open)
const int SERVO_CLOSED_DEG = 0;    // Flow stopped (clamp closed)
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

// ── VOLUME LABEL ──────────────────────────────────────────
/**
 * Returns volume status label based on percentage:
 *   > 65%       → "High"
 *   > 35%–65%   → "Mid"
 *   1%–35%      → "Low"
 *   0%          → "Empty"
 */
String getVolumeLabel(float weightGrams) {
  float pct = (weightGrams / MAX_WEIGHT_G) * 100.0;
  if (pct <= 0.0)  return "Empty";
  if (pct <= 35.0) return "Low";
  if (pct <= 65.0) return "Mid";
  return "High";
}

// ── LCD UPDATE ────────────────────────────────────────────
/**
 * Line 0: Flow: Flowing  (or Stopped)
 * Line 1: Vol : High     (or Mid / Low / Empty)
 */
void updateLCD(float weightGrams) {
  // Determine flow status
  // Flow is "Stopped" if servo is closed OR bottle is empty
  bool bottleEmpty = (weightGrams <= 0.0);
  bool flowStopped = (currentServoState != "FLOW") || bottleEmpty;

  String flowLabel   = flowStopped ? "Stopped " : "Flowing ";
  String volumeLabel = getVolumeLabel(weightGrams);

  // Pad volume label to 5 chars so display stays clean
  while (volumeLabel.length() < 5) volumeLabel += " ";

  // ── Row 0: Flow status ──
  lcd.setCursor(0, 0);
  lcd.print("Flow:");
  lcd.print(flowLabel);   // "Flowing " or "Stopped "

  // ── Row 1: Volume status ──
  lcd.setCursor(0, 1);
  lcd.print("Vol :");
  lcd.print(volumeLabel);

  // Debug to serial
  Serial.print("[LCD] Flow=");
  Serial.print(flowLabel);
  Serial.print(" | Vol=");
  Serial.println(volumeLabel);
}

// ── SERVO STATE SETTER ────────────────────────────────────
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

// ── FETCH PATIENT CONFIG FROM BACKEND ─────────────────────
/**
 * Calls GET /api/patient/<PATIENT_ID> and reads the bottle_volume_ml field.
 * Your Flask route should return something like:
 *   { "patient_id": 1, "name": "John", "bottle_volume_ml": 1000, ... }
 *
 * bottle_volume_ml is treated as grams (1 ml saline ≈ 1 g).
 * Updates MAX_WEIGHT_G in place. Retries up to MAX_RETRIES times.
 */
void fetchPatientConfig() {
  const int MAX_RETRIES = 3;

  lcd.setCursor(0, 0);
  lcd.print("Fetching config ");
  lcd.setCursor(0, 1);
  lcd.print("from server...  ");

  for (int attempt = 1; attempt <= MAX_RETRIES; attempt++) {
    Serial.print("[Config] Fetching patient config, attempt ");
    Serial.println(attempt);

    HTTPClient http;
    String url = buildUrl("/api/patient/");
    url += PATIENT_ID;
    http.begin(url);
    http.addHeader("Content-Type", "application/json");

    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK) {
      String response = http.getString();

      StaticJsonDocument<512> doc;
      DeserializationError err = deserializeJson(doc, response);

      if (!err && doc.containsKey("bottle_volume_ml")) {
        float volumeMl = doc["bottle_volume_ml"].as<float>();
        if (volumeMl > 0) {
          MAX_WEIGHT_G = volumeMl;  // 1 ml ≈ 1 g for saline/water
          Serial.print("[Config] MAX_WEIGHT_G set to ");
          Serial.print(MAX_WEIGHT_G, 0);
          Serial.println(" g from server.");

          lcd.setCursor(0, 0);
          lcd.print("Bottle size set!");
          lcd.setCursor(0, 1);
          lcd.print((int)MAX_WEIGHT_G);
          lcd.print("ml            ");
          delay(1500);

          http.end();
          return;  // success — exit early
        }
      } else {
        Serial.println("[Config] JSON parse error or missing 'bottle_volume_ml' key.");
      }
    } else {
      Serial.print("[Config] GET failed. HTTP code: ");
      Serial.println(httpCode);
    }

    http.end();
    delay(1000);  // wait before retry
  }

  // All retries failed — keep the default
  Serial.print("[Config] Could not fetch config. Using default MAX_WEIGHT_G = ");
  Serial.println(MAX_WEIGHT_G, 0);

  lcd.setCursor(0, 0);
  lcd.print("Config failed!  ");
  lcd.setCursor(0, 1);
  lcd.print("Using default   ");
  delay(1500);
}

// ── WIFI SETUP ─────────────────────────────────────────────
void connectWiFi() {
  Serial.print("[WiFi] Connecting to ");
  Serial.println(WIFI_SSID);

  // Show connecting status on LCD
  lcd.setCursor(0, 0);
  lcd.print("Connecting WiFi ");
  lcd.setCursor(0, 1);
  lcd.print("Please wait...  ");

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

    lcd.setCursor(0, 0);
    lcd.print("WiFi Connected! ");
    lcd.setCursor(0, 1);
    lcd.print(WiFi.localIP());
    delay(2000);
  } else {
    Serial.println("\n[WiFi] FAILED — check credentials. Continuing offline.");
    lcd.setCursor(0, 0);
    lcd.print("WiFi FAILED!    ");
    lcd.setCursor(0, 1);
    lcd.print("Offline mode... ");
    delay(2000);
  }
}

// ── POST SENSOR DATA TO FLASK ──────────────────────────────
void postSensorData(float weightGrams) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[HTTP] Not connected — skipping POST");
    return;
  }

  HTTPClient http;
  String url = buildUrl("/api/sensor-data");
  http.begin(url);
  http.addHeader("Content-Type", "application/json");

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

  // LCD init
  Wire.begin(21, 22);       // SDA=21, SCL=22 (ESP32 defaults)
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("IV Drip Monitor ");
  lcd.setCursor(0, 1);
  lcd.print("Initializing... ");
  delay(1500);

  // Servo init
  ESP32PWM::allocateTimer(0);
  clampServo.setPeriodHertz(50);
  clampServo.attach(SERVO_PIN, 500, 2400);
  clampServo.write(SERVO_CLOSED_DEG);     // Start closed (safe default)
  Serial.println("[Servo] Initialized → CLOSED");

  // HX711 init
  scale.begin(HX711_DATA_PIN, HX711_CLOCK_PIN);
  scale.set_scale(CALIBRATION_FACTOR);
  scale.tare();
  Serial.println("[Scale] HX711 ready, tared to zero.");

  // WiFi
  connectWiFi();

  // Fetch bottle size from backend (sets MAX_WEIGHT_G automatically)
  if (WiFi.status() == WL_CONNECTED) {
    fetchPatientConfig();
  } else {
    Serial.println("[Config] Skipping config fetch — no WiFi.");
  }

  // Show initial state on LCD
  lcd.clear();
  updateLCD(0);
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

    float weightGrams = 0;
    if (scale.is_ready()) {
      weightGrams = scale.get_units(5);
      if (weightGrams < 0) weightGrams = 0;

      Serial.print("[Scale] Weight = ");
      Serial.print(weightGrams, 2);
      Serial.println(" g");
    } else {
      Serial.println("[Scale] HX711 not ready — using last reading");
    }

    // Send to backend, get servo command
    postSensorData(weightGrams);

    // Update LCD with latest weight + servo state
    updateLCD(weightGrams);
  }

  yield();
}

/*
 ════════════════════════════════════════════════════
 LCD DISPLAY LAYOUT (16x2)
 ════════════════════════════════════════════════════

  Row 0: "Flow:Flowing    "  ← when servo = FLOW and liquid present
          "Flow:Stopped    "  ← when servo = STOP or bottle empty

  Row 1: "Vol :High       "  ← volume > 65%
          "Vol :Mid        "  ← volume 35%–65%
          "Vol :Low        "  ← volume 1%–35%
          "Vol :Empty      "  ← volume = 0

 ════════════════════════════════════════════════════
 VOLUME THRESHOLDS
 ════════════════════════════════════════════════════

  > 65% of MAX_WEIGHT_G  →  High
  > 35% of MAX_WEIGHT_G  →  Mid
    1% to 35%            →  Low
    = 0                  →  Empty

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

    LCD I2C → ESP32:
      VCC    → 3.3V or 5V
      GND    → GND
      SDA    → GPIO 21
      SCL    → GPIO 22

 2. LCD I2C ADDRESS:
    Default is 0x27. If display is blank/garbled, change to 0x3F:
      LiquidCrystal_I2C lcd(0x3F, 16, 2);
    To scan for the address, upload an I2C scanner sketch first.

 3. SET MAX_WEIGHT_G:
    Weigh your full IV bottle/bag and set MAX_WEIGHT_G to that value.
    Example: 500ml saline = ~500g → MAX_WEIGHT_G = 500.0

 4. CALIBRATION:
    a. Upload with any CALIBRATION_FACTOR.
    b. Open Serial Monitor at 115200 baud.
    c. Place a known weight (e.g. 200g) on the load cell.
    d. Divide known weight by reading to get factor.
    e. Update CALIBRATION_FACTOR and re-flash.

 5. FLASK ENDPOINT NEEDED:
    The ESP32 calls GET /api/patient/<patient_id> on startup.
    Your Flask route must return JSON with at least:
      { "bottle_volume_ml": 500 }   ← or 1000 for 1L, etc.

    Example Flask route to add:
      @app.route('/api/patient/<int:pid>', methods=['GET'])
      def get_patient(pid):
          patient = Patient.query.get(pid)
          return jsonify({
              "patient_id": patient.id,
              "name": patient.name,
              "bottle_volume_ml": patient.bottle_volume_ml
          })

    Make sure your Patient model/table has a bottle_volume_ml column
    (integer, values like 500 or 1000).

 6. LIBRARIES INSTALL:
    Sketch > Include Library > Manage Libraries…
    Search and install:
      - ArduinoJson (latest v6.x)
      - HX711 Arduino Library (by bogde)
      - ESP32Servo (by Kevin Harrington)
      - LiquidCrystal_I2C (by Frank de Brabander)

 ════════════════════════════════════════════════════
*/
