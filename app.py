"""
╔══════════════════════════════════════════════════════════╗
║  IV DRIP MONITORING SYSTEM — Flask Backend (app.py)      ║
║  Receives ESP32 sensor data → pushes to frontend via     ║
║  Socket.IO in real-time. Also exposes REST endpoints.    ║
╚══════════════════════════════════════════════════════════╝

Run:
    python app.py
"""

import os
from flask import Flask, request, jsonify, send_from_directory
from flask_socketio import SocketIO, emit
from flask_cors import CORS
from database import db, Patient, SensorLog
from datetime import datetime, timezone

# ── APP INIT ──────────────────────────────────────────────
BASE_DIR = os.path.dirname(os.path.abspath(__file__))
app = Flask(__name__, static_folder=BASE_DIR, static_url_path='')

# Configuration — update DB path as needed
app.config['SECRET_KEY']                    = 'iv-monitor-secret-2024'
app.config['SQLALCHEMY_DATABASE_URI']       = 'sqlite:///iv_monitor.db'
app.config['SQLALCHEMY_TRACK_MODIFICATIONS'] = False

# CORS: allow frontend dev server if separate
CORS(app, resources={r"/api/*": {"origins": "*"}})

# Socket.IO — threading mode (works reliably on Windows)
socketio = SocketIO(app, cors_allowed_origins="*", async_mode='threading')

# Init DB
db.init_app(app)

with app.app_context():
    db.create_all()
    # Seed demo patients if empty
    if Patient.query.count() == 0:
        demo = [
            Patient(name="Arjun Mehta",   bed_number="3B-07", solution="0.9% NaCl",       flow_rate=42, volume_pct=12,  device_id="ESP32-001", motor_on=True),
            Patient(name="Priya Sharma",  bed_number="3B-02", solution="Glucose 5%",       flow_rate=60, volume_pct=68,  device_id="ESP32-002", motor_on=True),
            Patient(name="Ramesh Kumar",  bed_number="3B-09", solution="Ringer's Lactate", flow_rate=35, volume_pct=45,  device_id="ESP32-003", motor_on=True),
            Patient(name="Deepa Nair",    bed_number="3B-11", solution="0.9% NaCl",        flow_rate=50, volume_pct=88,  device_id="ESP32-004", motor_on=True),
            Patient(name="Suresh Pillai", bed_number="3B-14", solution="Dextrose + NaCl",  flow_rate=30, volume_pct=33,  device_id="ESP32-005", motor_on=False),
        ]
        db.session.add_all(demo)
        db.session.commit()
        print("[DB] Seeded demo patients.")


# ── SERVE FRONTEND ────────────────────────────────────────
@app.route('/')
def index():
    """Serve the main HTML file (static frontend)."""
    return send_from_directory(BASE_DIR, 'index.html')


# ══════════════════════════════════════════════════════════
#  REST API ENDPOINTS
# ══════════════════════════════════════════════════════════

@app.route('/api/patients', methods=['GET'])
def get_patients():
    """
    GET /api/patients
    Returns list of all patients with current IV status.
    """
    patients = Patient.query.all()
    return jsonify([p.to_dict() for p in patients]), 200


@app.route('/api/patients', methods=['POST'])
def add_patient():
    """
    POST /api/patients
    Body: { name, bed_number, solution, flow_rate, device_id }
    Creates a new patient record.
    """
    data = request.get_json()
    if not data or not data.get('name') or not data.get('bed_number'):
        return jsonify({'error': 'name and bed_number are required'}), 400

    patient = Patient(
        name        = data['name'],
        bed_number  = data['bed_number'],
        solution    = data.get('solution', '0.9% NaCl'),
        flow_rate   = data.get('flow_rate', 40),
        volume_pct  = 100,
        device_id   = data.get('device_id', 'UNASSIGNED'),
        motor_on    = False
    )
    db.session.add(patient)
    db.session.commit()

    print(f"[API] New patient added: {patient.name} | Bed {patient.bed_number}")
    return jsonify(patient.to_dict()), 201


@app.route('/api/patients/<int:patient_id>', methods=['GET'])
def get_patient(patient_id):
    """GET /api/patients/<id> — Single patient record."""
    patient = Patient.query.get_or_404(patient_id)
    return jsonify(patient.to_dict()), 200


@app.route('/api/patients/<int:patient_id>', methods=['DELETE'])
def delete_patient(patient_id):
    """DELETE /api/patients/<id> — Remove a patient."""
    patient = Patient.query.get_or_404(patient_id)
    db.session.delete(patient)
    db.session.commit()
    return jsonify({'deleted': patient_id}), 200


# ──────────────────────────────────────────────────────────
#  SENSOR DATA ENDPOINT (called by ESP32 via HTTP POST)
# ──────────────────────────────────────────────────────────

