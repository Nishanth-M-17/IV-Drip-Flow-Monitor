# IVGuard — Clinical IV Monitoring System

Full-stack IV drip monitoring: ESP32-S3 firmware → Python , flask → HTML/JS dashboard.

```
esp32/ → Arduino firmware
backend/ → Spring Boot REST + WebSocket + MQTT server
frontend/ → index.html, style.css, app.js (open directly in browser)
```

---

## 1. Frontend (No build step required)

Open `frontend/index.html` directly in Chrome/Firefox.

**Or** serve it with a local dev server to avoid CORS issues:
```bash
cd frontend
npx serve .          # npm i -g serve  first
# OR
python3 -m http.server 5500
```

Then open: `http://localhost:5500`

The dashboard runs in **simulation mode** by default (`CONFIG.SIMULATE = true` in `app.js`) — the bottle drains and DPM fluctuates without any backend.

To connect to the real backend, the WebSocket auto-connects to `ws://localhost:8080/ws`. No code change needed once the backend is running.

---

## 2. Backend (Spring Boot)

### Prerequisites
- Java 21+
- Maven 3.9+
- (Optional) Mosquitto MQTT broker for hardware integration

### Run
```bash
cd backend
mvn spring-boot:run
```

The server starts on **http://localhost:8080**.

### Test the REST endpoints
```bash
# Health check
curl http://localhost:8080/api/health

# Simulate an ESP32 sensor push
curl -X POST http://localhost:8080/api/sensor-data \
  -H "Content-Type: application/json" \
  -d '{
    "roomId": "201",
    "patientId": "john-doe",
    "currentVolume": 55.0,
    "totalVolume": 500.0,
    "dpm": 63.5
  }'

# Trigger Emergency Clamp (as if the dashboard button was pressed)
curl -X POST http://localhost:8080/api/clamp \
  -H "Content-Type: application/json" \
  -d '{
    "roomId": "201",
    "patientId": "john-doe",
    "action": "LOCK",
    "timestamp": "2024-05-07T15:30:00.000Z"
  }'
```

### Threshold logic (SensorProcessingService.java)
```
if (currentVolume <= totalVolume * 0.12)  → CRITICAL alert broadcast + bottle UI turns red
if (currentVolume <= totalVolume * 0.15)  → WARNING alert broadcast
```
Thresholds are configurable in `application.properties`.

### WebSocket topics (subscribe from frontend)
| Topic | Content |
|---|---|
| `/topic/sensor` | Live DPM + volume updates (all rooms) |
| `/topic/alerts` | Critical/warning/info alerts |
| `/topic/clamp/{roomId}` | Clamp command acknowledgements |

---

## 3. MQTT Broker (Mosquitto)

Install on Ubuntu/Debian:
```bash
sudo apt install mosquitto mosquitto-clients
sudo systemctl enable --now mosquitto
```

Set broker IP in `application.properties`:
```properties
ivguard.mqtt.broker-url=tcp://localhost:1883
```

Monitor live MQTT traffic:
```bash
mosquitto_sub -h localhost -t "ivguard/#" -v
```

Simulate an ESP32 push manually:
```bash
mosquitto_pub -h localhost -t "ivguard/201/sensor" \
  -m '{"roomId":"201","currentVolume":55,"totalVolume":500,"dpm":64.2}'
```

Send a manual valve lock:
```bash
mosquitto_pub -h localhost -t "ivguard/201/valve" -m "LOCK"
```

---

## 4. ESP32-S3 Firmware

### Hardware wiring
| Component | ESP32-S3 Pin |
|---|---|
| IR break-beam OUT | GPIO 14 |
| Servo signal | GPIO 13 |
| Servo VCC | 5V |
| Servo GND | GND |

### Arduino IDE setup
1. Add ESP32 board support: `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
2. Install libraries via Library Manager:
   - **PubSubClient** (Nick O'Leary)
   - **ArduinoJson** (Benoit Blanchon)
   - **ESP32Servo** (Kevin Harrington)
3. Open `esp32/ivguard_firmware.ino`
4. Fill in:
   ```cpp
   const char* WIFI_SSID     = "your_network";
   const char* WIFI_PASSWORD = "your_password";
   const char* MQTT_BROKER   = "192.168.x.x";   // your broker IP
   const char* BACKEND_URL   = "http://192.168.x.x:8080/api/sensor-data";
   ```
5. Select board: **ESP32S3 Dev Module**, flash at 921600 baud.

### Data flow
```
IR sensor breaks beam
        ↓  (ISR)
   dropCount++
        ↓  (every 1 second)
   DPM = drops × 60
        ↓
   Publish JSON → MQTT: ivguard/201/sensor
        ↓
   Spring Boot receives, checks thresholds
        ↓
   WebSocket broadcast → Dashboard
        ↓  (if critical)
   Bottle UI turns red + alert toast fires
```

### Emergency clamp flow
```
Dashboard "EMERGENCY CLAMP" button
        ↓
   POST /api/clamp  {"action":"LOCK"}
        ↓
   Spring Boot → MQTT publish → ivguard/201/valve: "LOCK"
        ↓
   ESP32 receives → servo rotates 90° → flow stopped
        ↓
   WebSocket ACK → dashboard shows "CLAMP ACTIVE"
```

---

## 5. Architecture Diagram

```
┌──────────────────────────────────────────────────────┐
│                    Hospital Network                  │
│                                                      │
│  ┌─────────────┐    MQTT/HTTP    ┌────────────────┐  │
│  │  ESP32-S3   │ ─────────────► │  Spring Boot   │  │
│  │             │                │  :8080         │  │
│  │ IR sensor   │ ◄───────────── │                │  │
│  │ Servo valve │   MQTT LOCK    │ /api/sensor    │  │
│  └─────────────┘                │ /api/clamp     │  │
│                                 │ /ws (STOMP)    │  │
│                                 └───────┬────────┘  │
│                                         │ WebSocket  │
│                                 ┌───────▼────────┐  │
│                                 │  Browser UI    │  │
│                                 │  index.html    │  │
│                                 │  (any device)  │  │
│                                 └────────────────┘  │
│                                                      │
│  ┌─────────────┐                                     │
│  │  Mosquitto  │ ◄── both ESP32 and Spring Boot      │
│  │  MQTT :1883 │     connect here                    │
│  └─────────────┘                                     │
└──────────────────────────────────────────────────────┘
```

---

## Project Structure

```
ivguard/
├── frontend/
│   ├── index.html        Dashboard UI
│   ├── style.css         Clinical Light Mode styles
│   └── app.js            WebSocket client + Chart.js + live logic
│
├── backend/
│   ├── pom.xml
│   └── src/main/
│       ├── java/com/ivguard/
│       │   ├── IVGuardApplication.java
│       │   ├── controller/
│       │   │   └── IVGuardController.java    REST endpoints
│       │   ├── model/
│       │   │   ├── SensorPayload.java
│       │   │   └── ClampRequest.java
│       │   ├── service/
│       │   │   ├── SensorProcessingService.java  threshold logic
│       │   │   └── MqttService.java              MQTT pub/sub
│       │   └── websocket/
│       │       ├── WebSocketConfig.java
│       │       └── WebSocketBroadcaster.java
│       └── resources/
│           └── application.properties
│
└── esp32/
    └── ivguard_firmware.ino    Arduino sketch for ESP32-S3
```
