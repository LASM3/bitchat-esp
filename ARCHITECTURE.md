> [!WARNING]
> This document is AI-generated and could contain mistakes!
# Bitchat ESP32: Code and Architecture

This guide explains the firmware from the ground up: what the device does,
how its pieces fit together, and how a packet moves through the code.

## 1. What this firmware is

Bitchat is a peer-to-peer messaging system. Instead of requiring a central
server, nearby devices discover one another over Bluetooth Low Energy and can pass packets through peers that act as relays.

This project adapts that role to an ESP32. At startup, the ESP32:

1. Initializes non-volatile storage (NVS).
2. Loads or creates cryptographic identity keys and derives its peer ID.
3. Starts the ESP-IDF Bluetooth controller and Bluedroid BLE stack.
4. Continuously scans nearby BLE advertisements for Bitchat mainnet or testnet service UUIDs.
5. Registers a BLE GATT relay service and advertises it so a Bitchat device can connect, subscribe, and exchange protocol packets.

The current firmware is a relay and peer-discovery prototype, not a complete
chat client. Received public messages are logged to the serial console;
encrypted packets are recognized but not decrypted. There is no serial
command interface for sending messages, and the peer table is held in RAM.

## 2. A few concepts

- **Advertisement:** A short broadcast that lets nearby devices notice a service without first connecting. The service UUID identifies the Bitchat network.
- **GAP:** The BLE layer responsible for discovery, scanning, advertising, and connection procedures.
- **GATT:** BLE's connected client/server data model. This firmware provides a service and characteristic that connected peers use to exchange packet bytes.
- **NVS:** ESP-IDF's non-volatile key/value storage. This firmware keeps the random seeds for its cryptographic identity here, so its peer ID survives restarts.
- **Peer ID:** The eight-byte Bitchat identity derived here from the cryptographic identity's public key. It is NOT the BLE radio address.
- **Packet announcement:** A signed Bitchat packet advertising the ESP32's nickname and public keys to a connected subscriber.

## 3. Architecture at a glance

```text
                         ESP32 firmware
┌─────────────────────────────────────────────────────────────┐
│ main.cpp: app_main()                                        │
│   NVS → peer identity → BLE stack → scan → relay service    │
│                                                     │       │
│                                                     v       │
│ BLE GAP callback ── advertisements ──> mesh.cpp             │
│       │                                                     │
│       └── GATT callback <── peer connections / packet bytes │
│                         │                                   │
│                         ├── copy packet to FreeRTOS queue   │
│                         │       └── protocol worker         │
│                         │            └── bitchat_protocol   │
│                         │                 parse / verify /   │
│                         │                 log and announce  │
│                         └── forward packet to subscribed    │
│                             connections, except its source  │
│                                                             │
│ nvs.cpp ── NVS initialization                                │
│ struct.hpp ── shared IDs, UUIDs, peer and packet types       │
└─────────────────────────────────────────────────────────────┘
```

In the diagram, “send to subscribed connections” means GATT notifications
(implemented using ESP-IDF's `esp_ble_gatts_send_indicate` with confirmation
disabled). The firmware keeps a small cache of recently forwarded packet
fingerprints to help prevent a packet circulating repeatedly.

## 4. Startup, step by step

The entry point is `app_main()` in [`main/main.cpp`](./main/main.cpp).

1. **Initialize NVS.** `nvs_init()` in [`main/nvs.cpp`](./main/nvs.cpp) calls
   `nvs_flash_init()`. A failure stops startup.
2. **Initialize the BitChat identity.** `bitchat_peer_init()` in
   [`main/bitchat_protocol.cpp`](./main/bitchat_protocol.cpp) initializes
   libsodium and opens the `bitchat_peer` NVS namespace. It loads or generates
   a Noise key seed and an Ed25519 signing seed, commits them, derives the
   public/private key pairs, and hashes the Noise public key to form the
   eight-byte peer ID. A failure stops startup.
