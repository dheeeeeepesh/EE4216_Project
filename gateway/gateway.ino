#include <WiFi.h>
#include <AsyncTCP.h>            // background networking task the web server runs on
#include <ESPAsyncWebServer.h>   // non-blocking HTTP server
#include <ArduinoJson.h>         // safe JSON parsing and building
#include <PubSubClient.h>        // MQTT client
#include <HTTPClient.h>           // gateway -> node requests (Stage 4)
#include <ctype.h>               // isspace(), tolower()
#include <math.h>                // isfinite()
#include "config.h"
#include "secrets.h"
#include "gateway_types.h"
#include "dashboard_html.h"   // the web page served at "/" (DASHBOARD_HTML)

#define GATEWAY_VERSION 7

// Wi-Fi state (from Stage 1)
volatile bool     staConnected   = false;
volatile uint32_t staLostAtMs    = 0;   // when we last went from online to offline (0 = never)
volatile uint32_t lastStaEventMs = 0;   // when the Wi-Fi driver last reported a failed attempt

constexpr uint32_t STA_RETRY_INTERVAL_MS    = 15000;
constexpr uint32_t STATUS_PRINT_INTERVAL_MS = 5000;

// =====================================================================
// Gateway-only settings for Stage 2
// =====================================================================
constexpr size_t   MAX_BODY_BYTES          = 512;   // our real messages are ~100 bytes; anything
                                                    // far bigger is a bug or abuse, and refusing it
                                                    // protects the gateway's limited memory
constexpr uint32_t LOCK_TIMEOUT_MS         = 50;    // how long a task will wait for the lock
constexpr uint32_t STATE_CHECK_INTERVAL_MS = 1000;  // how often loop() checks for node state changes
constexpr bool     LOG_EVERY_POST          = true;  // handy now; set false later to reduce log noise

// =====================================================================
// What each node is expected to send (ReadingSpec is in gateway_types.h)
// =====================================================================

const ReadingSpec NODE1_SPECS[] = {
  {"distance_cm",  "cm", DISTANCE_MIN_CM,  DISTANCE_MAX_CM},
};
const ReadingSpec NODE2_SPECS[] = {
  {"temp_c",       "°C", TEMP_MIN_C,       TEMP_MAX_C},
  {"humidity_pct", "%",  HUMIDITY_MIN_PCT, HUMIDITY_MAX_PCT},
  {"light_pct",    "%",  LIGHT_MIN_PCT,    LIGHT_MAX_PCT},
};



// =====================================================================
// Node records (NodeHealth and NodeState are in gateway_types.h)
// =====================================================================

const char* healthName(NodeHealth h) {
  switch (h) {
    case NodeHealth::Online:  return "online";
    case NodeHealth::Stale:   return "stale";
    case NodeHealth::Offline: return "offline";
    default:                  return "never_seen";
  }
}

constexpr uint8_t NODE_COUNT = 2;
NodeState nodes[NODE_COUNT] = {
  {NODE1_ID, NODE1_IP, NODE1_SPECS, sizeof(NODE1_SPECS) / sizeof(NODE1_SPECS[0]),
   false, 0, 0, {}, 0, 0, 0, 0},
  {NODE2_ID, NODE2_IP, NODE2_SPECS, sizeof(NODE2_SPECS) / sizeof(NODE2_SPECS[0]),
   false, 0, 0, {}, 0, 0, 0, 0},
};
static_assert(sizeof(NODE1_SPECS) / sizeof(NODE1_SPECS[0]) <= MAX_READINGS, "node1 has too many readings");
static_assert(sizeof(NODE2_SPECS) / sizeof(NODE2_SPECS[0]) <= MAX_READINGS, "node2 has too many readings");

uint32_t unattributedRejects = 0;   // bad posts we couldn't tie to a node (e.g. broken JSON). Lock-protected.

// Only loop() reads or writes this, so it needs no lock. Data used by a
// single task is safe; the lock is only for data shared between tasks.
NodeHealth reportedHealth[NODE_COUNT] = {NodeHealth::NeverSeen, NodeHealth::NeverSeen};

// =====================================================================
// The lock (mutex)
// The web server task writes node records while loop() and /api/status read
// them. The mutex makes sure only one task touches them at a time, so nobody
// ever sees a half-written record. Rule: hold it briefly, never do slow work
// (network calls, long prints) while holding it.
// =====================================================================
SemaphoreHandle_t nodesMutex = nullptr;

bool lockNodes()   { return xSemaphoreTake(nodesMutex, pdMS_TO_TICKS(LOCK_TIMEOUT_MS)) == pdTRUE; }
void unlockNodes() { xSemaphoreGive(nodesMutex); }

// State is calculated from "last heard at" every time it's needed, instead of
// being stored. A stored state could go out of date; a calculated one can't.
NodeHealth computeHealth(const NodeState& n, uint32_t now) {
  if (!n.everSeen) return NodeHealth::NeverSeen;
  int32_t age = (int32_t)(now - n.lastSeenMs);
  if (age < 0) age = 0;   // guard: a post landed just after 'now' was read
  if ((uint32_t)age <= NODE_STALE_MS)   return NodeHealth::Online;
  if ((uint32_t)age <= NODE_OFFLINE_MS) return NodeHealth::Stale;
  return NodeHealth::Offline;
}

int findNode(const char* id) {
  for (uint8_t i = 0; i < NODE_COUNT; i++) {
    if (strcmp(nodes[i].id, id) == 0) return i;
  }
  return -1;
}

// =====================================================================
// MQTT (Stage 3)
//
// MQTT runs on top of an ordinary TCP connection (WiFiClient). PubSubClient
// does the MQTT part: logging in, publishing, and keepalive "pings" so the
// broker knows we're still here. It does nothing by itself in the
// background, so loop() must call mqtt.loop() often.
// =====================================================================

// ThingSpeak field numbers, matching how the data channel was set up on the
// website. If you rename or reorder fields there, change them here too.
const uint8_t NODE_FIRST_FIELD[NODE_COUNT] = {1, 2};   // node1 -> field1,2   node2 -> field2-4
const uint8_t NODE_STATE_FIELD[NODE_COUNT] = {5, 6};   // node1 state, node2 state

