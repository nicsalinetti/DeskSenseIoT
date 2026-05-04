from flask import Flask, render_template
from flask_socketio import SocketIO
import paho.mqtt.client as mqtt

app = Flask(__name__)
socketio = SocketIO(app, cors_allowed_origins="*")

latest_data = {"count": "0", "result": "VACANT", "current_timeout": "10"}

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

mqtt_client = mqtt.Client()
mqtt_client.on_message = on_message
mqtt_client.connect("localhost", 1883)
mqtt_client.subscribe("occupancy/count")
mqtt_client.loop_start()

@app.route('/')
def index():
    return render_template('index.html')

# New handler for the button click
@socketio.on('toggle_timeout')
def handle_toggle(data):
    new_timeout = data['timeout']
    latest_data["current_timeout"] = new_timeout
    # Send the update TO the ESP8266 via MQTT
    mqtt_client.publish("occupancy/set_timeout", str(new_timeout))
    print(f"Setting ESP8266 timeout to {new_timeout} minutes")

if __name__ == '__main__':
    socketio.run(app, host='0.0.0.0', port=5000)
