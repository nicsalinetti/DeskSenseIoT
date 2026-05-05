# Mosquitto Password Setup

Create one MQTT identity for the ESP8266 and one for the Flask app:

```bash
sudo mosquitto_passwd -c /etc/mosquitto/passwd desksense_esp
sudo mosquitto_passwd /etc/mosquitto/passwd desksense_app
sudo chown mosquitto: /etc/mosquitto/passwd
sudo chmod 640 /etc/mosquitto/passwd
sudo systemctl restart mosquitto
```

Use the `desksense_esp` password in `DeskSenseESP/arduino_secrets.h`.
Use the `desksense_app` password in `DeskSensePi/.env`.