constexpr uint32_t MQTT_RETRY_MIN_MS     = 5000;    // first retry after a failure
constexpr uint32_t MQTT_RETRY_MAX_MS     = 30000;   // retries slow down to at most this
constexpr uint16_t MQTT_BUFFER_BYTES     = 512;     // room for Stage 4's incoming commands
constexpr uint16_t MQTT_SOCKET_TIMEOUT_S = 3;       // longest connect() may wait for the broker
constexpr uint8_t  CMD_FIELD           = 1;       // field of the command channel that carries the command

WiFiClient   netClient;          // the raw TCP connection to the broker
PubSubClient mqtt(netClient);    // MQTT on top of that connection

// Written only by loop(), read by /api/status in the web server task.
// Each is one 32-bit value, which the ESP32 reads or writes in a single
// step, so the reader can never see half an update and no lock is needed.
// (The node records need the lock because they are many values that must
// change together.)
volatile bool     mqttUp          = false;
volatile int32_t  mqttLastState   = MQTT_DISCONNECTED;
volatile uint32_t lastPublishOkMs = 0;   // 0 = never published
volatile uint32_t publishesOk     = 0;
volatile uint32_t publishesFailed = 0;
volatile uint32_t mqttConnects    = 0;

// PubSubClient reports problems as numbers. These names make the log and
// the dashboard readable, and the report can quote them directly.
const char* mqttStateName(int s) {
  switch (s) {
    case MQTT_CONNECTION_TIMEOUT:      return "timeout (broker did not answer)";
    case MQTT_CONNECTION_LOST:         return "connection lost";
    case MQTT_CONNECT_FAILED:          return "connect failed (network or DNS)";
    case MQTT_DISCONNECTED:            return "disconnected";
    case MQTT_CONNECTED:               return "connected";
    case MQTT_CONNECT_BAD_PROTOCOL:    return "bad protocol";
    case MQTT_CONNECT_BAD_CLIENT_ID:   return "client ID rejected";
    case MQTT_CONNECT_UNAVAILABLE:     return "broker unavailable";
    case MQTT_CONNECT_BAD_CREDENTIALS: return "bad username or password";
    case MQTT_CONNECT_UNAUTHORIZED:    return "not authorized";
    default:                           return "unknown";
  }
}

// One connection attempt. This is the only place the gateway can pause:
// connect() waits for the broker for up to MQTT_SOCKET_TIMEOUT_S seconds.
// Only loop() is affected; the web server keeps answering in its own task.
// The time taken is printed so you can show the examiner the real pause.
// Stage 4: on success it also subscribes to the command field. ThingSpeak
// drops subscriptions when a client disconnects, so this must be repeated
// after every reconnect, which is why it lives here.
bool mqttTryConnect() {
  Serial.printf("[MQTT] Connecting to %s:%d ...\n", MQTT_HOST, MQTT_PORT);
  const uint32_t t0 = millis();
  const bool ok = mqtt.connect(MQTT_CLIENT_ID, MQTT_USERNAME, MQTT_PASSWORD);
  const uint32_t took = millis() - t0;
  mqttLastState = mqtt.state();
  if (!ok) {
    Serial.printf("[MQTT] Failed after %lu ms: %s (state %d)\n",
                  (unsigned long)took, mqttStateName(mqttLastState), (int)mqttLastState);
    return false;
  }

  char sub[64];
  snprintf(sub, sizeof(sub), "channels/%lu/subscribe/fields/field%u",
           (unsigned long)TS_CMD_CHANNEL_ID, (unsigned)CMD_FIELD);
  if (!mqtt.subscribe(sub)) {
    // Connected but not listening for commands is a half-working state, so
    // treat it as a failed attempt and let the normal backoff retry.
    Serial.printf("[MQTT] Subscribe to %s FAILED, dropping the connection to retry\n", sub);
    mqtt.disconnect();
    mqttLastState = MQTT_CONNECTION_LOST;
    return false;
  }

  mqttUp = true;
  mqttConnects = mqttConnects + 1;
  Serial.printf("[MQTT] Connected in %lu ms, listening on %s\n", (unsigned long)took, sub);
  return true;
}

// Build and send one combined message for both nodes.
// ThingSpeak's payload looks like a web form, not JSON:
//   field1=28.4&field2=71.2&field3=35.2&field4=64.0&field5=2&field6=2
void mqttPublishReadings() {
  float      vals[NODE_COUNT][MAX_READINGS];
  uint8_t    counts[NODE_COUNT];
  NodeHealth health[NODE_COUNT];

  // Same pattern as /api/status: lock, copy, unlock, then do the slow work.
  if (!lockNodes()) { Serial.println("[MQTT] Publish skipped: node data busy"); return; }
  const uint32_t t = millis();
  for (uint8_t i = 0; i < NODE_COUNT; i++) {
    health[i] = computeHealth(nodes[i], t);
    counts[i] = nodes[i].specCount;
    for (uint8_t k = 0; k < counts[i]; k++) vals[i][k] = nodes[i].values[k];
  }
  unlockNodes();

  String payload;
  for (uint8_t i = 0; i < NODE_COUNT; i++) {
    // Readings are only sent for nodes we heard from recently. An offline
    // node's old values are left out on purpose: resending them would make
    // the cloud chart look like the node was still working. The gap in the
    // chart is the honest record, and the state field explains it.
    const bool fresh = (health[i] == NodeHealth::Online || health[i] == NodeHealth::Stale);
    if (fresh) {
      for (uint8_t k = 0; k < counts[i]; k++) {
        payload += String("field") + (NODE_FIRST_FIELD[i] + k) + "=" + String(vals[i][k], 1) + "&";
      }
    }
    // State as a number, because ThingSpeak can only chart numbers.
    const int stateNum = (health[i] == NodeHealth::Online) ? 2 : (health[i] == NodeHealth::Stale) ? 1 : 0;
    payload += String("field") + NODE_STATE_FIELD[i] + "=" + stateNum + "&";
  }
  payload.remove(payload.length() - 1);   // drop the final '&'

  const String topic = String("channels/") + TS_DATA_CHANNEL_ID + "/publish";

  // publish() returning true means the message was handed to the network,
  // not that ThingSpeak stored it. ThingSpeak only offers QoS 0 ("send and
  // forget"), so the ThingSpeak chart is the real proof of delivery.
  if (mqtt.publish(topic.c_str(), payload.c_str())) {
    publishesOk = publishesOk + 1;
    lastPublishOkMs = millis();
    Serial.printf("[MQTT] Published: %s\n", payload.c_str());
  } else {
    publishesFailed = publishesFailed + 1;
    Serial.printf("[MQTT] Publish FAILED (state %d)\n", mqtt.state());
  }
}

