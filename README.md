# DeskSense
# IoT Table Occupancy Monitor

An Edge AI-powered occupancy system using a **NodeMCU (ESP8266)**, **Ultrasonic sensing**, and **K-Means Clustering** on vibration data to distinguish between 0, 1, and 2 people at a desk.

##  Features
*   **Presence Detection:** Uses ultrasonic distance sensing to trigger the "Occupied" state.
*   **Edge AI Count:** Implements a K-Means clustering model on-device to analyze piezo-vibration signatures in real-time.
*   **Real-time Dashboard:** Flask + Socket.IO web interface for live status and count monitoring.
*   **Dynamic Configuration:** Adjust vacancy timeout (1 min vs 10 min) directly from the web app via MQTT.
*   **Enterprise WiFi Support:** Native compatibility for WPA2 Enterprise networks (e.g., University/Office networks).

---

##  Hardware Requirements
*   **NodeMCU (ESP8266)**
*   **HC-SR04 Ultrasonic Sensor** (Best powered by 5V/Vin)
*   **Piezo Vibration Sensor** (Connected to Analog A0)
*   **Raspberry Pi** (To host the Mosquitto MQTT Broker and Flask App)

---

## Installation & Setup
### 1. MQTT Broker (Raspberry Pi)
Ensure you have an MQTT broker like Mosquitto installed and running on your Pi.

    sudo apt update
    sudo apt install mosquitto mosquitto-clients
    sudo systemctl enable mosquitto

For a secured setup, do **not** use anonymous MQTT. Example broker files are included in `DeskSensePi/mosquitto/`.

1. Create a broker password file:
    ```bash
    sudo mosquitto_passwd -c /etc/mosquitto/passwd desksense_esp
    sudo mosquitto_passwd /etc/mosquitto/passwd desksense_app
    ```
2. Install the sample ACL and listener config:
    ```bash
    sudo cp DeskSensePi/mosquitto/desksense.conf /etc/mosquitto/conf.d/desksense.conf
    sudo cp DeskSensePi/mosquitto/aclfile /etc/mosquitto/aclfile
    sudo systemctl restart mosquitto
    ```
3. For stronger transport security, enable the TLS listener in `desksense.conf` after installing your local CA/server certificate files.

### 2. ESP8266 Configuration
Open the Arduino IDE and navigate to the `DeskSenseESP` folder.

Create a new tab/file named arduino_secrets.h.

Use the following format to add your credentials:

    #define SECRET_SSID "your_network_name"
    #define SECRET_USER "your_wpa2_username"
    #define SECRET_PASS "your_password"
    #define SECRET_MQTT "your_pi_ip_address"
    #define SECRET_MQTT_USER "desksense_esp"
    #define SECRET_MQTT_PASS "your_mqtt_password"
    
Install the **PubSubClient** library via the Library Manager.
Upload the code to your NodeMCU.

### 3. Flask Dashboard Setup
1. Navigate to the `flask_app` folder on your Raspberry Pi.
2. Create an environment file from the example and edit credentials:
    ```bash
    cp .env.example .env
    nano .env
    ```
3. Install the necessary Python libraries:
    ```bash
    pip install -r requirements.txt
    ```
4. Run the application:
    ```bash
    python app.py
    ```
5. Open your browser and go to `http://<your-pi-ip>:5000`.

The dashboard now requires HTTP Basic authentication. Configure `DASHBOARD_USERNAME` and `DASHBOARD_PASSWORD` in `.env`.

---

## Security Measures
*   **IEEE 802.1X / WPA2 Enterprise:** ESP8266 WiFi setup supports enterprise authentication for managed networks.
*   **MQTT Authentication:** Mosquitto examples disable anonymous access and require per-client credentials.
*   **Config Isolation:** Secrets are loaded from `arduino_secrets.h` and `.env`, both excluded from version control.

---

##  Testing & Tuning
*   **Calibration:** Use the Serial Monitor (115200 baud) to view real-time distance readings and vibration cluster percentages.
*   **Sensitivity:** Adjust the `SENSITIVITY_BIAS` in the ESP8266 code to favor Cluster 2 if quiet occupants aren't being detected.
*   **Testing Mode:** Use the "Set 1 Min" button on the web app to quickly test the vacancy reset logic without waiting the full 10 minutes.

---

##  Security Note
This project uses an `arduino_secrets.h` file and a Pi-side `.env` file to store network credentials. These files are ignored by git so passwords are not uploaded to a public repository. Use `DeskSenseESP/arduino_secrets_template.h` and `DeskSensePi/.env.example` as setup references.

---

##  License
This project is for educational use.
