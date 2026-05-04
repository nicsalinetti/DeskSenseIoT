#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <math.h>

extern "C" {
#include "user_interface.h"
#include "wpa2_enterprise.h"
#include "arduino_secrets.h"
}

// --- CONFIGURATION ---
const char* ssid = SECRET_SSID;
const char* username = SECRET_USER;
const char* password = SECRET_PASS;
const char* mqtt_server = SECRET_MQTT;

WiFiClient espClient;
PubSubClient client(espClient);

#define PIEZO_PIN A0 
#define TRIG_PIN   D7 
#define ECHO_PIN   D6 

// --- MODEL & BIAS PARAMETERS ---
const float MEAN_PEAK = 26.95124, SCALE_PEAK = 13.49185;
const float MEAN_LOGAVG = 2.55127, SCALE_LOGAVG = 0.03253;
const float C1_PEAK_SCALED = (23.52 - MEAN_PEAK) / SCALE_PEAK;
const float C1_LOGAVG_SCALED = (2.5432 - MEAN_LOGAVG) / SCALE_LOGAVG;
const float C2_PEAK_SCALED = (61.00 - MEAN_PEAK) / SCALE_PEAK;
const float C2_LOGAVG_SCALED = (2.6314 - MEAN_LOGAVG) / SCALE_LOGAVG;

const float SENSITIVITY_BIAS = 1.2; 
const float REPORT_THRESHOLD = 3.0; 

const unsigned long POLL_INTERVAL = 1000;      
unsigned long VACANCY_THRESHOLD = 600000; // Default 10 mins (Changed to non-const)
const int SAMPLE_WINDOW = 250; 
int MAX_RAW_THRESHOLD = 300;

// --- STATE GLOBALS ---
bool isOccupied = false;
unsigned long lastSeenTime = 0;
unsigned long lastPollTime = 0;
int cluster2Hits = 0;
int totalSamples = 0;

// MQTT Callback to handle incoming messages from Flask
void callback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (int i = 0; i < length; i++) { message += (char)payload[i]; }
  
  if (String(topic) == "occupancy/set_timeout") {
    int minutes = message.toInt();
    VACANCY_THRESHOLD = minutes * 60000; // Convert mins to ms
    Serial.print("\n[CONFIG] Vacancy Timeout updated to: ");
    Serial.print(minutes); Serial.println(" minutes");
  }
}

void setup_wifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid);
  wifi_station_set_wpa2_enterprise_auth(1);
  wifi_station_set_enterprise_username((uint8*)username, strlen(username));
  wifi_station_set_enterprise_password((uint8*)password, strlen(password));
  wifi_station_connect();
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\nWiFi Connected");
}

void reconnect() {
  while (!client.connected()) {
    if (client.connect("OccupancyNodeMCU")) {
      client.subscribe("occupancy/set_timeout"); // Subscribe to the control topic
      client.publish("occupancy/status", "NodeMCU Online");
    } else { delay(5000); }
  }
}

float getDistance() {
  digitalWrite(TRIG_PIN, LOW); delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  return (duration == 0) ? 999 : duration * 0.034 / 2;
}

void setup() {
  Serial.begin(115200);
  pinMode(PIEZO_PIN, INPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback); // Attach the listener
}

void loop() {
  if (!client.connected()) reconnect();
  client.loop();

  unsigned long now = millis();

  if (now - lastPollTime >= POLL_INTERVAL) {
    lastPollTime = now;
    float currentDist = getDistance(); // Get distance at start of poll
    
    unsigned long startSample = millis();
    int peakRaw = 0; unsigned long sumRaw = 0; unsigned long count = 0;
    
    while (millis() - startSample < SAMPLE_WINDOW) {
      int val = analogRead(PIEZO_PIN);
      if (val > peakRaw) peakRaw = val;
      sumRaw += val; count++;
    }

    if (currentDist < 150.0) {
      lastSeenTime = now; 
      totalSamples++;
      
      if (peakRaw <= MAX_RAW_THRESHOLD) {
        float avgRaw = (float)sumRaw / count;
        float logAvgRaw = log(1.0 + avgRaw);
        float peakScaled = (peakRaw - MEAN_PEAK) / SCALE_PEAK;
        float logAvgScaled = (logAvgRaw - MEAN_LOGAVG) / SCALE_LOGAVG;
        float d1 = pow(peakScaled - C1_PEAK_SCALED, 2) + pow(logAvgScaled - C1_LOGAVG_SCALED, 2);
        float d2 = pow(peakScaled - C2_PEAK_SCALED, 2) + pow(logAvgScaled - C2_LOGAVG_SCALED, 2);

        // --- UPDATED PRINT LINE ---
        if (d2 < (d1 * SENSITIVITY_BIAS)) {
          cluster2Hits++;
          Serial.print("[Scan: 2 | Dist: "); Serial.print(currentDist); Serial.print("cm] ");
        } else {
          Serial.print("[Scan: 1 | Dist: "); Serial.print(currentDist); Serial.print("cm] ");
        }
      }

      if (!isOccupied) {
        isOccupied = true;
        client.publish("occupancy/status", "Occupied");
      }
      
      if (totalSamples % 10 == 0) {
        float percentTwo = ((float)cluster2Hits / totalSamples) * 100.0;
        Serial.println();
        Serial.print(">>> TOTAL SAMPLES: "); Serial.print(totalSamples);
        Serial.print(" | PERCENTAGE: "); Serial.print(percentTwo); Serial.println("%");
        
        const char* countVal = (percentTwo >= REPORT_THRESHOLD) ? "2" : "1";
        client.publish("occupancy/count", countVal);
      }

    } else {
      // Periodic print even when vacant so you can see why it's not triggering
      Serial.print("[Vacant | Dist: "); Serial.print(currentDist); Serial.println("cm]");
      
      if (isOccupied && (now - lastSeenTime > VACANCY_THRESHOLD)) {
        isOccupied = false;
        cluster2Hits = 0; totalSamples = 0;
        Serial.println("\nTABLE VACANT - THRESHOLD REACHED");
        client.publish("occupancy/status", "Vacant");
        client.publish("occupancy/count", "0");
      }
    }
  }
}