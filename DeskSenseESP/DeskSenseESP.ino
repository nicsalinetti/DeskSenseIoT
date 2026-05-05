#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <math.h>

// Required for WPA2 Enterprise authentication (e.g., school WiFi)
extern "C" {
#include "user_interface.h"
#include "wpa2_enterprise.h"
#include "arduino_secrets.h"
}

// ================== CONFIGURATION ==================
// WiFi + MQTT credentials stored securely in arduino_secrets.h
const char* ssid = SECRET_SSID;
const char* username = SECRET_USER;
const char* password = SECRET_PASS;
const char* mqtt_server = SECRET_MQTT;
const char* mqtt_user = SECRET_MQTT_USER;
const char* mqtt_password = SECRET_MQTT_PASS;

// Networking clients
WiFiClient espClient;
PubSubClient client(espClient);

// ================== PIN DEFINITIONS ==================
#define PIEZO_PIN A0   // Piezo vibration sensor (analog input)
#define TRIG_PIN   D7  // Ultrasonic trigger
#define ECHO_PIN   D6  // Ultrasonic echo

// ================== MODEL PARAMETERS ==================
// Precomputed normalization values (from training dataset)
const float MEAN_PEAK = 26.95124, SCALE_PEAK = 13.49185;
const float MEAN_LOGAVG = 2.55127, SCALE_LOGAVG = 0.03253;

// Cluster centers (scaled) for classification (1-person vs 2-person)
const float C1_PEAK_SCALED = (23.52 - MEAN_PEAK) / SCALE_PEAK;
const float C1_LOGAVG_SCALED = (2.5432 - MEAN_LOGAVG) / SCALE_LOGAVG;

const float C2_PEAK_SCALED = (61.00 - MEAN_PEAK) / SCALE_PEAK;
const float C2_LOGAVG_SCALED = (2.6314 - MEAN_LOGAVG) / SCALE_LOGAVG;

// Bias factor to slightly favor 1-person classification (reduce false 2s)
const float SENSITIVITY_BIAS = 1.2;

// % threshold of "2" detections required to report 2 people
const float REPORT_THRESHOLD = 15;

// ================== TIMING + SAMPLING ==================
const unsigned long POLL_INTERVAL = 1000;   // Time between scans (ms)
unsigned long VACANCY_THRESHOLD = 600000;   // Time before marking vacant (ms)
const int SAMPLE_WINDOW = 250;              // Piezo sampling window (ms)

// Ignore extreme spikes above this raw value (noise filtering)
int MAX_RAW_THRESHOLD = 300;

// ================== SLIDING WINDOW ==================
// Stores last 10 classification results (1 or 2)
#define WINDOW_SIZE 10
int recentScans[WINDOW_SIZE] = {0};
int scanIndex = 0;
int scanCount = 0;

// ================== STATE VARIABLES ==================
bool isOccupied = false;
unsigned long lastSeenTime = 0;
unsigned long lastPollTime = 0;

// ================== MQTT CALLBACK ==================
// Handles incoming MQTT messages (dynamic configuration)
void callback(char* topic, byte* payload, unsigned int length) {
  String message = "";

  // Convert payload to string
  for (int i = 0; i < length; i++) message += (char)payload[i];

  // Allow remote update of vacancy timeout
  if (String(topic) == "occupancy/set_timeout") {
    int minutes = message.toInt();
    VACANCY_THRESHOLD = minutes * 60000;

    Serial.print("\n[CONFIG] Vacancy Timeout updated to: ");
    Serial.print(minutes); Serial.println(" minutes");
  }
}

// ================== WIFI SETUP ==================
void setup_wifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid);

  // WPA2 Enterprise authentication (username/password instead of PSK)
  wifi_station_set_wpa2_enterprise_auth(1);
  wifi_station_set_enterprise_username((uint8*)username, strlen(username));
  wifi_station_set_enterprise_password((uint8*)password, strlen(password));
  wifi_station_connect();

  // Wait until connected
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi Connected");
}

// ================== MQTT RECONNECT ==================
void reconnect() {
  while (!client.connected()) {
    if (client.connect("OccupancyNodeMCU", mqtt_user, mqtt_password)) {
      client.subscribe("occupancy/set_timeout");
      client.publish("occupancy/status", "NodeMCU Online");
    } else {
      delay(5000); // Retry delay
    }
  }
}

