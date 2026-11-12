#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>
#include "config.h"

#define TRIG_PIN  5
#define ECHO_PIN  18
#define LED_PIN   7
#define NUMPIXELS 8

#define BUTTON_PIN 9
#define MODE_LED_PIN 10

constexpr uint32_t DEBOUNCE_MS = 50;
constexpr uint32_t AUTO_UPDATE_MS = 250;

bool autoMode = false;  // Start in MANUAL mode

constexpr uint32_t NODE2_FETCH_MS = 5000;

uint32_t lastNode2FetchMs = 0;

float node2Light = -1;
float node2Humidity = -1;
bool node2Fresh = false;


bool lastButtonReading = HIGH;
bool stableButtonState = HIGH;

uint32_t lastDebounceMs = 0;
uint32_t lastAutoUpdateMs = 0;

int lastAutoColour = -1;


constexpr uint32_t POST_TIMEOUT_MS = 3000;   // give up on a POST to the gateway after this
constexpr uint32_t WIFI_RETRY_MS   = 5000;   // wait between reconnect attempts

Adafruit_NeoPixel strip(NUMPIXELS, LED_PIN, NEO_GRB + NEO_KHZ800);
WebServer server(HTTP_PORT);

String   dataUrl;
uint32_t lastSend = 0, lastWifiTry = 0, seq = 0;

// ---------- Sensor ----------
float readDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long duration = pulseIn(ECHO_PIN, HIGH, 30000);   // 30000 us = 30 ms timeout
  if (duration == 0) return -1;
  return duration * 0.0343 / 2;
}

void fetchNode2Readings() {
  if (WiFi.status() != WL_CONNECTED) {
    node2Fresh = false;
    return;
  }

  HTTPClient http;
  String url = String("http://") +
               GW_AP_IP.toString() + EP_STATUS;

  http.setConnectTimeout(500);
  http.setTimeout(1000);
  http.begin(url);

  int code = http.GET();

  if (code == 200) {
    JsonDocument doc;

    if (!deserializeJson(doc, http.getString())) {
      JsonVariant node2 = doc["nodes"]["node2"];

      const char* state = node2["state"] | "";

      if (strcmp(state, "online") == 0 &&
          node2["readings"]["light_pct"].is<float>() &&
          node2["readings"]["humidity_pct"].is<float>()) {

        node2Light =
          node2["readings"]["light_pct"].as<float>();

        node2Humidity =
          node2["readings"]["humidity_pct"].as<float>();

        node2Fresh = true;

        Serial.printf(
          "[NODE2] Light: %.1f%%, Humidity: %.1f%%\n",
          node2Light, node2Humidity
        );

      } else {
        node2Fresh = false;
      }
    } else {
      node2Fresh = false;
    }
  } else {
    node2Fresh = false;
    Serial.printf("[NODE2] HTTP error: %d\n", code);
  }

  http.end();
}

void setRgb(int r, int g, int b) {
  strip.fill(strip.Color(r, g, b));
  strip.show();
}


void updateAutoRgb() {
  if (!autoMode) return;

  uint32_t now = millis();

  if (now - lastAutoUpdateMs < AUTO_UPDATE_MS)
    return;

  lastAutoUpdateMs = now;

  float distance = readDistance();

  bool distanceValid =
    distance >= DISTANCE_MIN_CM &&
    distance <= DISTANCE_MAX_CM;

  int colour;

  // Priority 1: Proximity

  if (distanceValid && distance < 15.0f) {
    colour = 1; // Red - Proximity
  }
  else if (!node2Fresh) {
    colour = 5; // Purple - Node 2 offline/stale
  }
  else if (node2Light > 90.0f) {
    colour = 2; // Orange - Light
  }
  else if (node2Humidity > 95.0f) {
    colour = 3; // Yellow - Humidity
  }
  else {
    colour = 4; // Green - Normal
  }

  if (colour == lastAutoColour) return;

  lastAutoColour = colour;

  switch (colour) {
    case 1:
      setRgb(255, 0, 0);
      break;

    case 2:
      setRgb(255, 100, 0);
      break;

    case 3:
      setRgb(255, 255, 0);
      break;

    case 4:
      setRgb(0, 255, 0);
      break;

    case 5:
      setRgb(128,0,128);
      break;
  }

  Serial.printf(
    "[AUTO] Distance: %.1f, Light: %.1f, "
    "Humidity: %.1f, Colour: %d\n",
    distance, node2Light, node2Humidity, colour
  );
}

void setMode(bool automatic) {
  if (autoMode == automatic) return;

  autoMode = automatic;

  digitalWrite(
    MODE_LED_PIN,
    autoMode ? HIGH : LOW
  );

  lastAutoColour = -1;
  lastAutoUpdateMs = millis() - AUTO_UPDATE_MS;

  if (!autoMode) {
    // Switching to MANUAL: turn RGB off
    strip.clear();
    strip.show();

    Serial.println("[MODE] MANUAL - RGB OFF");
  } else {
    // Switching to AUTO: refresh remote readings
    lastNode2FetchMs = millis() - NODE2_FETCH_MS;

    Serial.println("[MODE] AUTO");
  }
}

void checkModeButton() {
  bool reading = digitalRead(BUTTON_PIN);

  if (reading != lastButtonReading) {
    lastDebounceMs = millis();
  }

  if (millis() - lastDebounceMs >= DEBOUNCE_MS) {
    if (reading != stableButtonState) {
      stableButtonState = reading;

      if (stableButtonState == LOW) {
        setMode(!autoMode);
      }
    }
  }

  lastButtonReading = reading;
}

