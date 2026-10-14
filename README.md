# BLE_MESH
# BLE Mesh-Based Fire Alarm and Sensor Monitoring System

## Overview
A wireless fire monitoring and alert system built with ESP32 microcontrollers and Bluetooth Low Energy (BLE) Mesh. The project demonstrates distributed communication between sensor nodes, emergency input nodes, and alarm nodes, with MQTT-based remote monitoring integration.

## Key Features
- **BLE Mesh Networking:** Implemented node provisioning, model configuration, and group-based publish/subscribe communication.
- **Environmental Monitoring:** Integrated DHT11 temperature and humidity sensing with an MQ2 gas and smoke sensor.
- **Emergency Alerting:** Supported manual emergency triggering and sensor-based fire-level evaluation.
- **Distributed Alarm Control:** Used the Generic OnOff Model to synchronize buzzer and LED alerts across subscribed nodes.
- **Custom Vendor Models:** Implemented application-specific messages for transmitting sensor readings and emergency events.
- **Low-Power Communication:** Explored the BLE Mesh Low Power Node (LPN) and Friend mechanism for message buffering and periodic polling.
- **MQTT Integration:** Forwarded sensor telemetry and emergency events through a gateway to an MQTT broker and backend notification service.

## System Architecture
The prototype consists of seven ESP32/ESP32-C3 nodes with provisioning, client/server, sensing, and alarm-control responsibilities.

The system combines two communication layers:

- **Local network:** BLE Mesh handles sensor-event distribution and coordinated alarm activation.
- **Remote monitoring:** An ESP32 gateway bridges BLE Mesh data to an MQTT broker, enabling backend processing and email notifications.

Local alarm operation is designed to remain independent of internet connectivity.

## Technologies
- **Hardware:** ESP32, ESP32-C3, DHT11, MQ2, buzzers, LEDs, emergency push button
- **Embedded Development:** ESP-IDF, FreeRTOS, C
- **Wireless Networking:** Bluetooth Low Energy Mesh, Generic OnOff Model, Custom Vendor Models
- **IoT Communication:** Wi-Fi, MQTT, HiveMQ Cloud
- **Backend:** Node.js, SMTP/TLS email notifications

## Testing and Results
- Verified manual emergency-trigger transmission and multi-node alarm activation.
- Tested sensor acquisition and fire-level evaluation using temperature, humidity, and gas/smoke readings.
- Demonstrated sensor-event forwarding from BLE Mesh to the MQTT backend.
- Evaluated LPN–Friend message buffering and periodic polling; the report observed approximately 17 seconds for a message to reach the client in the tested LPN scenario.

## Limitations and Future Improvements
The current implementation is a small-scale prototype tested with seven nodes. Further work includes improving sensor accuracy, testing larger multi-floor deployments, optimizing power consumption, and expanding remote monitoring capabilities.

## Project Context
Academic project — Wireless Network Embedded System  
University of Information Technology, VNU-HCM | 2026