// =====================================================================
// Stage 4: remote commands (ThingSpeak -> MQTT -> gateway -> HTTP -> node)
//
// One command's journey:
//   1. A remote user writes the command channel (field 1) on ThingSpeak.
//   2. ThingSpeak delivers it to our MQTT subscription, and PubSubClient
//      calls onMqttMessage() from inside mqtt.loop().
//   3. The gateway parses and validates it, and finds the node that owns
//      the actuator. A bad command is answered "rejected" right here and
//      never reaches a node.
//   4. processPendingCommand() forwards it to the node over HTTP and waits
//      for the node's reply (at most CMD_HTTP_TIMEOUT_MS).
//   5. The result is kept as last_cmd for the dashboard and queued as an
//      acknowledgment for the ack channel.
//
// Receipt and execution are kept apart on purpose:
//   rejected = the gateway received it but it was invalid (nothing was sent)
//   timeout  = forwarded, but the node never answered (execution unknown)
//   failed   = the node answered and said it could not do it
//   executed = the node ran it and confirmed
//
// Accepted command formats (in field 1 of the command channel):
//   text:  led:set:255,0,0       led:off
//   JSON:  {"cmd_id":"c17","actuator":"led","action":"set","value":[255,0,0]}
// =====================================================================

constexpr uint8_t  ACK_QUEUE_LEN       = 4;       // results waiting to be published
constexpr uint32_t ACK_MIN_INTERVAL_MS = 16000;   // ThingSpeak free tier allows ~1 update / 15 s per channel

// actuator name in a command -> index of the node that owns it (0 = node1)
const ActuatorRoute ROUTES[] = {
  {"led", 0},
};
constexpr uint8_t ROUTE_COUNT = sizeof(ROUTES) / sizeof(ROUTES[0]);

PendingCmd pendingCmd = {};              // touched only by loop() (the MQTT callback runs inside it)
uint32_t   cmdCounter = 0;               // numbers commands that arrive without their own cmd_id
CmdResult  lastCmd    = {};              // shared with /api/status, so only touched while holding nodesMutex
CmdResult  ackQueue[ACK_QUEUE_LEN];      // touched only by loop()
uint8_t    ackHead = 0, ackCount = 0;
uint32_t   lastAckPublishMs = 0;         // 0 = no ack published yet

// ---- Parsing ----