@app.route('/api/sensor-data', methods=['POST'])
def receive_sensor_data():
    """
    POST /api/sensor-data
    Called by ESP32 every N seconds to push weight sensor reading.

    Expected JSON:
    {
      "patient_id": 1,
      "weight_g":   320,
      "flow_rate":  42,
      "device_id":  "ESP32-001"
    }

    Server computes volume_pct from weight, saves log,
    then pushes 'sensor_update' to all connected web clients.
    """
    data = request.get_json()
    if not data:
        return jsonify({'error': 'No JSON payload'}), 400

    patient_id = data.get('patient_id')
    weight_g   = data.get('weight_g', 0)
    flow_rate  = float(data.get('flow_rate', 40))
    device_id  = data.get('device_id', 'UNKNOWN')

    patient = Patient.query.get(patient_id)
    if not patient:
        return jsonify({'error': f'Patient {patient_id} not found'}), 404

    # Volume calculation:
    # Assume 500 mL bag = 500 g (water density)
    # Tare weight of bag + tubing ≈ 50 g (calibrate per device)
    TARE_WEIGHT_G = 50
    FULL_VOLUME_G = 500

    net_weight_g = max(0, weight_g - TARE_WEIGHT_G)
    volume_pct   = round((net_weight_g / FULL_VOLUME_G) * 100, 1)
    volume_pct   = min(100, volume_pct)

    # Estimate time remaining (minutes)
    if flow_rate > 0:
        remaining_ml = (volume_pct / 100) * 500
        time_left_min = (remaining_ml / flow_rate) * 60
        hours, mins = divmod(int(time_left_min), 60)
        time_left_str = f"{hours}h {mins}m" if hours else f"{mins}m"
    else:
        time_left_str = "Stopped"

    # Update patient record
    patient.volume_pct  = volume_pct
    patient.flow_rate   = flow_rate
    patient.last_seen   = datetime.now(timezone.utc)

    # Log to DB
    log = SensorLog(
        patient_id = patient_id,
        weight_g   = weight_g,
        volume_pct = volume_pct,
        flow_rate  = flow_rate,
        device_id  = device_id
    )
    db.session.add(log)
    db.session.commit()

    # Push real-time update to all connected browsers via Socket.IO
    payload = {
        'patient_id': patient_id,
        'volume_pct': volume_pct,
        'flow_rate':  flow_rate,
        'time_left':  time_left_str,
        'weight_g':   weight_g,
        'device_id':  device_id,
        'timestamp':  datetime.now(timezone.utc).isoformat()
    }
    socketio.emit('sensor_update', payload)

    print(f"[Sensor] Patient {patient_id} | {volume_pct}% | {flow_rate} mL/h | {time_left_str}")

    # Return servo state to ESP32 (it polls this to know whether to run motor)
    return jsonify({
        'status':       'ok',
        'SERVO_STATE':  'FLOW' if patient.motor_on else 'STOP',
        'patient_id':   patient_id
    }), 200


# ──────────────────────────────────────────────────────────
#  SERVO COMMAND ENDPOINT (called by frontend button)
# ──────────────────────────────────────────────────────────

@app.route('/api/servo-command', methods=['POST'])
def servo_command():
    """
    POST /api/servo-command
    Body: { patient_id: 1, state: "FLOW" | "STOP" }

    Sets the motor_on flag in the DB.
    The ESP32 polls the sensor-data response to get this state.
    """
    data = request.get_json()
    patient_id = data.get('patient_id')
    state      = data.get('state', 'STOP')

    patient = Patient.query.get(patient_id)
    if not patient:
        return jsonify({'error': 'Patient not found'}), 404

    patient.motor_on = (state == 'FLOW')
    db.session.commit()

    print(f"[Servo] Patient {patient_id} ({patient.name}) → {state}")
    return jsonify({'status': 'ok', 'SERVO_STATE': state}), 200


@app.route('/api/logs/<int:patient_id>', methods=['GET'])
def get_logs(patient_id):
    """GET /api/logs/<id> — Sensor log history for charts."""
    limit = request.args.get('limit', 50, type=int)
    logs  = SensorLog.query.filter_by(patient_id=patient_id)\
                    .order_by(SensorLog.timestamp.desc())\
                    .limit(limit).all()
    return jsonify([l.to_dict() for l in logs]), 200


# ── SOCKET.IO EVENTS ──────────────────────────────────────
@socketio.on('connect')
def on_connect():
    print(f"[Socket.IO] Client connected: {request.sid}")
    emit('server_hello', {'message': 'IV Monitor connected'})

@socketio.on('disconnect')
def on_disconnect():
    print(f"[Socket.IO] Client disconnected: {request.sid}")


# ── RUN ───────────────────────────────────────────────────
if __name__ == '__main__':
    print("═" * 50)
    print("  IV DRIP MONITORING SYSTEM — Backend Server")
    print("  http://localhost:5000")
    print("═" * 50)
    socketio.run(app, host='0.0.0.0', port=5000, debug=True)
