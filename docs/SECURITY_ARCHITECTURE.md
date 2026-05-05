# DeskSense Security Architecture

This document maps the project security controls to IEEE-oriented IoT and network security expectations.

## Relevant IEEE Areas

- IEEE 2413: IoT architecture documentation, trust boundaries, and subsystem responsibilities.
- IEEE 802.1X: authenticated network access for enterprise WiFi deployments.
- IEEE 802.11 security practices: protected WiFi association and transport-layer protection for application traffic.

## Trust Boundaries

1. Sensor/device layer: ESP8266, ultrasonic sensor, and piezo sensor.
2. Network layer: WPA2 Enterprise or isolated IoT SSID/VLAN.
3. Broker layer: Mosquitto MQTT broker with per-client credentials and ACLs.
4. Application layer: Flask dashboard with authenticated access.
5. User/admin layer: browser users and SSH administrators.

## Implemented Controls

- ESP8266 stores WiFi and MQTT credentials in `arduino_secrets.h`, which is ignored by git.
- Flask loads MQTT and dashboard secrets from environment variables or `.env`.
- Mosquitto sample config disables anonymous MQTT clients.
- Mosquitto ACLs restrict who can read or write each DeskSense topic.
- Flask validates timeout commands before publishing to MQTT.
- ESP8266 validates timeout commands again before applying them.
- Flask logs occupancy, device status, and timeout changes.
- Optional MQTT TLS settings are included for encrypted broker communication.

## Recommended Deployment Controls

- Use WPA2 Enterprise / IEEE 802.1X when available.
- Place the ESP8266 and Raspberry Pi on an IoT VLAN or isolated SSID.
- Permit only required traffic: ESP8266 to Mosquitto, dashboard users to Flask, and admin SSH from trusted hosts.
- Use SSH keys, disable password SSH login, and change default Raspberry Pi credentials.
- Run the Flask dashboard as a non-root systemd service.
- Put Flask behind Nginx with HTTPS for production or shared-network use.
- Rotate MQTT passwords when devices or maintainers change.
- Keep the Pi patched with unattended security updates.

## Residual Risks

- The ESP8266 firmware uses MQTT username/password authentication but does not enable MQTT TLS by default because TLS certificate handling on ESP8266 deployments is hardware- and memory-sensitive.
- Occupancy data can be privacy-sensitive. Retain logs only as long as needed for operations or testing.
- Physical access to the device can expose firmware and sensor wiring. Use physical enclosure controls in public deployments.
