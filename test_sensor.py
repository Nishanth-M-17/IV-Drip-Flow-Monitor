"""
test_sensor.py — Simulate ESP32 sensor POSTs to Flask backend.
Run AFTER starting app.py to see live updates in the browser.

Usage:
    python test_sensor.py
"""

import requests
import time
import random
import math

BASE_URL = "http://localhost:5000"

# Slows down the drain rate without changing the displayed flow_rate.
# 1.0 = real-time drain speed. 0.25 = drains 4x slower than real flow rate.
SLOWDOWN_FACTOR = 0.25

# Simulate 4 devices draining slowly
DEVICES = [
    {"patient_id": 1, "device_id": "ESP32-001", "weight_g": 340, "flow_rate": 42, "servo_state": "FLOW"},  # was 74 -> hit the 50g floor almost instantly
    {"patient_id": 2, "device_id": "ESP32-002", "weight_g": 390, "flow_rate": 60, "servo_state": "FLOW"},
    {"patient_id": 3, "device_id": "ESP32-003", "weight_g": 275, "flow_rate": 35, "servo_state": "FLOW"},
    {"patient_id": 4, "device_id": "ESP32-004", "weight_g": 490, "flow_rate": 50, "servo_state": "FLOW"},
]

tick = 0

print("═" * 40)
print("  IV Monitor — Sensor Simulator")
print("  Ctrl+C to stop")
print("═" * 40)

while True:
    for device in DEVICES:
        # Only drain weight if the servo is currently flowing.
        # If STOP was set on the last response, freeze the weight
        # (real IV clamp would physically block flow too).
        if device["servo_state"] == "FLOW":
            drain_g = (device["flow_rate"] / 60 / 60) * 5 * 1000 * SLOWDOWN_FACTOR  # g per 5s, slowed down
            device["weight_g"] = max(50, device["weight_g"] - drain_g + random.uniform(-1, 1))
        else:
            # Tiny jitter only, so the reading isn't perfectly frozen/dead-looking
            device["weight_g"] = max(50, device["weight_g"] + random.uniform(-0.3, 0.3))

        payload = {
            "patient_id": device["patient_id"],
            "weight_g": round(device["weight_g"], 1),
            "flow_rate": device["flow_rate"] + random.uniform(-2, 2),
            "device_id": device["device_id"]
        }

        try:
            r = requests.post(f"{BASE_URL}/api/sensor-data", json=payload, timeout=3)
            resp = r.json()
            servo_state = resp.get("SERVO_STATE", "FLOW")
            device["servo_state"] = servo_state  # remember it for the next tick
            print(f"[{device['device_id']}] weight={payload['weight_g']}g | servo={servo_state}")
        except Exception as e:
            print(f"[ERROR] {device['device_id']}: {e}")

    tick += 1
    print(f"── tick {tick} ──")
    time.sleep(5)