// Keeps a sender-chosen cmd_id short and safe to put in a ThingSpeak payload.
void copyCmdId(PendingCmd& c, const char* id) {
  size_t i = 0;
  for (; id[i] && i < sizeof(c.id) - 1; i++) {
    const char ch = id[i];
    const bool ok = (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') ||
                    (ch >= 'A' && ch <= 'Z') || ch == '_' || ch == '-';
    c.id[i] = ok ? ch : '_';
  }
  c.id[i] = '\0';
}

// Text form  actuator:action[:r,g,b]   e.g. "led:set:255,0,0" or "led:off".
// Returns nullptr when it parsed, otherwise a short reason.
const char* parseTextCommand(const char* text, PendingCmd& c) {
  while (*text && isspace((unsigned char)*text)) text++;
  size_t n = strlen(text);
  while (n > 0 && isspace((unsigned char)text[n - 1])) n--;
  if (n == 0) return "empty command";

  char buf[48];
  if (n >= sizeof(buf)) return "command too long";
  for (size_t i = 0; i < n; i++) buf[i] = (char)tolower((unsigned char)text[i]);
  buf[n] = '\0';

  char* actuator = buf;
  char* action   = strchr(buf, ':');
  if (!action) return "format is actuator:action[:r,g,b]";
  *action++ = '\0';
  char* args = strchr(action, ':');
  if (args) *args++ = '\0';

  if (!actuator[0] || strlen(actuator) >= sizeof(c.actuator)) return "bad actuator";
  if (!action[0]   || strlen(action)   >= sizeof(c.action))   return "bad action";
  snprintf(c.actuator, sizeof(c.actuator), "%s", actuator);
  snprintf(c.action,   sizeof(c.action),   "%s", action);

  c.hasRgb = false;
  if (args) {
    int  v[3];
    char extra;   // a fourth match means there was trailing junk after b
    if (sscanf(args, "%d,%d,%d%c", &v[0], &v[1], &v[2], &extra) != 3) return "value must be r,g,b";
    for (int i = 0; i < 3; i++) c.rgb[i] = v[i];
    c.hasRgb = true;
  }
  return nullptr;
}

// JSON form, the same shape the gateway sends to nodes (see config.h contract).
// cmd_id is optional here; the gateway makes one if it is missing.
const char* parseJsonCommand(const char* text, PendingCmd& c) {
  JsonDocument doc;
  if (deserializeJson(doc, text)) return "bad JSON";

  const char* cmdId    = doc["cmd_id"]   | "";
  const char* actuator = doc["actuator"] | "";
  const char* action   = doc["action"]   | "";
  if (cmdId[0]) copyCmdId(c, cmdId);

  if (!actuator[0] || strlen(actuator) >= sizeof(c.actuator)) return "bad actuator";
  if (!action[0]   || strlen(action)   >= sizeof(c.action))   return "bad action";
  snprintf(c.actuator, sizeof(c.actuator), "%s", actuator);
  snprintf(c.action,   sizeof(c.action),   "%s", action);

  c.hasRgb = false;
  JsonArrayConst v = doc["value"].as<JsonArrayConst>();
  if (!v.isNull()) {
    if (v.size() != 3) return "value must be [r,g,b]";
    for (int i = 0; i < 3; i++) {
      if (!v[i].is<int>()) return "value must be integers";
      c.rgb[i] = v[i].as<int>();
    }
    c.hasRgb = true;
  }
  return nullptr;
}

// Checks the command makes sense and finds the owning node.
// Returns nullptr when valid (nodeIdx is then set), otherwise a short reason.
const char* validateCommand(const PendingCmd& c, int& nodeIdx) {
  nodeIdx = -1;
  for (uint8_t i = 0; i < ROUTE_COUNT; i++) {
    if (strcmp(ROUTES[i].actuator, c.actuator) == 0) nodeIdx = ROUTES[i].nodeIdx;
  }
  if (nodeIdx < 0) return "unknown actuator";

  if (strcmp(c.actuator, "led") == 0) {
    if (strcmp(c.action, "set") == 0) {
      if (!c.hasRgb) return "set needs r,g,b";
      for (int i = 0; i < 3; i++) {
        if (c.rgb[i] < 0 || c.rgb[i] > 255) return "value out of range (0 to 255)";
      }
    } else if (strcmp(c.action, "off") != 0) {
      return "unknown action";
    }
  }
  return nullptr;
}

// "led:set:255,0,0" or "led:off", used in logs and acknowledgments.
void describeCommand(const PendingCmd& c, char* out, size_t n) {
  if (c.hasRgb) snprintf(out, n, "%s:%s:%d,%d,%d", c.actuator, c.action, c.rgb[0], c.rgb[1], c.rgb[2]);
  else          snprintf(out, n, "%s:%s", c.actuator, c.action);
}

// ---- Results and acknowledgments ----

// ThingSpeak payloads are form-encoded (field1=a&field2=b), so anything that
// could break that (spaces, &, =, %, +) is swapped for '_'.
void appendClean(String& out, const char* s) {
  for (; *s; s++) {
    const char ch = *s;
    const bool ok = (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                    ch == '.' || ch == ',' || ch == ':' || ch == '-' || ch == '_';
    out += ok ? ch : '_';
  }
}

void queueAck(const CmdResult& r) {
  if (ackCount == ACK_QUEUE_LEN) {   // full: the oldest result is dropped to make room
    ackHead = (ackHead + 1) % ACK_QUEUE_LEN;
    ackCount--;
    Serial.println("[ACK] Queue full, oldest acknowledgment dropped");
  }
  ackQueue[(ackHead + ackCount) % ACK_QUEUE_LEN] = r;
  ackCount++;
}

// Records the final outcome of a command: last_cmd for the dashboard, an
// acknowledgment for ThingSpeak, and a log line.
void finishCommand(const PendingCmd& c, int nodeIdx, const char* status, const char* detail) {
  CmdResult r = {};
  r.valid = true;
  snprintf(r.id,     sizeof(r.id),     "%s", c.id);
  snprintf(r.target, sizeof(r.target), "%s", nodeIdx >= 0 ? nodes[nodeIdx].id : "-");
  snprintf(r.status, sizeof(r.status), "%s", status);
  snprintf(r.detail, sizeof(r.detail), "%s", detail);
  describeCommand(c, r.text, sizeof(r.text));
  r.doneMs = millis();
  r.rttMs  = r.doneMs - c.rxMs;

  if (lockNodes()) { lastCmd = r; unlockNodes(); }
  else Serial.println("[CMD] last_cmd not updated: node data busy");

  queueAck(r);
  Serial.printf("[CMD] %s -> %s: %s (%s) after %lu ms\n", r.id, r.target, r.status, r.detail,
                (unsigned long)r.rttMs);
}

// Sends the oldest waiting acknowledgment to the ack channel, at most one per
// ACK_MIN_INTERVAL_MS because ThingSpeak ignores faster updates on one channel.
// Payload: field1=cmd_id field2=status field3=detail field4=command field5=rtt_ms
void publishPendingAck() {
  if (!mqttUp || ackCount == 0) return;
  const uint32_t now = millis();
  if (lastAckPublishMs != 0 && now - lastAckPublishMs < ACK_MIN_INTERVAL_MS) return;
  lastAckPublishMs = now;   // set before publishing, so a failed publish is retried after the interval

  const CmdResult& r = ackQueue[ackHead];
  String payload = "field1=";
  appendClean(payload, r.id);
  payload += "&field2=";  appendClean(payload, r.status);
  payload += "&field3=";  appendClean(payload, r.detail);
  payload += "&field4=";  appendClean(payload, r.text);
  payload += "&field5=";  payload += String((unsigned long)r.rttMs);

  const String topic = String("channels/") + TS_ACK_CHANNEL_ID + "/publish";
  if (mqtt.publish(topic.c_str(), payload.c_str())) {
    Serial.printf("[ACK] Published: %s\n", payload.c_str());
    ackHead = (ackHead + 1) % ACK_QUEUE_LEN;
    ackCount--;
  } else {
    Serial.printf("[ACK] Publish FAILED (state %d), will retry\n", mqtt.state());
  }
}

// ---- Receiving and forwarding ----

void handleIncomingCommand(const char* msg, bool tooLong) {
  PendingCmd c = {};
  snprintf(c.id, sizeof(c.id), "m%lu", (unsigned long)++cmdCounter);
  c.rxMs = millis();

  const char* p = msg;
  while (*p && isspace((unsigned char)*p)) p++;
  const char* err = tooLong ? "message too long"
                  : (*p == '{') ? parseJsonCommand(p, c)
                                : parseTextCommand(p, c);
  int nodeIdx = -1;
  if (!err) err = validateCommand(c, nodeIdx);
  if (err) {
    finishCommand(c, nodeIdx, "rejected", err);   // received, but invalid: nothing goes to a node
    return;
  }
  if (pendingCmd.used) {
    finishCommand(c, nodeIdx, "rejected", "gateway busy");
    return;
  }
  c.used = true;
  pendingCmd = c;
  Serial.printf("[CMD] %s received: %s\n", c.id, msg);
}

// Called by PubSubClient (from inside mqtt.loop()) for every message on the
// command topic. Kept short: it only parks the command for loop() to forward.
void onMqttMessage(char* topic, uint8_t* payload, unsigned int length) {
  char msg[200];
  const bool tooLong = length >= sizeof(msg);
  const unsigned int n = tooLong ? sizeof(msg) - 1 : length;
  memcpy(msg, payload, n);
  msg[n] = '\0';   // PubSubClient does not add the end-of-string marker
  handleIncomingCommand(msg, tooLong);
}

// Forwards the waiting command to its node and records what came back.
// This is the one place besides MQTT connect where loop() can pause (up to
// CMD_HTTP_TIMEOUT_MS). Only loop() is affected; the web server keeps answering.
void processPendingCommand() {
  if (!pendingCmd.used) return;
  PendingCmd c = pendingCmd;
  pendingCmd.used = false;

  int nodeIdx = -1;
  const char* err = validateCommand(c, nodeIdx);   // checked on arrival; this finds the node again
  if (err) { finishCommand(c, nodeIdx, "rejected", err); return; }
  const NodeState& meta = nodes[nodeIdx];          // id and expectedIp never change, so no lock needed

  JsonDocument body;
  body["cmd_id"]   = c.id;
  body["actuator"] = c.actuator;
  body["action"]   = c.action;
  if (c.hasRgb && strcmp(c.action, "set") == 0) {
    JsonArray v = body["value"].to<JsonArray>();
    for (int i = 0; i < 3; i++) v.add(c.rgb[i]);
  }
  String bodyStr;
  serializeJson(body, bodyStr);

  const String url = String("http://") + meta.expectedIp.toString() + EP_CMD;
  Serial.printf("[CMD] %s forwarding to %s (%s): %s\n", c.id, meta.id, url.c_str(), bodyStr.c_str());

  HTTPClient http;
  http.setConnectTimeout(CMD_HTTP_TIMEOUT_MS);
  http.setTimeout(CMD_HTTP_TIMEOUT_MS);
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  const int code = http.POST(bodyStr);
  const String resp = (code > 0) ? http.getString() : String();
  http.end();

  const char* status = "failed";
  char detail[CMD_DETAIL_LEN] = "";
  if (code <= 0) {
    // No answer from the node: the connection failed or the reply never came.
    status = "timeout";
    snprintf(detail, sizeof(detail), "%s", HTTPClient::errorToString(code).c_str());
  } else {
    JsonDocument ack;
    if (deserializeJson(ack, resp)) {
      snprintf(detail, sizeof(detail), "bad ack from node (HTTP %d)", code);
    } else {
      const char* ackId = ack["cmd_id"] | "";
      const char* st    = ack["status"] | "";
      const char* er    = ack["error"]  | "";
      if (strcmp(ackId, c.id) != 0) {
        snprintf(detail, sizeof(detail), "ack for a different cmd_id");
      } else if (strcmp(st, "executed") == 0) {
        status = "executed";
        snprintf(detail, sizeof(detail), "ok");
      } else if (strcmp(st, "failed") == 0 || strcmp(st, "rejected") == 0) {
        status = (st[0] == 'f') ? "failed" : "rejected";   // literals only: 'ack' is freed at the end of this block
        snprintf(detail, sizeof(detail), "%s", er[0] ? er : "no reason given");
      } else {
        snprintf(detail, sizeof(detail), "unknown ack status");
      }
    }
  }
  finishCommand(c, nodeIdx, status, detail);
}

// =====================================================================
// HTTP helpers
// =====================================================================
AsyncWebServer server(HTTP_PORT);

void sendJson(AsyncWebServerRequest* req, int code, JsonDocument& doc) {
  String out;
  serializeJson(doc, out);
  req->send(code, "application/json", out);
}

// One place for every rejection, so counting, logging and the reply format
// are always consistent.
void rejectPost(AsyncWebServerRequest* req, int code, int nodeIdx, const String& reason) {
  if (lockNodes()) {
    if (nodeIdx >= 0) nodes[nodeIdx].postsRejected++;
    else              unattributedRejects++;
    unlockNodes();
  }
  Serial.printf("[HTTP] REJECTED %s from %s (%d): %s\n", EP_DATA,
                req->client()->remoteIP().toString().c_str(), code, reason.c_str());
  JsonDocument doc;
  doc["ok"]    = false;
  doc["error"] = reason;
  sendJson(req, code, doc);
}

// =====================================================================
// POST /api/data , part 1: collect the body
// The async server hands us the body in pieces as it arrives over the
// network. We copy the pieces into one buffer stored on the request itself
// (_tempObject). The library frees that buffer automatically when the
// request finishes, so there is no memory leak to worry about.
// =====================================================================
void onDataBody(AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
  if (total > MAX_BODY_BYTES) return;               // refuse to store it; part 2 replies 413
  if (index == 0) req->_tempObject = malloc(total + 1);  // +1 for the end-of-string marker
  if (!req->_tempObject) return;                    // out of memory; part 2 replies 400
  memcpy((uint8_t*)req->_tempObject + index, data, len);
  if (index + len == total) ((char*)req->_tempObject)[total] = '\0';
}

// =====================================================================
// POST /api/data , part 2: validate and store
// Runs once the whole body has arrived. Checks go from cheapest to most
// specific, and the first failure stops everything with a clear reason.
// All validation happens BEFORE taking the lock, so the lock is held only
// for the few lines that actually change the record.
// =====================================================================
void onDataRequest(AsyncWebServerRequest* req) {
  if (req->contentLength() > MAX_BODY_BYTES) { rejectPost(req, 413, -1, "body too large"); return; }

  const char* body = (const char*)req->_tempObject;
  if (!body) { rejectPost(req, 400, -1, "empty body"); return; }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) { rejectPost(req, 400, -1, String("bad JSON: ") + err.c_str()); return; }

  const char* nodeId = doc["node_id"].as<const char*>();   // nullptr if missing or not text
  if (!nodeId) { rejectPost(req, 400, -1, "node_id missing or not a string"); return; }

  int idx = findNode(nodeId);
  if (idx < 0) { rejectPost(req, 400, -1, String("unknown node_id: ") + nodeId); return; }

  // Validate optional RGB automation status from Node 1.
  // Older Node 1 firmware can still send sensor readings.
  const char* mode = "";
  const char* trigger = "";

  if (idx == 0) {
    mode = doc["mode"] | "";
    trigger = doc["trigger"] | "";

    if (mode[0] || trigger[0]) {
      bool validMode =
        strcmp(mode, "auto") == 0 ||
        strcmp(mode, "manual") == 0;

      bool validTrigger =
        strcmp(trigger, "proximity") == 0 ||
        strcmp(trigger, "light") == 0 ||
        strcmp(trigger, "humidity") == 0 ||
        strcmp(trigger, "normal") == 0 ||
        strcmp(trigger, "sensor_unavailable") == 0 ||
        strcmp(trigger, "manual") == 0 ||
        strcmp(trigger, "unknown") == 0;

      if (!validMode || !validTrigger) {
        rejectPost(req, 400, idx,
                   "invalid automation status");
        return;
      }
    }
  }


  if (!doc["seq"].is<uint32_t>()) {   // false for missing, negative, decimal or text values
    rejectPost(req, 400, idx, "seq missing or not a non-negative integer"); return;
  }
  uint32_t seq = doc["seq"].as<uint32_t>();

  JsonObjectConst readings = doc["readings"].as<JsonObjectConst>();
  if (readings.isNull()) { rejectPost(req, 400, idx, "readings missing or not an object"); return; }

  // Check every reading this node must send. specs/specCount never change,
  // so reading them here without the lock is safe.
  float vals[MAX_READINGS];
  const NodeState& meta = nodes[idx];
  for (uint8_t i = 0; i < meta.specCount; i++) {
    const ReadingSpec& s = meta.specs[i];
    JsonVariantConst v = readings[s.key];
    // is<float>() is false for missing keys, null (a failed DHT22 read) and
    // text like "28.4", so all three are caught by this one check.
    if (!v.is<float>()) { rejectPost(req, 400, idx, String(s.key) + " missing or not a number"); return; }
    float f = v.as<float>();
    if (!isfinite(f) || f < s.minV || f > s.maxV) {
      // (long)lroundf(...) prints whole numbers. String(x, 0) pads small
      // numbers with a leading space, which gave "( 2 to 400)" in v4.
      rejectPost(req, 400, idx, String(s.key) + " out of range (" + String((long)lroundf(s.minV)) +
                                " to " + String((long)lroundf(s.maxV)) + ")");
      return;
    }
    vals[i] = f;
  }

  // Logged, not enforced: the Python mock posts from the laptop's address,
  // and blocking it would make solo testing impossible.
  IPAddress from = req->client()->remoteIP();
  if (from != meta.expectedIp) {
    Serial.printf("[HTTP] Warning: %s posted from %s (expected %s)\n",
                  nodeId, from.toString().c_str(), meta.expectedIp.toString().c_str());
  }

  // ---- Commit under the lock ----
  if (!lockNodes()) {
    JsonDocument busy;
    busy["ok"] = false;
    busy["error"] = "gateway busy, retry";
    sendJson(req, 503, busy);
    return;
  }
  NodeState& n = nodes[idx];
  uint32_t gap = 0;
  bool rebooted = false;
  if (n.everSeen) {
    if (seq < n.lastSeq) {                       // counter went backwards: the node restarted
      rebooted = true;
      n.reboots++;
    } else if (seq > n.lastSeq + 1) {            // numbers skipped: posts were lost
      gap = seq - n.lastSeq - 1;
      n.seqGaps += gap;
    }
  }
  n.everSeen   = true;
  n.lastSeenMs = millis();
  n.lastSeq    = seq;
  for (uint8_t i = 0; i < n.specCount; i++) n.values[i] = vals[i];
  
  n.postsOk++;

  // Save Node 1 automation status while holding the mutex.
  if (idx == 0) {
    snprintf(n.mode, sizeof(n.mode), "%s", mode);
    snprintf(n.trigger, sizeof(n.trigger), "%s", trigger);
  }

  unlockNodes();

  // ---- Lock released; printing happens after, because it is slow ----

  if (rebooted) Serial.printf("[NODE] %s restarted (seq went back to %lu)\n", nodeId, (unsigned long)seq);
  if (gap)      Serial.printf("[NODE] %s: %lu post(s) lost before seq %lu\n", nodeId, (unsigned long)gap, (unsigned long)seq);
  if (LOG_EVERY_POST) {
    Serial.printf("[HTTP] OK %s seq=%lu", nodeId, (unsigned long)seq);
    for (uint8_t i = 0; i < meta.specCount; i++) Serial.printf(" %s=%.1f", meta.specs[i].key, vals[i]);
    Serial.println();
  }

  JsonDocument ok;
  ok["ok"] = true;
  sendJson(req, 200, ok);
}

// =====================================================================
// GET /api/status
// Pattern: take the lock, COPY the records, release the lock, then build
// the JSON from the copy. Building JSON is relatively slow, and doing it on
// a copy means the lock is held for microseconds instead of milliseconds.
// =====================================================================
void onStatusRequest(AsyncWebServerRequest* req) {
  NodeState snap[NODE_COUNT];
  uint32_t unattributed;
  CmdResult lc;
  if (!lockNodes()) { req->send(503, "application/json", "{\"error\":\"gateway busy\"}"); return; }
  for (uint8_t i = 0; i < NODE_COUNT; i++) snap[i] = nodes[i];
  unattributed = unattributedRejects;
  lc = lastCmd;
  unlockNodes();

  const uint32_t now = millis();   // read after the copy, so it is never older than lastSeenMs
  JsonDocument doc;
  doc["uptime_s"] = now / 1000;

  JsonObject cloud = doc["cloud"].to<JsonObject>();
  cloud["wifi"] = staConnected ? "online" : "offline";
  // Three cases, so the dashboard can tell "cloud broken" apart from
  // "no Internet at all".
  cloud["mqtt"] = mqttUp ? "connected" : (staConnected ? "disconnected" : "waiting_for_wifi");
  cloud["mqtt_detail"] = mqttStateName(mqttLastState);
  const uint32_t lp = lastPublishOkMs;
  if (lp) {
    int32_t a = (int32_t)(now - lp);
    cloud["last_publish_age_s"] = (a < 0 ? 0 : a) / 1000;
  } else {
    cloud["last_publish_age_s"] = nullptr;
  }
  // The (uint32_t) casts copy each volatile value into a plain number first.
  // ArduinoJson doesn't know how to store a "volatile" type directly.
  cloud["publishes_ok"]     = (uint32_t)publishesOk;
  cloud["publishes_failed"] = (uint32_t)publishesFailed;
  cloud["mqtt_connects"]    = (uint32_t)mqttConnects;

  JsonObject jnodes = doc["nodes"].to<JsonObject>();
  for (uint8_t i = 0; i < NODE_COUNT; i++) {
    const NodeState& s = snap[i];
    JsonObject o = jnodes[s.id].to<JsonObject>();
    o["state"] = healthName(computeHealth(s, now));
    if (s.everSeen) {
      o["age_s"] = (now - s.lastSeenMs) / 1000;
      o["seq"]   = s.lastSeq;
    } else {
      o["age_s"] = nullptr;
      o["seq"]   = nullptr;
    }
    JsonObject r = o["readings"].to<JsonObject>();
    JsonObject u = o["units"].to<JsonObject>();
    for (uint8_t k = 0; k < s.specCount; k++) {
      u[s.specs[k].key] = s.specs[k].unit;
      // serialized() inserts "28.4" as-is. Without it, the float would print
      // as something like 28.39999962 because of how decimals are stored.
      if (s.everSeen) r[s.specs[k].key] = serialized(String(s.values[k], 1));
    }
    o["posts_ok"]       = s.postsOk;
    o["posts_rejected"] = s.postsRejected;
    o["seq_gaps"]       = s.seqGaps;
    o["reboots"]        = s.reboots;

    // Expose Node 1 automation status to the dashboard.
    if (i == 0 && s.everSeen && s.mode[0]) {
      o["mode"] = s.mode;
      o["trigger"] = s.trigger;
    }

  }
  doc["unattributed_rejects"] = unattributed;
  if (lc.valid) {
    // 'lc' is a local copy, so it stays valid until sendJson() below has serialized it.
    JsonObject lj = doc["last_cmd"].to<JsonObject>();
    lj["cmd_id"] = lc.id;
    lj["target"] = lc.target;
    lj["status"] = lc.status;
    lj["detail"] = lc.detail;
    lj["text"]   = lc.text;
    lj["rtt_ms"] = lc.rttMs;
    const int32_t lcAge = (int32_t)(now - lc.doneMs);   // signed: a command may finish just after 'now' was read
    lj["age_s"]  = (lcAge < 0 ? 0 : lcAge) / 1000;
  } else {
    doc["last_cmd"] = nullptr;   // no command received since boot
  }

  sendJson(req, 200, doc);
}

// =====================================================================
// Wi-Fi event handler (from Stage 1)
// =====================================================================
void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      staConnected = true;
      Serial.printf("[STA] Online. IP=%s  channel=%d\n",
                    WiFi.localIP().toString().c_str(), WiFi.channel());
      // Recovery time is useful evidence for the reliability section.
      if (staLostAtMs) Serial.printf("[STA] Back online after %lus offline\n",
                                     (unsigned long)((millis() - staLostAtMs) / 1000));
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      if (staConnected) staLostAtMs = millis();
      staConnected   = false;
      lastStaEventMs = millis();   // the driver is still actively trying
      Serial.printf("[STA] Offline (reason %d)\n", info.wifi_sta_disconnected.reason);
      break;
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED: {
      const uint8_t* m = info.wifi_ap_staconnected.mac;
      Serial.printf("[AP] Device joined  MAC=%02X:%02X:%02X:%02X:%02X:%02X\n",
                    m[0], m[1], m[2], m[3], m[4], m[5]);
      break;
    }
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED: {
      const uint8_t* m = info.wifi_ap_stadisconnected.mac;
      Serial.printf("[AP] Device left    MAC=%02X:%02X:%02X:%02X:%02X:%02X\n",
                    m[0], m[1], m[2], m[3], m[4], m[5]);
      break;
    }
    case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED:
      Serial.printf("[AP] DHCP gave out %s\n",
                    IPAddress(info.wifi_ap_staipassigned.ip.addr).toString().c_str());
      break;
    default:
      break;
  }
}

