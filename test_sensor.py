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

# Simulate 4 devices draining slowly
DEVICES = [
    {"patient_id": 1, "device_id": "ESP32-001", "weight_g": 74,  "flow_rate": 42},  # critical
    {"patient_id": 2, "device_id": "ESP32-002", "weight_g": 390, "flow_rate": 60},
    {"patient_id": 3, "device_id": "ESP32-003", "weight_g": 275, "flow_rate": 35},
    {"patient_id": 4, "device_id": "ESP32-004", "weight_g": 490, "flow_rate": 50},
]

tick = 0
print("═" * 40)
print("  IV Monitor — Sensor Simulator")
print("  Ctrl+C to stop")
print("═" * 40)

while True:
    for device in DEVICES:
        # Simulate weight decreasing over time
        drain_g = (device["flow_rate"] / 60 / 60) * 5 * 1000  # g per 5s
        device["weight_g"] = max(50, device["weight_g"] - drain_g + random.uniform(-1, 1))

        payload = {
            "patient_id": device["patient_id"],
            "weight_g":   round(device["weight_g"], 1),
            "flow_rate":  device["flow_rate"] + random.uniform(-2, 2),
            "device_id":  device["device_id"]
        }

        try:
            r = requests.post(f"{BASE_URL}/api/sensor-data", json=payload, timeout=3)
            resp = r.json()
            print(f"[{device['device_id']}] weight={payload['weight_g']}g | servo={resp.get('SERVO_STATE')}")
        except Exception as e:
            print(f"[ERROR] {device['device_id']}: {e}")

    tick += 1
    print(f"── tick {tick} ──")
    time.sleep(5)