// ---------- Uplink ----------
void sendReading() {
  if (WiFi.status() != WL_CONNECTED) return;

  float d = readDistance(); 
  if (d < DISTANCE_MIN_CM || d > DISTANCE_MAX_CM) {   // also catches -1 (no echo)
    Serial.println("Invalid/no echo, not sent");
    return;
  }

  JsonDocument doc;
  doc["node_id"] = NODE1_ID;
  doc["seq"] = ++seq;
  doc["readings"]["distance_cm"] = serialized(String(d, 1));

  doc["mode"] = autoMode ? "auto" : "manual";

  const char* trigger = "manual";

  if (autoMode) {
    switch (lastAutoColour) {
      case 1: trigger = "proximity"; break;
      case 2: trigger = "light"; break;
      case 3: trigger = "humidity"; break;
      case 4: trigger = "normal"; break;
      case 5: trigger = "sensor_unavailable"; break;
      default: trigger = "unknown"; break;
    }
  }

doc["trigger"] = trigger;

  String body;
  serializeJson(doc, body);

  HTTPClient http;
  http.setConnectTimeout(POST_TIMEOUT_MS);
  http.setTimeout(POST_TIMEOUT_MS);
  http.begin(dataUrl);
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(body);
  Serial.printf("POST %s -> %d\n", body.c_str(), code);
  if (code > 0 && code != 200) Serial.println(http.getString());   // gateway explains rejections
  http.end();
}

// ---------- Downlink ----------
void replyCmd(int httpCode, const char* cmdId, const char* status, const char* error = nullptr) {
  JsonDocument r;
  r["cmd_id"] = cmdId;
  r["status"] = status;
  if (error) r["error"] = error;
  String out;
  serializeJson(r, out);
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(httpCode, "application/json", out);
}

void handleCmd() {
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) {
    replyCmd(400, "", "rejected", "bad JSON");
    return;
  }
  const char* cmdId    = doc["cmd_id"]   | "";
  const char* actuator = doc["actuator"] | "";
  const char* action   = doc["action"]   | "";

  if (!cmdId[0])                      { replyCmd(400, cmdId, "rejected", "cmd_id missing"); return; }
  if (strcmp(actuator, "led") != 0)   { replyCmd(400, cmdId, "rejected", "unknown actuator"); return; }
  if (autoMode) { replyCmd(409, cmdId, "rejected", "RGB controlled by AUTO mode"); return; }

  if (strcmp(action, "off") == 0) {
    strip.clear();
    strip.show();
    Serial.printf("CMD %s: led off\n", cmdId);
  } else if (strcmp(action, "set") == 0) {
    JsonArrayConst v = doc["value"].as<JsonArrayConst>();
    if (v.isNull() || v.size() != 3) { replyCmd(400, cmdId, "rejected", "value must be [r,g,b]"); return; }
    int c[3];
    for (int i = 0; i < 3; i++) {
      if (!v[i].is<int>())            { replyCmd(400, cmdId, "rejected", "value must be integers"); return; }
      c[i] = v[i].as<int>();
      if (c[i] < 0 || c[i] > 255)     { replyCmd(400, cmdId, "rejected", "value out of range (0 to 255)"); return; }
    }
    strip.fill(strip.Color(c[0], c[1], c[2]));
    strip.show();
    Serial.printf("CMD %s: led set %d,%d,%d\n", cmdId, c[0], c[1], c[2]);
  } else {
    replyCmd(400, cmdId, "rejected", "unknown action");
    return;
  }
  replyCmd(200, cmdId, "executed");
}

// ---------- Wi-Fi ----------
void connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);                        // keeps incoming commands responsive
  WiFi.config(NODE1_IP, GW_AP_IP, GW_AP_SUBNET);   // static IP: must come before begin()
  WiFi.begin(GW_AP_SSID, GW_AP_PASSWORD);
}

void setup() {
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(MODE_LED_PIN, OUTPUT);

  digitalWrite(MODE_LED_PIN, LOW);

  Serial.begin(115200);
  delay(500);
  Serial.printf("\n=== %s (config v%d) ===\n", NODE1_ID, CONFIG_VERSION);

  strip.begin();
  strip.setBrightness(50);
  strip.show();

  dataUrl = String("http://") + GW_AP_IP.toString() + EP_DATA;

  WiFi.mode(WIFI_STA);
  int n = WiFi.scanNetworks();   // takes ~2 s, debugging only
  connectWifi();
  uint32_t t = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t < 15000) delay(500);
  Serial.print("Node IP: ");
  Serial.println(WiFi.localIP());

  server.on(EP_CMD, HTTP_POST, handleCmd);
  server.begin();
}

void loop() {
  checkModeButton();
  updateAutoRgb();

  server.handleClient();


  checkModeButton();

  if (autoMode &&
      millis() - lastNode2FetchMs >= NODE2_FETCH_MS) {

    lastNode2FetchMs = millis();
    fetchNode2Readings();
  }

  updateAutoRgb();

  if (WiFi.status() != WL_CONNECTED && millis() - lastWifiTry >= WIFI_RETRY_MS) {
    lastWifiTry = millis();
    Serial.println("Wi-Fi lost, reconnecting...");
    WiFi.disconnect();
    WiFi.begin(GW_AP_SSID, GW_AP_PASSWORD);
  }

  if (millis() - lastSend >= NODE_POST_INTERVAL_MS) {
    lastSend = millis();
    sendReading();
  }
}