// =====================================================================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.printf("\n=== EE4216 Gateway v%d (config v%d) ===\n", GATEWAY_VERSION, CONFIG_VERSION);

  // The lock must exist before the server starts, because the first
  // request could arrive the moment server.begin() returns.
  nodesMutex = xSemaphoreCreateMutex();

  WiFi.onEvent(onWiFiEvent);
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);

  if (!WiFi.softAPConfig(GW_AP_IP, GW_AP_IP, GW_AP_SUBNET)) Serial.println("[AP] softAPConfig FAILED");
  if (WiFi.softAP(GW_AP_SSID, GW_AP_PASSWORD, GW_AP_CHANNEL, 0, GW_AP_MAX_CLIENTS)) {
    Serial.printf("[AP] Up. SSID=%s  IP=%s\n", GW_AP_SSID, WiFi.softAPIP().toString().c_str());
  } else {
    Serial.println("[AP] softAP FAILED");
  }

  WiFi.setAutoReconnect(true);
  WiFi.begin(HOTSPOT_SSID, HOTSPOT_PASSWORD);
  lastStaEventMs = millis();   // counts as activity, so no backup nudge for the first 15 s
  Serial.println("[STA] Connecting to hotspot in the background...");

  // MQTT settings only. The actual connection is made later from loop(),
  // once the hotspot is up.
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setBufferSize(MQTT_BUFFER_BYTES);
  mqtt.setSocketTimeout(MQTT_SOCKET_TIMEOUT_S);
  mqtt.setCallback(onMqttMessage);   // Stage 4: incoming commands arrive here

  // Lets a dashboard opened from another address (e.g. a file on the
  // teammate's laptop) read our API. Without it the browser blocks the
  // request. Acceptable here because this network is private.
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");

  // Arguments: path, method, "request complete" handler, file-upload handler
  // (unused), body handler. The body handler runs first as data arrives.
  server.on(EP_DATA, HTTP_POST, onDataRequest, nullptr, onDataBody);
  server.on(EP_STATUS, HTTP_GET, onStatusRequest);
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* r) {
    r->send(200, "text/html", DASHBOARD_HTML);
  });
  server.onNotFound([](AsyncWebServerRequest* r) {
    // Browsers sometimes send an OPTIONS "may I?" request before the real
    // one. Answering it politely avoids confusing errors in the browser.
    if (r->method() == HTTP_OPTIONS) { r->send(204); return; }
    JsonDocument d;
    d["ok"] = false;
    d["error"] = "not found";
    sendJson(r, 404, d);
  });
  server.begin();
  Serial.printf("[HTTP] Server listening on http://%s:%d\n", GW_AP_IP.toString().c_str(), HTTP_PORT);
}