3. **Initialize Bluetooth.** `ble_init()` in
   [`main/blehandler.cpp`](./main/blehandler.cpp) releases classic Bluetooth
   memory, initializes and enables the BLE controller, then initializes and
   enables Bluedroid.
4. **Start scanning.** `ble_start_scan(0)` configures active continuous BLE
   scanning. Scan results are delivered to the GAP callback.
5. **Start the relay.** `ble_start_relay()` configures the local GATT MTU,
   creates a bounded FreeRTOS protocol-work queue and worker task, registers
   the GATT callback, and registers the relay GATT application.
6. **Keep the application task alive.** `app_main()` waits in a one-second
   FreeRTOS delay loop. BLE events and packet work are handled by callbacks
   and the protocol worker.

Each startup operation is checked. On an error, the application logs it and
returns the ESP-IDF error code rather than continuing with a partially
initialized device.

## 5. BLE discovery and network selection

[`main/mesh.cpp`](./main/mesh.cpp) parses the BLE advertising data as
length/type/value fields. For complete and incomplete 128-bit service UUID
lists, it compares each UUID with the mainnet and testnet UUID byte arrays in
[`main/struct.hpp`](./main/struct.hpp), then logs the first detection of each
network.

The active network is chosen at build time with the `Bitchat BLE network`
choice in [`main/Kconfig.projbuild`](./main/Kconfig.projbuild). The same
selection controls the service UUID used in the relay's advertisement and
GATT service. The checked-in `sdkconfig` currently selects mainnet.

Detection currently records only two process-lifetime booleans and emits log
messages. It does not create entries in the peer table, connect to detected
devices, or dynamically switch the relay's selected network.

## 6. The GATT relay

The relay implementation lives in [`main/blehandler.cpp`](./main/blehandler.cpp).
It registers one primary service using the selected 128-bit network UUID. The
service has a custom 128-bit characteristic with write, write-without-response,
and notification properties, plus the standard Client Characteristic
Configuration Descriptor (CCCD).

The relay advertises its service and the local name `BitChat ESP32 Relay`.
It starts advertising after the advertising data, scan response, and GATT
service are ready. When a peer connects, the connection is tracked along with
its negotiated MTU. After the peer enables notifications on the CCCD, the
firmware queues work to send it a BitChat announcement.

Writes to the packet characteristic are handled as follows:

1. Copy the incoming bytes into owned memory and enqueue them for protocol
   processing. The queue is bounded (eight items); allocation failures and a
   full queue are logged, and that protocol-processing item is dropped.
2. Calculate a fingerprint and check the recent-packet cache. The fingerprint
   excludes the TTL byte and any transport padding so relayed copies can be
   recognized as the same packet.
3. If the packet is new, send it to each other active connection that
   subscribed to notifications. The source connection is excluded. Frames
   larger than a recipient's negotiated MTU minus the ATT overhead are
   skipped and logged.

The forwarding path and the protocol worker are distinct: a packet can be
forwarded even if the bounded queue cannot accept a copy for local
inspection. The recent-packet cache contains 64 entries and is an in-memory
ring; it is not a durable or network-wide deduplication mechanism.

## 7. Protocol identity, announcements, and packet handling

[`main/bitchat_protocol.cpp`](./main/bitchat_protocol.cpp) contains the
BitChat-specific packet and identity logic.

### Persistent identity

Two random seeds are stored as NVS blobs. The Noise seed derives a public
keypair used for peer identity and the Ed25519 seed derives a signing keypair.
The first eight bytes of SHA-256 over the Noise public key become the BitChat
peer ID. The nickname in this implementation is the fixed string `ESP32`.

The seeds are generated only when their NVS entries do not exist. Reflashing
without erasing NVS therefore preserves the identity; erasing NVS creates a
new one.

### Announcement creation

Once a subscriber is ready, the protocol worker creates a version-1 announce
packet containing the nickname, Noise public key, and signing public key.
Those values are encoded as type/length/value fields. The packet is signed
with Ed25519 before it is sent.

