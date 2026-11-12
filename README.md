EE4216 Mini-Project: Bidirectional IoT Network
1. Hardware Components
Node 1 — Distance Sensing and RGB Control
	•	ESP32-S3 development board
	•	HC-SR04 ultrasonic sensor
	•	8-pixel RGB LED stick
	•	Push button for AUTO/MANUAL mode switching
	•	LED for operating-mode indication
Node 2 — Environmental Monitoring
	•	ESP32-S3 development board
	•	DHT22 temperature and humidity sensor
	•	Light sensor module
Gateway
	•	ESP32-S3 development board
	•	Internet-enabled Wi-Fi hotspot for upstream connectivity
2. Repository Structure
EE4216_Project/
│
├── Gateway/
│   ├── Gateway.ino
│   ├── config.h
│   ├── gateway_types.h
│   ├── dashboard_html.h
│   └── secrets.h
│
├── Node1/
│   ├── Node1.ino
│   └── config.h
│
├── Node2/
│   ├── Node2.ino
│   └── config.h
│
└── README.md

3. Software Requirements
	•	Arduino IDE
	•	ESP32 board support package
	•	Required Arduino libraries, including:
	◦	WiFi
	◦	WebServer
	◦	HTTPClient
	◦	ArduinoJson
	◦	PubSubClient
	◦	Adafruit NeoPixel
	◦	DHT sensor library
	•	ThingSpeak account with MQTT device access
	•	Internet-enabled Wi-Fi hotspot
4. Configuration and Credentials
The secrets.h file is included in the repository. However, all Wi-Fi hotspot credentials and ThingSpeak-related configuration values have been cleared for security and privacy reasons.
The file retains the required configuration definitions, allowing users to enter their own credentials and channel information before compiling the gateway firmware.
Users must provide:
	•	Wi-Fi hotspot SSID and password
	•	ThingSpeak MQTT client ID
	•	ThingSpeak MQTT username and password
	•	ThingSpeak sensor-data channel ID
	•	ThingSpeak remote-command channel ID
	•	ThingSpeak acknowledgment channel ID
The MQTT broker address and port are configured for ThingSpeak.
Note: Actual credentials have intentionally been omitted from the public repository.
5. Setup Instructions
Step 1 — Install Arduino IDE and Libraries
Step 2 — Configure ThingSpeak
	1	Create three channels:
	◦	Sensor Data — stores measurements and node status.
	◦	Remote Commands — accepts actuator-control commands.
	◦	Acknowledgments — records command execution results.
	2	Create an MQTT device under Devices > MQTT.
	3	Authorize the MQTT device to publish and subscribe to the three channels.
	4	Obtain the MQTT client ID, username, and password.
Step 3 — Configure secrets.h
Open Gateway/secrets.h.
The hotspot credentials and ThingSpeak-related values have been intentionally left empty in the repository.
Replace these empty values with your own hotspot credentials, MQTT authentication details, and ThingSpeak channel IDs.
Step 4 — Configure the Local Network
The local Wi-Fi network and IP addressing scheme are defined in config.h.
Default addressing:
Device
IP Address
Gateway
192.168.4.1
Node 1
192.168.4.11
Node 2
192.168.4.12
Ensure that all three sketches use matching network configuration settings.
Step 5 — Upload the Firmware
	1	Open Gateway/Gateway.ino in Arduino IDE.
	2	Compile and upload it to the gateway ESP32-S3.
	3	Open Node1/Node1.ino and upload it to the first sensor node.
	4	Open Node2/Node2.ino and upload it to the second sensor node.
Ensure that the relevant header files are present in each sketch directory.
Step 6 — Start the IoT Network
	1	Enable the configured Internet hotspot.
	2	Power on the ESP32-S3 gateway.
	3	Wait for the gateway to establish its hotspot and MQTT connections.
	4	Power on both sensor nodes.
	5	Verify that both nodes connect to the gateway and begin transmitting sensor measurements.
Step 7 — Access the Local Dashboard
Connect a laptop or mobile device to the gateway's local Wi-Fi network.
Open a web browser and navigate to:
http://192.168.4.1
The dashboard displays sensor measurements, node connectivity, cloud connection status, and RGB LED controls.
Step 8 — Test Remote RGB Control
	1	Switch Node 1 to MANUAL mode using the physical push button.
	2	Open the ThingSpeak Remote Commands channel.
	3	Send a command such as: led:set:255,0,0
	4	Verify that the RGB LED stick turns red.
	5	Check the ThingSpeak Acknowledgment channel for the execution result.
In AUTO mode, the RGB stick responds automatically to sensor conditions instead of accepting manual colour commands.
10. Project Documentation
The accompanying EE4216 project report contains the system architecture, implementation details, communication protocols, dashboard design, additional features, testing procedures, and results analysis.
The source files in this repository provide the complete firmware implementation for the gateway and both sensor nodes.
11. Course Information
Course: EE4216
Project: Design and Implementation of a Bidirectional IoT Network
Development Platform: ESP32-S3
Cloud Platform: ThingSpeak
