#pragma once
// =====================================================================
// gateway_types.h : data types used by the gateway (gateway only)
//
// gateway_types.h  VERSION 2
// Changelog:
//   v2  Stage 4: PendingCmd, ActuatorRoute and CmdResult added for the
//       MQTT remote-command path
//   v1  ReadingSpec, NodeHealth and NodeState moved here from gateway.ino
//
// Why these live in a separate header instead of gateway.ino:
// Before compiling, the Arduino IDE scans the .ino for functions and
// automatically writes a "prototype" (a one-line advance notice of each
// function) near the top of the file. It inserts them just before the
// first function it finds, which can be ABOVE some struct definitions.
// A prototype like computeHealth(const NodeState&) then mentions
// NodeState before the compiler has seen it, giving
// "'NodeState' does not name a type". Headers are pulled in by #include
// before any of that inserted code, so types defined here are always
// known in time. The same rule applies to every type added below.
// =====================================================================

#include <WiFi.h>     // IPAddress
#include <stdint.h>

constexpr uint8_t MAX_READINGS = 4;   // most readings any one node may send

// What one reading from a node must look like.
// One table per node (in gateway.ino), so adding a sensor later means
// adding one line there instead of writing new validation code.
struct ReadingSpec {
  const char* key;    // JSON key inside "readings"
  const char* unit;   // shown on the dashboard
  float minV, maxV;   // valid range from the sensor datasheet (config.h)
};

enum class NodeHealth : uint8_t { NeverSeen, Online, Stale, Offline };

struct NodeState {
  // Fixed at startup and never changed, so these can be read without the lock.
  const char*        id;
  IPAddress          expectedIp;
  const ReadingSpec* specs;
  uint8_t            specCount;
  char mode[8];
  char trigger[24];


  // Changed by every accepted post, so these must only be touched while
  // holding nodesMutex.
  bool     everSeen;
  uint32_t lastSeenMs;
  uint32_t lastSeq;
  float    values[MAX_READINGS];
  uint32_t postsOk, postsRejected, seqGaps, reboots;
};

// ---------------------------------------------------------------------
// Stage 4: remote commands
// ---------------------------------------------------------------------
constexpr uint8_t CMD_ID_LEN     = 16;   // "m17", or the cmd_id the sender chose (truncated)
constexpr uint8_t CMD_TEXT_LEN   = 32;   // canonical text such as "led:set:255,0,0"
constexpr uint8_t CMD_DETAIL_LEN = 40;   // short reason shown next to a failed command

// A command that has been received and checked, waiting to be forwarded.
struct PendingCmd {
  bool     used;                 // true while a command is waiting
  char     id[CMD_ID_LEN];
  char     actuator[12];         // e.g. "led"
  char     action[8];            // e.g. "set" or "off"
  bool     hasRgb;
  int      rgb[3];               // only meaningful when hasRgb is true
  uint32_t rxMs;                 // when the gateway received it
};

// Says which node owns which actuator, so the gateway knows where to forward.
struct ActuatorRoute {
  const char* actuator;
  uint8_t     nodeIdx;           // index into the nodes[] table
};

// Outcome of one command. Used for /api/status "last_cmd" and for the
// acknowledgments published back to ThingSpeak.
struct CmdResult {
  bool     valid;                // false until the first command has finished
  char     id[CMD_ID_LEN];
  char     target[8];            // node id, or "-" if the command never got that far
  char     status[12];           // executed / failed / rejected / timeout
  char     detail[CMD_DETAIL_LEN];
  char     text[CMD_TEXT_LEN];
  uint32_t rttMs;                // receipt to final result, in milliseconds
  uint32_t doneMs;               // millis() when it finished
};
