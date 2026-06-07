"""
╔══════════════════════════════════════════════════════════╗
║  IV DRIP MONITORING SYSTEM — database.py                 ║
║  SQLAlchemy models: Patient, SensorLog                   ║
╚══════════════════════════════════════════════════════════╝
"""

from flask_sqlalchemy import SQLAlchemy
from datetime import datetime

db = SQLAlchemy()


class Patient(db.Model):
    """
    One record per IV patient. Updated on each sensor POST.
    """
    __tablename__ = 'patients'

    id          = db.Column(db.Integer, primary_key=True)
    name        = db.Column(db.String(120), nullable=False)
    bed_number  = db.Column(db.String(20),  nullable=False, unique=True)
    solution    = db.Column(db.String(80),  default='0.9% NaCl')
    flow_rate   = db.Column(db.Float,       default=40.0)    # mL/hr
    volume_pct  = db.Column(db.Float,       default=100.0)   # 0–100%
    motor_on    = db.Column(db.Boolean,     default=False)
    device_id   = db.Column(db.String(40),  default='UNASSIGNED')
    created_at  = db.Column(db.DateTime,    default=datetime.utcnow)
    last_seen   = db.Column(db.DateTime,    default=datetime.utcnow)

    # Relationship: one patient → many sensor log entries
    logs = db.relationship('SensorLog', backref='patient', lazy=True, cascade='all, delete-orphan')

    def to_dict(self):
        """Serialise to JSON-safe dict for REST responses."""
        return {
            'id':          self.id,
            'name':        self.name,
            'bed':         self.bed_number,
            'solution':    self.solution,
            'flow':        self.flow_rate,
            'volume':      round(self.volume_pct, 1),
            'motorOn':     self.motor_on,
            'deviceId':    self.device_id,
            'createdAt':   self.created_at.isoformat() if self.created_at else None,
            'lastSeen':    self.last_seen.isoformat()  if self.last_seen  else None,
            # Computed time estimate
            'timeLeft':    self._estimate_time_left()
        }

    def _estimate_time_left(self):
        """Rough estimate based on current volume % and flow rate."""
        if not self.motor_on or self.flow_rate <= 0:
            return "Stopped"
        remaining_ml  = (self.volume_pct / 100) * 500  # assume 500 mL bag
        minutes_left  = (remaining_ml / self.flow_rate) * 60
        hours, mins   = divmod(int(minutes_left), 60)
        return f"{hours}h {mins}m" if hours else f"{mins}m"

    def __repr__(self):
        return f"<Patient {self.name} | Bed {self.bed_number} | {self.volume_pct}%>"


class SensorLog(db.Model):
    """
    Time-series sensor readings from each ESP32.
    Used for graph history and analytics.
    """
    __tablename__ = 'sensor_logs'

    id          = db.Column(db.Integer, primary_key=True)
    patient_id  = db.Column(db.Integer, db.ForeignKey('patients.id'), nullable=False)
    weight_g    = db.Column(db.Float,   nullable=False)      # raw load cell grams
    volume_pct  = db.Column(db.Float,   nullable=False)      # computed 0–100%
    flow_rate   = db.Column(db.Float,   default=0.0)         # mL/hr at time of reading
    device_id   = db.Column(db.String(40))
    timestamp   = db.Column(db.DateTime, default=datetime.utcnow, index=True)

    def to_dict(self):
        return {
            'id':         self.id,
            'patientId':  self.patient_id,
            'weightG':    self.weight_g,
            'volumePct':  self.volume_pct,
            'flowRate':   self.flow_rate,
            'deviceId':   self.device_id,
            'timestamp':  self.timestamp.isoformat()
        }

    def __repr__(self):
        return f"<SensorLog Patient={self.patient_id} {self.volume_pct}% @ {self.timestamp}>"
