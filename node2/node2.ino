#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include "config.h"

#define DHT_PIN      4
#define LIGHT_PIN    5      
#define DHT_TYPE     DHT22
#define LIGHT_INVERT 1     

constexpr uint32_t POST_TIMEOUT_MS = 3000;
constexpr uint32_t WIFI_RETRY_MS   = 5000;

DHT dht(DHT_PIN, DHT_TYPE);
WebServer server(HTTP_PORT);

String   dataUrl;
uint32_t lastSend = 0, lastWifiTry = 0, seq = 0;

// ---------- Sensors ----------
float readLightPct() {
  long sum = 0;
  for (int i = 0; i < 8; i++) sum += analogRead(LIGHT_PIN);   // average to reduce noise
  float pct = (sum / 8.0f) / 4095.0f * 100.0f;                // 12-bit ADC -> 0-100 %
  if (LIGHT_INVERT) pct = 100.0f - pct;
  return pct;
}

// ---------- Uplink ----------
void sendReading() {
  if (WiFi.status() != WL_CONNECTED) return;

  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (isnan(t) || isnan(h) || t < TEMP_MIN_C || t > TEMP_MAX_C ||
      h < HUMIDITY_MIN_PCT || h > HUMIDITY_MAX_PCT) {
    Serial.println("DHT22 read invalid, not sent");
    return;
  }
  float light = readLightPct();

  JsonDocument doc;
  doc["node_id"] = NODE2_ID;
  doc["seq"] = ++seq;
  JsonObject r = doc["readings"].to<JsonObject>();
  r["temp_c"]       = serialized(String(t, 1));
  r["humidity_pct"] = serialized(String(h, 1));
  r["light_pct"]    = serialized(String(light, 1));
  String body;
  serializeJson(doc, body);

  HTTPClient http;
  http.setConnectTimeout(POST_TIMEOUT_MS);
  http.setTimeout(POST_TIMEOUT_MS);
  http.begin(dataUrl);
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(body);
  Serial.printf("POST %s -> %d\n", body.c_str(), code);
  if (code > 0 && code != 200) Serial.println(http.getString());
  http.end();
}

// ---------- Downlink: this node has no actuator, so every command is rejected ----------
void handleCmd() {
  JsonDocument doc;
  const char* cmdId = "";
  if (!deserializeJson(doc, server.arg("plain"))) cmdId = doc["cmd_id"] | "";
  JsonDocument r;
  r["cmd_id"] = cmdId;
  r["status"] = "rejected";
  r["error"]  = "node2 has no actuator";
  String out;
  serializeJson(r, out);
  server.send(400, "application/json", out);
}

// ---------- Wi-Fi ----------
void connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.config(NODE2_IP, GW_AP_IP, GW_AP_SUBNET);
  WiFi.begin(GW_AP_SSID, GW_AP_PASSWORD);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.printf("\n=== %s (config v%d) ===\n", NODE2_ID, CONFIG_VERSION);

  dht.begin();
  analogReadResolution(12);

  dataUrl = String("http://") + GW_AP_IP.toString() + EP_DATA;

  connectWifi();
  uint32_t t = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t < 15000) delay(500);
  Serial.print("Node IP: ");
  Serial.println(WiFi.localIP());

  server.on(EP_CMD, HTTP_POST, handleCmd);
  server.begin();
}

void loop() {
  server.handleClient();

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
