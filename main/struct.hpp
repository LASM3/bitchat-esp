#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <stdio.h>

using namespace std;

//string debug = "F47B5E2D-4A9E-4C5A-9B3F-8E1D2C3A4B5A";
//string release = "F47B5E2D-4A9E-4C5A-9B3F-8E1D2C3A4B5C";

/// Unique peer identifier (8 bytes, random or hashed)
using PeerID = uint64_t;

/// Unique message identifier
using MessageID = uint64_t;

/// Used when sending to everyone
constexpr PeerID BROADCAST_ID = 0xFFFFFFFFFFFFFFFF;

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

struct PeerInfo {
  PeerID id;
  string nickname;
  bool isBlocked  = false;
  bool isFavorite = false;
};