// ================== ULTRASONIC DISTANCE ==================
// Returns distance in cm. If no echo, return large value (no object)
float getDistance() {
  digitalWrite(TRIG_PIN, LOW); delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 30000);

  // Convert time-of-flight to distance
  return (duration == 0) ? 999 : duration * 0.034 / 2;
}

// ================== SETUP ==================
void setup() {
  Serial.begin(115200);

  pinMode(PIEZO_PIN, INPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  setup_wifi();

  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
}

// ================== MAIN LOOP ==================
void loop() {
  if (!client.connected()) reconnect();
  client.loop();

  unsigned long now = millis();

  // Run scan every POLL_INTERVAL
  if (now - lastPollTime >= POLL_INTERVAL) {
    lastPollTime = now;

    float currentDist = getDistance();

    // ================== PIEZO SAMPLING ==================
    // Capture vibration signal over short window
    unsigned long startSample = millis();
    int peakRaw = 0;
    unsigned long sumRaw = 0;
    unsigned long count = 0;

    while (millis() - startSample < SAMPLE_WINDOW) {
      int val = analogRead(PIEZO_PIN);

      if (val > peakRaw) peakRaw = val;  // Track max vibration
      sumRaw += val;                     // Accumulate for average
      count++;
    }

    // ================== OCCUPANCY DETECTION ==================
    // Only process if object detected within 50 cm
    if (currentDist < 50.0) {
      lastSeenTime = now;

      // Ignore extreme noise spikes
      if (peakRaw <= MAX_RAW_THRESHOLD) {

        // Feature extraction
        float avgRaw = (float)sumRaw / count;
        float logAvgRaw = log(1.0 + avgRaw);  // Log transform reduces skew

        // Normalize features (match training scale)
        float peakScaled = (peakRaw - MEAN_PEAK) / SCALE_PEAK;
        float logAvgScaled = (logAvgRaw - MEAN_LOGAVG) / SCALE_LOGAVG;

        // ================== CLASSIFICATION ==================
        // Distance to each cluster center (Euclidean distance)
        float d1 = pow(peakScaled - C1_PEAK_SCALED, 2) +
                   pow(logAvgScaled - C1_LOGAVG_SCALED, 2);

        float d2 = pow(peakScaled - C2_PEAK_SCALED, 2) +
                   pow(logAvgScaled - C2_LOGAVG_SCALED, 2);

        int currentScan;

        // Apply bias to reduce false positives for 2-person detection
        if (d2 < (d1 * SENSITIVITY_BIAS)) {
          currentScan = 2;
          Serial.print("[Scan: 2 | Dist: "); Serial.print(currentDist); Serial.print("cm] ");
        } else {
          currentScan = 1;
          Serial.print("[Scan: 1 | Dist: "); Serial.print(currentDist); Serial.print("cm] ");
        }

        // ================== SLIDING WINDOW ==================
        // Store result in circular buffer
        recentScans[scanIndex] = currentScan;
        scanIndex = (scanIndex + 1) % WINDOW_SIZE;

        if (scanCount < WINDOW_SIZE) scanCount++;

        // Count number of "2" detections in window
        int count2 = 0;
        for (int i = 0; i < scanCount; i++) {
          if (recentScans[i] == 2) count2++;
        }

        // Compute percentage of 2-person detections
        float percentTwo = ((float)count2 / scanCount) * 100.0;

        Serial.print(" | Last10 2-person %: ");
        Serial.println(percentTwo);

        // Final decision based on threshold
        const char* countVal = (percentTwo >= REPORT_THRESHOLD) ? "2" : "1";

        // Publish result
        client.publish("occupancy/count", countVal);
      }

      // Update occupancy state
      if (!isOccupied) {
        isOccupied = true;
        client.publish("occupancy/status", "Occupied");
      }

    } else {
      // ================== VACANCY DETECTION ==================
      Serial.print("[Vacant | Dist: ");
      Serial.print(currentDist);
      Serial.println("cm]");

      // If no presence detected for threshold duration → mark vacant
      if (isOccupied && (now - lastSeenTime > VACANCY_THRESHOLD)) {
        isOccupied = false;

        // Reset sliding window when table becomes empty
        scanCount = 0;
        scanIndex = 0;

        Serial.println("\nTABLE VACANT - RESETTING WINDOW");

        client.publish("occupancy/status", "Vacant");
        client.publish("occupancy/count", "0");
      }
    }
  }
}
