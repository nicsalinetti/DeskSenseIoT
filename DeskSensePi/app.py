import logging
import os
import secrets
import ssl
from functools import wraps

from flask import Flask, Response, render_template, request, session
from flask_socketio import SocketIO
import paho.mqtt.client as mqtt
from dotenv import load_dotenv

load_dotenv()

app = Flask(__name__)
app.secret_key = os.environ.get("FLASK_SECRET_KEY", secrets.token_hex(32))

DASHBOARD_USERNAME = os.environ.get("DASHBOARD_USERNAME", "admin")
DASHBOARD_PASSWORD = os.environ.get("DASHBOARD_PASSWORD", "change-me-now")
MQTT_HOST = os.environ.get("MQTT_HOST", "localhost")
MQTT_PORT = int(os.environ.get("MQTT_PORT", "1883"))
MQTT_USERNAME = os.environ.get("MQTT_USERNAME", "desksense_app")
MQTT_PASSWORD = os.environ.get("MQTT_PASSWORD", "")
MQTT_TLS_ENABLED = os.environ.get("MQTT_TLS_ENABLED", "false").lower() == "true"
MQTT_CA_CERT = os.environ.get("MQTT_CA_CERT", "")
ALLOWED_TIMEOUTS = {1, 5, 10}

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
logger = logging.getLogger("desksense")

socketio = SocketIO(
    app,
    cors_allowed_origins=os.environ.get("SOCKETIO_ALLOWED_ORIGINS", "").split(",")
    if os.environ.get("SOCKETIO_ALLOWED_ORIGINS")
    else None,
)

latest_data = {"count": "0", "result": "VACANT", "current_timeout": "10"}


def check_auth(username, password):
    return secrets.compare_digest(username, DASHBOARD_USERNAME) and secrets.compare_digest(
        password, DASHBOARD_PASSWORD
    )


def require_auth(view):
    @wraps(view)
    def wrapped(*args, **kwargs):
        auth = request.authorization
        if not auth or not check_auth(auth.username, auth.password):
            return Response(
                "Authentication required",
                401,
                {"WWW-Authenticate": 'Basic realm="DeskSense"'},
            )
        session["authenticated"] = True
        return view(*args, **kwargs)

    return wrapped

def on_message(client, userdata, message):
    topic = message.topic
    payload = message.payload.decode()
    
    if topic == "occupancy/count":
        latest_data["count"] = payload
        if payload == "0":
            latest_data["result"] = "VACANT"
        elif payload == "1":
            latest_data["result"] = "ONE PERSON"
        elif payload == "2":
            latest_data["result"] = "TWO PEOPLE"
        socketio.emit('update_data', latest_data)
        logger.info("Occupancy count update: count=%s result=%s", latest_data["count"], latest_data["result"])
    elif topic == "occupancy/status":
        latest_data["status"] = payload
        socketio.emit('update_data', latest_data)
        logger.info("Device status update: %s", payload)

mqtt_client = mqtt.Client()
mqtt_client.on_message = on_message
if MQTT_USERNAME:
    mqtt_client.username_pw_set(MQTT_USERNAME, MQTT_PASSWORD)
if MQTT_TLS_ENABLED:
    mqtt_client.tls_set(ca_certs=MQTT_CA_CERT or None, tls_version=ssl.PROTOCOL_TLS_CLIENT)
    mqtt_client.tls_insecure_set(False)
mqtt_client.connect(MQTT_HOST, MQTT_PORT)
mqtt_client.subscribe("occupancy/count")
mqtt_client.subscribe("occupancy/status")
mqtt_client.loop_start()

@app.route('/')
@require_auth
def index():
    return render_template('index.html')


@socketio.on('connect')
def handle_connect():
    if not session.get("authenticated"):
        return False
    socketio.emit('update_data', latest_data, to=request.sid)


@socketio.on('toggle_timeout')
def handle_toggle(data):
    if not session.get("authenticated"):
        logger.warning("Rejected unauthenticated timeout update")
        return
    try:
        new_timeout = int(data['timeout'])
    except (KeyError, TypeError, ValueError):
        logger.warning("Rejected malformed timeout update: %s", data)
        return
    if new_timeout not in ALLOWED_TIMEOUTS:
        logger.warning("Rejected invalid timeout update: %s", new_timeout)
        return
    latest_data["current_timeout"] = new_timeout
    mqtt_client.publish("occupancy/set_timeout", str(new_timeout))
    socketio.emit('update_data', latest_data)
    logger.info("Setting ESP8266 timeout to %s minutes", new_timeout)

if __name__ == '__main__':
    socketio.run(app, host='0.0.0.0', port=5000)
