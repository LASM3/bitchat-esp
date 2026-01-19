#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <stdio.h>  
#include <map>

using namespace std;

//string debug = "F47B5E2D-4A9E-4C5A-9B3F-8E1D2C3A4B5A";
//string release = "F47B5E2D-4A9E-4C5A-9B3F-8E1D2C3A4B5C";

/// Unique peer identifier (8 bytes, random or hashed)
using PeerID = uint64_t;

static const char* BLE_TAG = "BITCHAT_BLE";

static const char* MAIN_TAG = "BITCHAT";

static const char* NVS_TAG = "BITCHAT_NVS";

static const char* ERR_TAG = "BITCHAT_ERR";

static const char* MESH_TAG = "BITCHAT_MESH";

/// Unique message identifier
using MessageID = uint64_t;

// Testnet UUID in little-endian (BLE format)  
static const uint8_t TESTNET_UUID_LE[16] = {  
    0x5A, 0x4B, 0x3A, 0x2C, 0x1D, 0x8E, 0x3F, 0x9B,  
    0x5A, 0x4C, 0x9E, 0x4A, 0x2D, 0x5E, 0x7B, 0xF4  
};  
  
// Mainnet UUID in little-endian  
static const uint8_t MAINNET_UUID_LE[16] = {  
    0x5C, 0x4B, 0x3A, 0x2C, 0x1D, 0x8E, 0x3F, 0x9B,  
    0x5A, 0x4C, 0x9E, 0x4A, 0x2D, 0x5E, 0x7B, 0xF4  
};  

/// Used when sending to everyone
constexpr PeerID BROADCAST_ID = 0xFFFFFFFFFFFFFFFF;

struct PeerInfo {
    uint64_t peerID;
    int8_t rssi;
    uint8_t version;
    uint32_t last_seen;
};

extern map<uint64_t, PeerInfo> peers;


enum class PacketType : uint8_t {
  Chat            = 1,  // normal chat message
  Ack             = 2,  // delivery acknowledgement
  ReadReceipt     = 3,  // message read confirmation
  FragmentStart   = 4,  // start of fragmented payload
  FragmentChunk   = 5,  // middle fragment
  FragmentEnd     = 6   // final fragment
};

/// This is what actually goes over BLE
struct Packet {
  uint8_t     version = 1;
  PacketType  type;

  uint8_t     ttl;        // hop count
  PeerID      sender;     // who sent it
  PeerID      recipient;  // who should receive it (or BROADCAST)

  MessageID   messageId;  // used for deduplication
  string mac;

  vector<uint8_t> payload; // raw data (message, ack, fragment...)
};

struct ChatMessage {
  MessageID  id;
  PeerID     sender;
  uint64_t   timestamp;   // milliseconds since epoch
  string text;
};

struct AckMessage {
  MessageID messageId; // message being acknowledged
  PeerID    from;      // who sent the ACK
};


struct ReadReceipt {
  MessageID messageId; // message that was read
  PeerID    reader;    // who read it
};

/// Sent before fragments
struct FragmentStart {
  MessageID originalMessageId;
  uint32_t  totalSize;     // full payload size
  uint16_t  totalChunks;   // how many fragments
};

/// Sent for each chunk
struct FragmentChunk {
  MessageID originalMessageId;
  uint16_t  index;         // fragment number
  vector<uint8_t> data;
};

/// Sent after last fragment
struct FragmentEnd {
  MessageID originalMessageId;
};
