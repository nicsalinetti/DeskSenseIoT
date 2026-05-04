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
Note: Ensure your mosquitto.conf includes listener 1883 and allow_anonymous true to allow the NodeMCU to connect.

### 2. ESP8266 Configuration
Open the Arduino IDE and navigate to the esp8266_code folder.

Create a new tab/file named arduino_secrets.h.

Use the following format to add your credentials:

    #define SECRET_SSID "your_network_name"
    #define SECRET_USER "your_wpa2_username"
    #define SECRET_PASS "your_password"
    #define SECRET_MQTT "your_pi_ip_address"
    
Install the **PubSubClient** library via the Library Manager.
Upload the code to your NodeMCU.

### 3. Flask Dashboard Setup
1. Navigate to the `flask_app` folder on your Raspberry Pi.
2. Install the necessary Python libraries:
    ```bash
    pip install flask flask-socketio paho-mqtt
    ```
3. Run the application:
    ```bash
    python app.py
    ```
4. Open your browser and go to `http://<your-pi-ip>:5000`.

---

##  Testing & Tuning
*   **Calibration:** Use the Serial Monitor (115200 baud) to view real-time distance readings and vibration cluster percentages.
*   **Sensitivity:** Adjust the `SENSITIVITY_BIAS` in the ESP8266 code to favor Cluster 2 if quiet occupants aren't being detected.
*   **Testing Mode:** Use the "Set 1 Min" button on the web app to quickly test the vacancy reset logic without waiting the full 10 minutes.

---

##  Security Note
This project uses an `arduino_secrets.h` file to store network credentials. This file is included in the `.gitignore` to ensure your passwords are never uploaded to a public GitHub repository. Use `arduino_secrets_template.h` as a reference for new setups.

---

##  License
This project is for educational use.
