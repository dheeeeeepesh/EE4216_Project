#pragma once
#include <WiFi.h>   // provides the IPAddress type

#define CONFIG_VERSION 4

// ---------------------------------------------------------------------
// Local sensor network (the gateway's AP side)
// ---------------------------------------------------------------------
#define GW_AP_SSID         "EE4216-grp4"
#define GW_AP_PASSWORD     "password4216"  
#define GW_AP_CHANNEL      6              
#define GW_AP_MAX_CLIENTS  4            

const IPAddress GW_AP_IP     (192, 168, 4, 1);
const IPAddress GW_AP_SUBNET (255, 255, 255, 0);
const IPAddress NODE1_IP     (192, 168, 4, 11);
const IPAddress NODE2_IP     (192, 168, 4, 12);

#define HTTP_PORT 80

// Node identifiers (sent inside every JSON message)
#define NODE1_ID "node1"   // HC-SR04 (distance) + RGB LED stick
#define NODE2_ID "node2"   // DHT22 (temperature + humidity) + Light sensor module

// HTTP endpoints
#define EP_DATA   "/api/data"     // node -> gateway, POST, sensor readings
#define EP_CMD    "/api/cmd"      // gateway -> node, POST, actuator command
#define EP_STATUS "/api/status"   // dashboard -> gateway, GET, everything the page shows

// Timing (milliseconds)
constexpr uint32_t NODE_POST_INTERVAL_MS    = 5000;   // how often each node posts
constexpr uint32_t NODE_STALE_MS            = 12000;  // missed ~2 posts: data is "stale"
constexpr uint32_t NODE_OFFLINE_MS          = 30000;  // missed ~6 posts: node is "offline"
constexpr uint32_t CMD_HTTP_TIMEOUT_MS      = 3000;   // gateway gives up waiting for a node ack
constexpr uint32_t MQTT_PUBLISH_INTERVAL_MS = 20000;  // ThingSpeak free tier allows ~1 update / 15 s

// Validation ranges. Both sides use these: nodes can skip obviously bad
// reads, and the gateway rejects anything outside them with HTTP 400.
// Ranges come from each sensor's datasheet limits.
constexpr float TEMP_MIN_C       = -40.0f, TEMP_MAX_C       = 80.0f;   // DHT22
constexpr float HUMIDITY_MIN_PCT =   0.0f, HUMIDITY_MAX_PCT = 100.0f;  // DHT22
constexpr float DISTANCE_MIN_CM  =   2.0f, DISTANCE_MAX_CM  = 400.0f;  // HC-SR04
constexpr float LIGHT_MIN_PCT    =   0.0f, LIGHT_MAX_PCT    = 100.0f;  // light sensor, scaled to %
