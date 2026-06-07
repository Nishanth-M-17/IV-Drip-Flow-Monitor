IV Drip Monitoring System
This project is a real-time IV Drip Monitoring System designed to track IV bag levels and alert medical staff. It features a Flask-based backend, a real-time frontend using Socket.IO, and a Python-based simulator for testing.

Features
Real-time Monitoring: Uses Socket.IO to push live sensor updates from hardware to the dashboard.

REST API: Exposes endpoints for managing patient records and retrieving historical logs.

Premium UI: A high-contrast, kinetic interface built with CSS (inspired by the "Voldog" aesthetic), GSAP animations, and custom interactions.

Simulated Data: Includes a test_sensor.py script to simulate real-time ESP32 sensor traffic.

Project Structure
app.py: Flask backend managing API routes and WebSocket communication.

database.py: SQLAlchemy database models for Patient and SensorLog.

index.html: The main dashboard entry point.

script.js: Handles frontend logic, including tab navigation, card rendering, and real-time animations.

style.css: Premium CSS styling, animations, and responsive layout.

test_sensor.py: Python simulator script for testing the backend with fake sensor data.

requirements.txt: Project dependencies.

Setup and Installation
1. Prerequisites
Ensure you have Python installed.

2. Install Dependencies
pip install -r requirements.txt

3. Running the Application
Start the Flask backend:
python app.py

4. Running the Sensor Simulator (Optional)
To see the system react to live data while the backend is running, open a new terminal and run:
python test_sensor.py

Technology Stack
Backend: Flask, Flask-SocketIO, Flask-SQLAlchemy

Database: SQLite

Frontend: HTML5, CSS3, JavaScript (ES6+), GSAP (Animations)

Production: Gunicorn, Eventlet
