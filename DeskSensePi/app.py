import logging
import os
import secrets
import ssl
from functools import wraps

from flask import Flask, Response, render_template, request, session
from flask_socketio import SocketIO
import paho.mqtt.client as mqtt
from dotenv import load_dotenv

# Load environment variables from .env file
load_dotenv()

# ================== APP SETUP ==================
app = Flask(__name__)

# Secret key used for session security (auto-generated if not provided)
app.secret_key = os.environ.get("FLASK_SECRET_KEY", secrets.token_hex(32))

# ================== CONFIGURATION ==================
# Dashboard login credentials (basic auth)
DASHBOARD_USERNAME = os.environ.get("DASHBOARD_USERNAME", "admin")
DASHBOARD_PASSWORD = os.environ.get("DASHBOARD_PASSWORD", "change-me-now")

# MQTT broker configuration
MQTT_HOST = os.environ.get("MQTT_HOST", "localhost")
MQTT_PORT = int(os.environ.get("MQTT_PORT", "1883"))
MQTT_USERNAME = os.environ.get("MQTT_USERNAME", "desksense_app")
MQTT_PASSWORD = os.environ.get("MQTT_PASSWORD", "")

# TLS settings for secure MQTT (optional)
MQTT_TLS_ENABLED = os.environ.get("MQTT_TLS_ENABLED", "false").lower() == "true"
MQTT_CA_CERT = os.environ.get("MQTT_CA_CERT", "")

# Allowed timeout values (in minutes) that frontend can set
ALLOWED_TIMEOUTS = {1, 5, 10}

# ================== LOGGING ==================
logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
logger = logging.getLogger("desksense")

# ================== SOCKET.IO ==================
# Enables real-time communication between backend and frontend
socketio = SocketIO(
    app,
    cors_allowed_origins=os.environ.get("SOCKETIO_ALLOWED_ORIGINS", "").split(",")
    if os.environ.get("SOCKETIO_ALLOWED_ORIGINS")
    else None,
)

# ================== SHARED STATE ==================
# Stores latest data from MQTT to push to frontend
latest_data = {
    "count": "0",               # 0, 1, or 2 people
    "result": "VACANT",         # Human-readable status
    "current_timeout": "10"     # Current vacancy timeout (minutes)
}

# ================== AUTHENTICATION ==================
# Secure comparison to prevent timing attacks
def check_auth(username, password):
    return secrets.compare_digest(username, DASHBOARD_USERNAME) and secrets.compare_digest(
        password, DASHBOARD_PASSWORD
    )

# Decorator to protect routes with basic auth
def require_auth(view):
    @wraps(view)
    def wrapped(*args, **kwargs):
        auth = request.authorization

        # If no credentials or invalid → reject request
        if not auth or not check_auth(auth.username, auth.password):
            return Response(
                "Authentication required",
                401,
                {"WWW-Authenticate": 'Basic realm="DeskSense"'},
            )

        # Mark session as authenticated for Socket.IO use
        session["authenticated"] = True
        return view(*args, **kwargs)

    return wrapped

# ================== MQTT MESSAGE HANDLER ==================
# Called whenever a subscribed MQTT message is received
def on_message(client, userdata, message):
    topic = message.topic
    payload = message.payload.decode()
    
    # -------- Occupancy Count Updates --------
    if topic == "occupancy/count":
        latest_data["count"] = payload

        # Convert numeric value to human-readable label
        if payload == "0":
            latest_data["result"] = "VACANT"
        elif payload == "1":
            latest_data["result"] = "ONE PERSON"
        elif payload == "2":
            latest_data["result"] = "TWO PEOPLE"

        # Push update to all connected frontend clients
        socketio.emit('update_data', latest_data)

        logger.info(
            "Occupancy count update: count=%s result=%s",
            latest_data["count"],
            latest_data["result"]
        )

    # -------- Device Status Updates --------
    elif topic == "occupancy/status":
        latest_data["status"] = payload

        # Broadcast device status (e.g., "Occupied", "Vacant", "Online")
        socketio.emit('update_data', latest_data)

        logger.info("Device status update: %s", payload)

# ================== MQTT CLIENT SETUP ==================
mqtt_client = mqtt.Client()

# Attach message handler
mqtt_client.on_message = on_message

# Configure authentication if provided
if MQTT_USERNAME:
    mqtt_client.username_pw_set(MQTT_USERNAME, MQTT_PASSWORD)

# Configure TLS if enabled (secure MQTT connection)
if MQTT_TLS_ENABLED:
    mqtt_client.tls_set(ca_certs=MQTT_CA_CERT or None, tls_version=ssl.PROTOCOL_TLS_CLIENT)
    mqtt_client.tls_insecure_set(False)

# Connect to MQTT broker
mqtt_client.connect(MQTT_HOST, MQTT_PORT)

# Subscribe to relevant topics from ESP8266
mqtt_client.subscribe("occupancy/count")
mqtt_client.subscribe("occupancy/status")

# Start MQTT listener loop in background thread
mqtt_client.loop_start()

# ================== ROUTES ==================
@app.route('/')
@require_auth
def index():
    # Protected dashboard page
    return render_template('index.html')

# ================== SOCKET.IO EVENTS ==================
@socketio.on('connect')
def handle_connect():
    # Reject connection if user is not authenticated
    if not session.get("authenticated"):
        return False

    # Send current state immediately upon connection
    socketio.emit('update_data', latest_data, to=request.sid)

@socketio.on('toggle_timeout')
def handle_toggle(data):
    # Prevent unauthorized users from changing settings
    if not session.get("authenticated"):
        logger.warning("Rejected unauthenticated timeout update")
        return

    # Validate incoming data
    try:
        new_timeout = int(data['timeout'])
    except (KeyError, TypeError, ValueError):
        logger.warning("Rejected malformed timeout update: %s", data)
        return

    # Only allow predefined safe values
    if new_timeout not in ALLOWED_TIMEOUTS:
        logger.warning("Rejected invalid timeout update: %s", new_timeout)
        return

    # Update local state
    latest_data["current_timeout"] = new_timeout

    # Send new timeout to ESP8266 via MQTT
    mqtt_client.publish("occupancy/set_timeout", str(new_timeout))

    # Broadcast updated state to all clients
    socketio.emit('update_data', latest_data)

    logger.info("Setting ESP8266 timeout to %s minutes", new_timeout)

# ================== APP ENTRY POINT ==================
if __name__ == '__main__':
    # Run Flask app with Socket.IO support
    socketio.run(app, host='0.0.0.0', port=5000)
