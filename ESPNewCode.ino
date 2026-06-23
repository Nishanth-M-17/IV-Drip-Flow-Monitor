#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <HX711.h>
#include <ESP32Servo.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// #wifi
const char* WIFI_SSID     = "Your_WiFi_Name";
const char* WIFI_PASSWORD = "Your_WiFi_Password";

// #server
const char* SERVER_IP     = "192.168.x.x";
const int   SERVER_PORT   = 5000;

const int   PATIENT_ID    = 1;
const char* DEVICE_ID     = "ESP32-001";

float MAX_WEIGHT_G = 500.0;

// #loadcell
#define HX711_DATA_PIN   4
#define HX711_CLOCK_PIN  5
// #servo
#define SERVO_PIN        18

// #lcd
LiquidCrystal_I2C lcd(0x27, 16, 2);

// #loadcell
const float CALIBRATION_FACTOR = 420.0;

const unsigned long POST_INTERVAL_MS = 5000;
unsigned long lastPostTime = 0;

// #loadcell
HX711  scale;
// #servo
Servo  clampServo;

// #servo
const int SERVO_OPEN_DEG   = 90;
const int SERVO_CLOSED_DEG = 0;
String    currentServoState = "STOP";

float lastWeight = 0;
unsigned long lastTime = 0;

String buildUrl(const char* path) {
  String url = "http://";
  url += SERVER_IP;
  url += ":";
  url += SERVER_PORT;
  url += path;
  return url;
}

String getVolumeLabel(float weightGrams) {
  float pct = (weightGrams / MAX_WEIGHT_G) * 100.0;
  if (pct <= 0.0)  return "Empty";
  if (pct <= 35.0) return "Low";
  if (pct <= 65.0) return "Mid";
  return "High";
}

// #lcd
void updateLCD(float weightGrams) {
  bool bottleEmpty = (weightGrams <= 0.0);
  bool flowStopped = (currentServoState != "FLOW") || bottleEmpty;

  String flowLabel   = flowStopped ? "Stopped " : "Flowing ";
  String volumeLabel = getVolumeLabel(weightGrams);

  while (volumeLabel.length() < 5) volumeLabel += " ";

  lcd.setCursor(0, 0);
  lcd.print("Flow:");
  lcd.print(flowLabel);

  lcd.setCursor(0, 1);
  lcd.print("Vol :");
  lcd.print(volumeLabel);

  Serial.print("[LCD] Flow=");
  Serial.print(flowLabel);
  Serial.print(" | Vol=");
  Serial.println(volumeLabel);
}

// #servo
// Physically actuates the clamp: rotates to SERVO_OPEN_DEG to release
// the IV tube (flow allowed) or to SERVO_CLOSED_DEG to press/pinch the
// tube shut (flow stopped). Driven only by the SERVO_STATE value the
// backend returns in the POST response below.
void setServoState(const String& state) {
  if (state == currentServoState) return;

  if (state == "FLOW") {
    clampServo.write(SERVO_OPEN_DEG);
    Serial.println("[Servo] -> OPEN (FLOW)");
  } else {
    clampServo.write(SERVO_CLOSED_DEG);
    Serial.println("[Servo] -> CLOSED (STOP)");
  }
  currentServoState = state;
}

// #server
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
          MAX_WEIGHT_G = volumeMl;
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
          return;
        }
      } else {
        Serial.println("[Config] JSON parse error or missing 'bottle_volume_ml' key.");
      }
    } else {
      Serial.print("[Config] GET failed. HTTP code: ");
      Serial.println(httpCode);
    }

    http.end();
    delay(1000);
  }

  Serial.print("[Config] Could not fetch config. Using default MAX_WEIGHT_G = ");
  Serial.println(MAX_WEIGHT_G, 0);

  lcd.setCursor(0, 0);
  lcd.print("Config failed!  ");
  lcd.setCursor(0, 1);
  lcd.print("Using default   ");
  delay(1500);
}

// #wifi
void connectWiFi() {
  Serial.print("[WiFi] Connecting to ");
  Serial.println(WIFI_SSID);

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
    Serial.println("\n[WiFi] FAILED - check credentials. Continuing offline.");
    lcd.setCursor(0, 0);
    lcd.print("WiFi FAILED!    ");
    lcd.setCursor(0, 1);
    lcd.print("Offline mode... ");
    delay(2000);
  }
}

// #server
// Sends the real load-cell weight to Flask every POST_INTERVAL_MS, along
// with a flow rate computed from the actual weight change over time
// (no hardcoded values). The backend's JSON response carries SERVO_STATE,
// which reflects whatever Allow Flow / Stop Flow button state was last
// set on the web dashboard -- setServoState() then physically moves
// the servo to match it.
void postSensorData(float weightGrams) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[HTTP] Not connected - skipping POST");
    return;
  }

  unsigned long currentTime = millis();
  float weightDiff = lastWeight - weightGrams;
  float timeDiff = (currentTime - lastTime) / 1000.0;
  float flowRate = (timeDiff > 0) ? (weightDiff / timeDiff) : 0;

  lastWeight = weightGrams;
  lastTime = currentTime;

  HTTPClient http;
  String url = buildUrl("/api/sensor-data");
  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<256> doc;
  doc["patient_id"] = PATIENT_ID;
  doc["weight_g"]   = (int)weightGrams;
  doc["flow_rate"]  = flowRate;
  doc["device_id"]  = DEVICE_ID;

  String payload;
  serializeJson(doc, payload);

  Serial.print("[HTTP] POST -> weight=");
  Serial.print(weightGrams, 1);
  Serial.print("g flow_rate=");
  Serial.print(flowRate, 2);
  Serial.println("g/s");

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

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("IV Drip Monitor - ESP32 Firmware");

  // #lcd
  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("IV Drip Monitor ");
  lcd.setCursor(0, 1);
  lcd.print("Initializing... ");
  delay(1500);

  // #servo
  ESP32PWM::allocateTimer(0);
  clampServo.setPeriodHertz(50);
  clampServo.attach(SERVO_PIN, 500, 2400);
  clampServo.write(SERVO_CLOSED_DEG);
  Serial.println("[Servo] Initialized -> CLOSED");

  // #loadcell
  scale.begin(HX711_DATA_PIN, HX711_CLOCK_PIN);
  scale.set_scale(CALIBRATION_FACTOR);
  scale.tare();
  Serial.println("[Scale] HX711 ready, tared to zero.");

  // #wifi
  connectWiFi();

  // #server
  if (WiFi.status() == WL_CONNECTED) {
    fetchPatientConfig();
  } else {
    Serial.println("[Config] Skipping config fetch - no WiFi.");
  }

  lcd.clear();
  updateLCD(0);
}

void loop() {
  unsigned long now = millis();

  // #wifi
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] Reconnecting...");
    WiFi.reconnect();
    delay(3000);
    return;
  }

  if (now - lastPostTime >= POST_INTERVAL_MS) {
    lastPostTime = now;

    // #loadcell
    // Real-time weight reading taken directly from the HX711 amplifier
    // every cycle -- this is the only source of volume data; nothing
    // is simulated or hardcoded.
    float weightGrams = 0;
    if (scale.is_ready()) {
      weightGrams = scale.get_units(5);
      if (weightGrams < 0) weightGrams = 0;

      Serial.print("[Scale] Weight = ");
      Serial.print(weightGrams, 2);
      Serial.println(" g");
    } else {
      Serial.println("[Scale] HX711 not ready - using last reading");
    }

    // #server
    postSensorData(weightGrams);

    // #lcd
    updateLCD(weightGrams);
  }

  yield();
}