Announcements need a plausible timestamp. The firmware uses the system
clock when it is set. Otherwise, it can temporarily estimate time from a
timestamp learned from a verified packet and the ESP32 uptime. If it has no
usable time, announcement generation returns no packet and waits for another
opportunity.

### Incoming packet processing

`bitchat_process_packet()` validates the basic frame shape and recognizes
version-1 and version-2 headers. It ignores packets from its own peer ID and
handles these packet types:

- **Announce:** Parses the nickname and public keys; checks that the peer ID
  matches the announced Noise public key; verifies a signature when present;
  and stores the peer's nickname, version, last-seen uptime, and signing key
  in the in-memory peer map. The map is capped at 64 peers. Once a signing key
  has been pinned for a peer ID, an unsigned announcement or a changed key is
  rejected.
- **Public message:** Verifies a signature if the sender has a pinned signing
  key and the packet includes a signature. It optionally decompresses the
  payload, sanitizes control characters, and logs up to 220 characters.
- **Noise handshake and encrypted message:** Logs that an encrypted packet
  was received, but does not decrypt or display its contents.
- **Other packet types:** They are not acted on by the current handler.

The shared packet and peer data definitions, including network UUIDs, are in
[`main/struct.hpp`](./main/struct.hpp). The public interface for identity and
packet processing is declared in
[`main/bitchat_protocol.hpp`](./main/bitchat_protocol.hpp).

## 8. Source files and responsibilities

| File | Responsibility |
|---|---|
| [`main/main.cpp`](./main/main.cpp) | Firmware entry point and startup ordering. |
| [`main/blehandler.cpp`](./main/blehandler.cpp) | BLE controller setup, scanning, advertising, GATT service, connections, packet queue, and forwarding. |
| [`main/blehandler.hpp`](./main/blehandler.hpp) | BLE functions exposed to the rest of the firmware. |
| [`main/mesh.cpp`](./main/mesh.cpp) | Parses advertisements and detects mainnet/testnet service UUIDs. |
| [`main/mesh.hpp`](./main/mesh.hpp) | Advertisement parser declaration. |
| [`main/bitchat_protocol.cpp`](./main/bitchat_protocol.cpp) | Persistent identity, packet parsing, announcements, signatures, decompression, and logging. |
| [`main/bitchat_protocol.hpp`](./main/bitchat_protocol.hpp) | Protocol and peer-identity function declarations. |
| [`main/nvs.cpp`](./main/nvs.cpp) | Initializes ESP-IDF non-volatile storage. |
| [`main/nvs.hpp`](./main/nvs.hpp) | NVS initialization declaration. |
| [`main/struct.hpp`](./main/struct.hpp) | Shared packet, peer, identifier, and network UUID definitions. |
| [`main/Kconfig.projbuild`](./main/Kconfig.projbuild) | Mainnet/testnet build choice. |
| [`main/CMakeLists.txt`](./main/CMakeLists.txt) | Registers firmware source files and ESP-IDF component dependencies. |
| [`partitions.csv`](./partitions.csv) | Flash partition layout, including the NVS partition used for identity seeds. |
| [`CMakeLists.txt`](./CMakeLists.txt) | Loads ESP-IDF's CMake project support and declares the project. |

## 9. Current boundaries and extension points

- Peer discovery currently logs network UUID sightings; it does not yet
  maintain a discovered-device list or connect to advertisers.
- GATT connections are used for relaying bytes. This code does not implement
  pairing or BLE link encryption.
- Public messages can be parsed and logged, but there is no user-facing send
  path, chat UI, or persistent message history.
- Encrypted packet contents are not decrypted in this firmware.
- Peer records and the forwarding fingerprint cache live only in RAM.
- BLE packet forwarding is limited by the negotiated MTU. Oversized frames
  are dropped rather than fragmented by the relay.

For a new feature, identify whether it belongs at the BLE transport layer
(`blehandler.cpp`), advertisement discovery (`mesh.cpp`), or Bitchat packet
layer (`bitchat_protocol.cpp`). Keep transport forwarding separate from
protocol interpretation where possible, and add tests or hardware validation
for packet-format and BLE behavior.