// =====================================================================
void loop() {
  const uint32_t now = millis();
  static uint32_t lastRetryMs      = 0;
  static uint32_t lastPrintMs      = 0;
  static uint32_t lastStateCheckMs = 0;

  // ---- Backup hotspot reconnect ----
  // Every failed attempt by the Wi-Fi driver produces a "disconnected"
  // event, which updates lastStaEventMs. So if no event has arrived for a
  // full interval, the driver has gone quiet (given up), and only then do we
  // nudge it. Nudging while it is still trying is what caused
  // "sta is connecting, return error" in earlier versions.
  // millis() is read fresh here because the event handler may have updated
  // lastStaEventMs after 'now' was taken; the signed cast guards that case.
  if (!staConnected &&
      (int32_t)(millis() - lastStaEventMs) >= (int32_t)STA_RETRY_INTERVAL_MS &&
      now - lastRetryMs >= STA_RETRY_INTERVAL_MS) {
    lastRetryMs = now;
    Serial.println("[STA] Driver has gone quiet, forcing a reconnect attempt");
    WiFi.reconnect();
  }

  // ---- MQTT connection management ----
  static bool     staWasConnected      = false;
  static uint32_t lastMqttAttemptMs    = 0;
  static uint32_t mqttRetryMs          = MQTT_RETRY_MIN_MS;
  static uint32_t lastPublishAttemptMs = 0;
  static bool     warnedNotConfigured  = false;

  const bool staNow = staConnected;
  if (staNow && !staWasConnected) {
    // The hotspot just came back: retry the broker straight away at the
    // fastest rate, instead of waiting out an old, long backoff.
    mqttRetryMs       = MQTT_RETRY_MIN_MS;
    lastMqttAttemptMs = now - MQTT_RETRY_MIN_MS;
  }
  staWasConnected = staNow;

  if (TS_DATA_CHANNEL_ID == 0) {
    // secrets.h still has placeholder values, so don't even try.
    if (!warnedNotConfigured) {
      Serial.println("[MQTT] Not configured: fill in the ThingSpeak values in secrets.h");
      warnedNotConfigured = true;
    }
  } else if (!staNow) {
    // No Internet, so no point trying the broker. Close the old connection
    // so the next attempt starts clean once the hotspot returns.
    if (mqttUp || mqtt.connected()) {
      mqtt.disconnect();
      mqttUp = false;
      mqttLastState = MQTT_DISCONNECTED;
      Serial.println("[MQTT] Down because the hotspot is offline; will reconnect when it returns");
    }
  } else if (mqtt.connected()) {
    mqtt.loop();   // keepalive pings, and (from Stage 4) incoming commands
  } else {
    if (mqttUp) {
      // We were connected, and now we're not: the broker dropped us.
      mqttUp = false;
      mqttLastState = mqtt.state();
      Serial.printf("[MQTT] Lost connection: %s\n", mqttStateName(mqttLastState));
      mqttRetryMs = MQTT_RETRY_MIN_MS;
      lastMqttAttemptMs = now;
    }
    // Backoff: each failure doubles the wait (5, 10, 20, then 30 s max).
    // A broker that is down doesn't get hammered, and loop() pauses less
    // often during a long outage.
    if (now - lastMqttAttemptMs >= mqttRetryMs) {
      lastMqttAttemptMs = now;
      if (mqttTryConnect()) {
        mqttRetryMs = MQTT_RETRY_MIN_MS;
      } else {
        mqttRetryMs = (mqttRetryMs * 2 > MQTT_RETRY_MAX_MS) ? MQTT_RETRY_MAX_MS : mqttRetryMs * 2;
        Serial.printf("[MQTT] Next attempt in %lus\n", (unsigned long)(mqttRetryMs / 1000));
      }
    }
  }

  // ---- Remote commands (Stage 4) ----
  // The MQTT callback only parks a command in pendingCmd. Forwarding it to the
  // node can take up to CMD_HTTP_TIMEOUT_MS, so it happens here, outside the
  // callback. Only loop() waits; the web server keeps answering.
  processPendingCommand();
  publishPendingAck();

  // ---- MQTT publishing ----
  // The timer is NOT reset by reconnects, so even if the connection flaps we
  // never publish faster than every 20 s and never break ThingSpeak's limit.
  if (mqttUp && now - lastPublishAttemptMs >= MQTT_PUBLISH_INTERVAL_MS) {
    lastPublishAttemptMs = now;
    mqttPublishReadings();
  }

  // ---- Node state changes ----
  // Prints a line only when a node's state changes, e.g. online -> stale.
  // That gives clean evidence for the "node disconnection" test.
  if (now - lastStateCheckMs >= STATE_CHECK_INTERVAL_MS) {
    lastStateCheckMs = now;
    NodeHealth current[NODE_COUNT];
    uint32_t   ageS[NODE_COUNT];
    if (lockNodes()) {
      // millis() is read inside the lock on purpose. If we used 'now' from the
      // top of loop(), a post arriving in between could have a lastSeenMs
      // newer than 'now', and the subtraction would go wrong.
      const uint32_t t = millis();
      for (uint8_t i = 0; i < NODE_COUNT; i++) {
        current[i] = computeHealth(nodes[i], t);
        ageS[i]    = nodes[i].everSeen ? (t - nodes[i].lastSeenMs) / 1000 : 0;
      }
      unlockNodes();
      for (uint8_t i = 0; i < NODE_COUNT; i++) {
        if (current[i] != reportedHealth[i]) {
          Serial.printf("[NODE] %s: %s -> %s (last heard %lus ago)\n", nodes[i].id,
                        healthName(reportedHealth[i]), healthName(current[i]), (unsigned long)ageS[i]);
          reportedHealth[i] = current[i];
        }
      }
    }
  }

  // ---- Periodic health line ----
  if (now - lastPrintMs >= STATUS_PRINT_INTERVAL_MS) {
    lastPrintMs = now;
    Serial.printf("[STATUS] up=%lus  STA=%s  MQTT=%s  AP clients=%d  %s=%s  %s=%s  free heap=%u\n",
                  (unsigned long)(now / 1000),
                  staConnected ? "online" : "offline",
                  mqttUp ? "up" : "down",
                  WiFi.softAPgetStationNum(),
                  nodes[0].id, healthName(reportedHealth[0]),
                  nodes[1].id, healthName(reportedHealth[1]),
                  ESP.getFreeHeap());
  }
}
