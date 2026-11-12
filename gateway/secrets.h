#pragma once

// Upstream Internet (phone hotspot), used by the STA interface
#define HOTSPOT_SSID        ""
#define HOTSPOT_PASSWORD    ""

// ThingSpeak MQTT broker (used from Stage 3 onward)
// Port 1883 is unencrypted MQTT. It's easier to debug, so we start here.
// Port 8883 (TLS) is a possible "extra feature" upgrade later.
#define MQTT_HOST       "mqtt3.thingspeak.com"
#define MQTT_PORT       1883

// From ThingSpeak: Devices > MQTT > Add a new device.
// Authorize the device for publish AND subscribe on all three channels.
#define MQTT_CLIENT_ID  
#define MQTT_USERNAME   
#define MQTT_PASSWORD   

// Channel IDs (numbers shown on each channel's page)
#define TS_DATA_CHANNEL_ID  
#define TS_CMD_CHANNEL_ID  
#define TS_ACK_CHANNEL_ID   
