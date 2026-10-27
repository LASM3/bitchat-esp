#include "bitchat_protocol.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <map>
#include <string>

#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs.h"
#include "esp32/rom/miniz.h"
#include "sodium.h"

#include "struct.hpp"

namespace {

constexpr uint8_t kPacketVersion1 = 1;
constexpr uint8_t kPacketVersion2 = 2;
constexpr uint8_t kAnnounceType = 0x01;
constexpr uint8_t kPublicMessageType = 0x02;
constexpr uint8_t kNoiseHandshakeType = 0x10;
constexpr uint8_t kEncryptedMessageType = 0x11;
constexpr uint8_t kHasRecipient = 0x01;
constexpr uint8_t kHasSignature = 0x02;
constexpr uint8_t kIsCompressed = 0x04;
constexpr uint8_t kHasRoute = 0x08;
constexpr uint8_t kIsRSR = 0x10;
constexpr uint8_t kSignatureSize = crypto_sign_BYTES;
constexpr size_t kMaxPacketSize = 1024;
constexpr size_t kMaxDecompressedMessageSize = 512;
constexpr uint64_t kEarliestTimestampMs = 1577836800000ULL;
constexpr uint64_t kLatestTimestampMs = 4102444800000ULL;
constexpr uint64_t kTimestampLifetimeMs = 15ULL * 60ULL * 1000ULL;
constexpr size_t kMaxTrackedPeers = 64;
constexpr char kPeerTag[] = "PEER";
constexpr char kNickname[] = "Horcrux";

uint8_t s_noise_seed[crypto_box_SEEDBYTES];
uint8_t s_signing_seed[crypto_sign_SEEDBYTES];
uint8_t s_noise_public_key[crypto_box_PUBLICKEYBYTES];
uint8_t s_noise_secret_key[crypto_box_SECRETKEYBYTES];
uint8_t s_signing_public_key[crypto_sign_PUBLICKEYBYTES];
uint8_t s_signing_secret_key[crypto_sign_SECRETKEYBYTES];
uint8_t s_local_peer_id[8];
bool s_identity_ready = false;
uint64_t s_last_seen_timestamp_ms = 0;
uint64_t s_last_seen_uptime_ms = 0;

struct PacketView {
    uint8_t version;
    uint8_t type;
    uint8_t flags;
    uint64_t timestamp_ms;
    const uint8_t* sender_id;
    const uint8_t* payload;
    size_t payload_length;
    const uint8_t* signature;
    bool compressed;
};

uint16_t read_be16(const uint8_t* data) {
    return static_cast<uint16_t>((static_cast<uint16_t>(data[0]) << 8) |
                                 data[1]);
}

uint32_t read_be32(const uint8_t* data) {
    return (static_cast<uint32_t>(data[0]) << 24) |
           (static_cast<uint32_t>(data[1]) << 16) |
           (static_cast<uint32_t>(data[2]) << 8) |
           static_cast<uint32_t>(data[3]);
}

uint64_t read_be64(const uint8_t* data) {
    uint64_t value = 0;
    for (size_t i = 0; i < 8; ++i) {
        value = (value << 8) | data[i];
    }
    return value;
}

void write_be16(uint8_t* data, uint16_t value) {
    data[0] = static_cast<uint8_t>(value >> 8);
    data[1] = static_cast<uint8_t>(value);
}

void write_be64(uint8_t* data, uint64_t value) {
    for (int i = 7; i >= 0; --i) {
        data[i] = static_cast<uint8_t>(value);
        value >>= 8;
    }
}

bool load_or_create_seed(nvs_handle_t handle, const char* key,
                         uint8_t* seed, size_t seed_length) {
    size_t stored_length = seed_length;
    esp_err_t err = nvs_get_blob(handle, key, seed, &stored_length);
    if (err == ESP_OK) {
        if (stored_length == seed_length) {
            return true;
        }
        ESP_LOGE(kPeerTag, "Stored identity seed '%s' has an invalid size", key);
        return false;
    }
    if (err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(kPeerTag, "Could not read identity seed '%s': %s",
                 key, esp_err_to_name(err));
        return false;
    }

    esp_fill_random(seed, seed_length);
    err = nvs_set_blob(handle, key, seed, seed_length);
    if (err != ESP_OK) {
        ESP_LOGE(kPeerTag, "Could not save identity seed '%s': %s",
                 key, esp_err_to_name(err));
        return false;
    }
    return true;
}

bool parse_packet(const uint8_t* data, size_t length, PacketView* packet) {
    if (data == nullptr || packet == nullptr || length < 22) {
        return false;
    }

    const uint8_t version = data[0];
    const size_t header_length =
        version == kPacketVersion1 ? 14 :
        version == kPacketVersion2 ? 16 : 0;
    if (header_length == 0 || length < header_length + 8) {
        return false;
    }

    const uint8_t flags = data[11];
    const uint32_t payload_length =
        version == kPacketVersion1 ? read_be16(data + 12) : read_be32(data + 12);
    if (payload_length > length) {
        return false;
    }

    size_t offset = header_length;
    const uint8_t* sender_id = data + offset;
    offset += 8;

    if ((flags & kHasRecipient) != 0) {
        if (length - offset < 8) {
            return false;
        }
        offset += 8;
    }

    if (version >= kPacketVersion2 && (flags & kHasRoute) != 0) {
        if (length - offset < 1) {
            return false;
        }
        const size_t route_length = 1 + static_cast<size_t>(data[offset]) * 8;
        if (route_length > length - offset) {
            return false;
        }
        offset += route_length;
    }

    if (payload_length > length - offset) {
        return false;
    }
    const uint8_t* payload = data + offset;
    offset += payload_length;

    const uint8_t* signature = nullptr;
    if ((flags & kHasSignature) != 0) {
        if (length - offset < kSignatureSize) {
            return false;
        }
        signature = data + offset;
    }

    packet->version = version;
    packet->type = data[1];
    packet->flags = flags;
    packet->timestamp_ms = read_be64(data + 3);
    packet->sender_id = sender_id;
    packet->payload = payload;
    packet->payload_length = payload_length;
    packet->signature = signature;
    packet->compressed = (flags & kIsCompressed) != 0;
    return true;
}

size_t make_signature_preimage(const uint8_t* packet_data,
                               const PacketView& packet,
                               uint8_t* output, size_t capacity) {
    if (packet_data == nullptr) {
        return 0;
    }

    const size_t header_length = packet.version == kPacketVersion1 ? 14 : 16;
    size_t offset = header_length + 8;
    if ((packet.flags & kHasRecipient) != 0) {
        offset += 8;
    }
    if (packet.version >= kPacketVersion2 && (packet.flags & kHasRoute) != 0) {
        offset += 1 + static_cast<size_t>(packet_data[offset]) * 8;
    }
    const size_t preimage_length = offset + packet.payload_length;
    if (preimage_length > capacity) {
        return 0;
    }

    memcpy(output, packet_data, preimage_length);
    output[2] = 0;
    output[11] &= static_cast<uint8_t>(~(kHasSignature | kIsRSR));

    const size_t padded_length = preimage_length + 16;
    size_t block_length = preimage_length;
    if (padded_length <= 256) {
        block_length = 256;
    } else if (padded_length <= 512) {
        block_length = 512;
    } else if (padded_length <= 1024) {
        block_length = 1024;
    } else if (padded_length <= 2048) {
        block_length = 2048;
    }

    const size_t padding_length = block_length - preimage_length;
    if (padding_length > 0 && padding_length <= 255 &&
        block_length <= capacity) {
        memset(output + preimage_length,
               static_cast<int>(padding_length), padding_length);
        return block_length;
    }
    return preimage_length;
}

bool verify_packet_signature(const uint8_t* packet_data,
                             const PacketView& packet,
                             const uint8_t public_key[crypto_sign_PUBLICKEYBYTES]) {
    if (packet.signature == nullptr) {
        return false;
    }

    uint8_t preimage[kMaxPacketSize];
    const size_t preimage_length = make_signature_preimage(
        packet_data, packet, preimage, sizeof(preimage));
    return preimage_length != 0 &&
        crypto_sign_ed25519_verify_detached(
            packet.signature, preimage, preimage_length, public_key) == 0;
}

uint64_t peer_id_key(const uint8_t sender_id[8]) {
    uint64_t id = 0;
    for (size_t i = 0; i < 8; ++i) {
        id = (id << 8) | sender_id[i];
    }
    return id;
}

void format_peer_id(const uint8_t sender_id[8], char output[17]) {
    for (size_t i = 0; i < 8; ++i) {
        snprintf(output + i * 2, 3, "%02x", sender_id[i]);
    }
    output[16] = '\0';
}

std::string safe_text(const uint8_t* data, size_t length, size_t max_length) {
    const size_t count = std::min(length, max_length);
    std::string result;
    result.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const uint8_t value = data[i];
        result.push_back(value < 0x20 || value == 0x7f ? ' ' :
                         static_cast<char>(value));
    }
    return result;
}

bool decode_announcement(const uint8_t* payload, size_t length,
                         std::string* nickname,
                         uint8_t noise_key[crypto_box_PUBLICKEYBYTES],
                         uint8_t signing_key[crypto_sign_PUBLICKEYBYTES]) {
    bool has_nickname = false;
    bool has_noise_key = false;
    bool has_signing_key = false;
    size_t offset = 0;
    while (offset + 2 <= length) {
        const uint8_t type = payload[offset++];
        const size_t value_length = payload[offset++];
        if (value_length > length - offset) {
            return false;
        }
        const uint8_t* value = payload + offset;
        if (type == 0x01) {
            *nickname = safe_text(value, value_length, 64);
            has_nickname = true;
        } else if (type == 0x02 && value_length == crypto_box_PUBLICKEYBYTES) {
            memcpy(noise_key, value, crypto_box_PUBLICKEYBYTES);
            has_noise_key = true;
        } else if (type == 0x03 && value_length == crypto_sign_PUBLICKEYBYTES) {
            memcpy(signing_key, value, crypto_sign_PUBLICKEYBYTES);
            has_signing_key = true;
        }
        offset += value_length;
    }
    return has_nickname && has_noise_key && has_signing_key && offset == length;
}

bool decompress_message(const PacketView& packet, uint8_t* output,
                        size_t output_capacity, size_t* output_length) {
    const size_t size_field_length =
        packet.version == kPacketVersion1 ? 2 : 4;
    if (packet.payload_length <= size_field_length) {
        return false;
    }

    const uint32_t expected_length =
        packet.version == kPacketVersion1
            ? read_be16(packet.payload)
            : read_be32(packet.payload);
    if (expected_length == 0 || expected_length > output_capacity) {
        return false;
    }

    const size_t decompressed_length = tinfl_decompress_mem_to_mem(
        output, output_capacity, packet.payload + size_field_length,
        packet.payload_length - size_field_length,
        TINFL_FLAG_PARSE_ZLIB_HEADER);
    if (decompressed_length == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED ||
        decompressed_length != expected_length) {
        return false;
    }

    *output_length = decompressed_length;
    return true;
}

void remember_network_timestamp(uint64_t timestamp_ms) {
    if (timestamp_ms < kEarliestTimestampMs ||
        timestamp_ms > kLatestTimestampMs ||
        timestamp_ms <= s_last_seen_timestamp_ms) {
        return;
    }

    s_last_seen_timestamp_ms = timestamp_ms;
    s_last_seen_uptime_ms =
        static_cast<uint64_t>(esp_timer_get_time()) / 1000ULL;
}

uint64_t current_timestamp_ms() {
    const time_t now = time(nullptr);
    if (now >= 1577836800) {
        return static_cast<uint64_t>(now) * 1000ULL;
    }

    if (s_last_seen_timestamp_ms != 0) {
        const uint64_t uptime_ms =
            static_cast<uint64_t>(esp_timer_get_time()) / 1000ULL;
        if (uptime_ms - s_last_seen_uptime_ms > kTimestampLifetimeMs) {
            return 0;
        }
        return s_last_seen_timestamp_ms + (uptime_ms - s_last_seen_uptime_ms);
    }
    return 0;
}

}  // namespace

esp_err_t bitchat_peer_init() {
    if (sodium_init() < 0) {
        ESP_LOGE(kPeerTag, "libsodium initialization failed");
        return ESP_FAIL;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open("bitchat_peer", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(kPeerTag, "Could not open identity storage: %s",
                 esp_err_to_name(err));
        return err;
    }

    const bool seeds_loaded =
        load_or_create_seed(handle, "noise_seed", s_noise_seed,
                            sizeof(s_noise_seed)) &&
        load_or_create_seed(handle, "sign_seed", s_signing_seed,
                            sizeof(s_signing_seed));
    if (!seeds_loaded) {
        nvs_close(handle);
        return ESP_FAIL;
    }

    err = nvs_commit(handle);
    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGE(kPeerTag, "Could not commit identity storage: %s",
                 esp_err_to_name(err));
        return err;
    }

    if (crypto_box_seed_keypair(s_noise_public_key, s_noise_secret_key,
                                s_noise_seed) != 0 ||
        crypto_sign_ed25519_seed_keypair(s_signing_public_key,
                                         s_signing_secret_key,
                                         s_signing_seed) != 0) {
        ESP_LOGE(kPeerTag, "Could not derive BitChat identity keys");
        return ESP_FAIL;
    }

    uint8_t fingerprint[crypto_hash_sha256_BYTES];
    if (crypto_hash_sha256(fingerprint, s_noise_public_key,
                           sizeof(s_noise_public_key)) != 0) {
        ESP_LOGE(kPeerTag, "Could not derive BitChat peer ID");
        return ESP_FAIL;
    }
    memcpy(s_local_peer_id, fingerprint, sizeof(s_local_peer_id));
    s_identity_ready = true;

    char peer_id[17];
    format_peer_id(s_local_peer_id, peer_id);
    ESP_LOGI(kPeerTag, "BitChat identity ready: %s (nickname %s)",
             peer_id, kNickname);
    return ESP_OK;
}

size_t bitchat_make_announcement(uint8_t* output, size_t capacity) {
    if (!s_identity_ready || output == nullptr || capacity < 161) {
        return 0;
    }

    uint8_t payload[80];
    size_t payload_length = 0;
    const size_t nickname_length = strlen(kNickname);
    payload[payload_length++] = 0x01;
    payload[payload_length++] = static_cast<uint8_t>(nickname_length);
    memcpy(payload + payload_length, kNickname, nickname_length);
    payload_length += nickname_length;
    payload[payload_length++] = 0x02;
    payload[payload_length++] = crypto_box_PUBLICKEYBYTES;
    memcpy(payload + payload_length, s_noise_public_key,
           crypto_box_PUBLICKEYBYTES);
    payload_length += crypto_box_PUBLICKEYBYTES;
    payload[payload_length++] = 0x03;
    payload[payload_length++] = crypto_sign_PUBLICKEYBYTES;
    memcpy(payload + payload_length, s_signing_public_key,
           crypto_sign_PUBLICKEYBYTES);
    payload_length += crypto_sign_PUBLICKEYBYTES;

    const size_t unsigned_length = 14 + 8 + payload_length;
    if (capacity < unsigned_length + kSignatureSize) {
        return 0;
    }
    const uint64_t timestamp_ms = current_timestamp_ms();
    if (timestamp_ms == 0) {
        return 0;
    }

    memset(output, 0, unsigned_length + kSignatureSize);
    output[0] = kPacketVersion1;
    output[1] = kAnnounceType;
    output[2] = 7;
    write_be64(output + 3, timestamp_ms);
    output[11] = 0;
    write_be16(output + 12, static_cast<uint16_t>(payload_length));
    memcpy(output + 14, s_local_peer_id, sizeof(s_local_peer_id));
    memcpy(output + 22, payload, payload_length);

    uint8_t signing_preimage[256];
    memcpy(signing_preimage, output, unsigned_length);
    signing_preimage[2] = 0;
    const size_t padding_length = sizeof(signing_preimage) - unsigned_length;
    memset(signing_preimage + unsigned_length,
           static_cast<int>(padding_length), padding_length);

    unsigned long long signature_length = 0;
    if (crypto_sign_ed25519_detached(
            output + unsigned_length, &signature_length,
            signing_preimage, sizeof(signing_preimage),
            s_signing_secret_key) != 0 ||
        signature_length != kSignatureSize) {
        ESP_LOGE(kPeerTag, "Could not sign BitChat announcement");
        return 0;
    }
    output[11] |= kHasSignature;
    return unsigned_length + kSignatureSize;
}

void bitchat_process_packet(const uint8_t* data, size_t length) {
    PacketView packet = {};
    if (!parse_packet(data, length, &packet)) {
        ESP_LOGD(kPeerTag, "Ignoring non-BitChat frame (%u bytes)",
                 static_cast<unsigned>(length));
        return;
    }

    char peer_id[17];
    format_peer_id(packet.sender_id, peer_id);

    if (memcmp(packet.sender_id, s_local_peer_id,
               sizeof(s_local_peer_id)) == 0) {
        return;
    }

    if (packet.type == kAnnounceType) {
        std::string nickname;
        uint8_t noise_key[crypto_box_PUBLICKEYBYTES];
        uint8_t signing_key[crypto_sign_PUBLICKEYBYTES];
        if (packet.compressed ||
            !decode_announcement(packet.payload, packet.payload_length,
                                 &nickname, noise_key, signing_key)) {
            ESP_LOGW(kPeerTag, "Malformed announce from %s", peer_id);
            return;
        }

        uint8_t fingerprint[crypto_hash_sha256_BYTES];
        if (crypto_hash_sha256(fingerprint, noise_key, sizeof(noise_key)) != 0 ||
            memcmp(fingerprint, packet.sender_id, sizeof(s_local_peer_id)) != 0) {
            ESP_LOGW(kPeerTag, "Announce peer ID does not match Noise key: %s",
                     peer_id);
            return;
        }

        const bool verified = verify_packet_signature(data, packet, signing_key);
        if (packet.signature != nullptr && !verified) {
            ESP_LOGW(kPeerTag, "Invalid announce signature from %s", peer_id);
            return;
        }
        if (verified) {
            remember_network_timestamp(packet.timestamp_ms);
        }

        const uint64_t id = peer_id_key(packet.sender_id);
        auto peer_it = peers.find(id);
        if (peer_it != peers.end() &&
            peer_it->second.has_signing_public_key) {
            if (packet.signature == nullptr) {
                ESP_LOGW(kPeerTag,
                         "Ignoring unsigned announce for verified peer %s",
                         peer_id);
                return;
            }
            if (memcmp(peer_it->second.signing_public_key, signing_key,
                       sizeof(signing_key)) != 0) {
                ESP_LOGW(kPeerTag,
                         "Signing key changed for peer %s; keeping pinned key",
                         peer_id);
                return;
            }
        }

        const bool is_new_peer = peer_it == peers.end();
        if (is_new_peer && peers.size() >= kMaxTrackedPeers) {
            ESP_LOGW(kPeerTag, "Peer table is full; ignoring %s", peer_id);
            return;
        }
        PeerInfo& peer = peers[id];
        const bool changed = is_new_peer || peer.nickname != nickname;
        peer.peerID = id;
        peer.version = packet.version;
        peer.last_seen = static_cast<uint32_t>(
            static_cast<uint64_t>(esp_timer_get_time()) / 1000000ULL);
        peer.nickname = nickname;
        if (packet.signature != nullptr) {
            peer.has_signing_public_key = true;
            memcpy(peer.signing_public_key, signing_key, sizeof(signing_key));
        }
        if (changed) {
            ESP_LOGI(kPeerTag, "Peer %s: %s%s", peer_id, nickname.c_str(),
                     verified ? " [verified]" : " [unverified]");
        }
        return;
    }

    if (packet.type == kPublicMessageType) {
        const uint64_t id = peer_id_key(packet.sender_id);
        const auto peer_it = peers.find(id);
        const std::string nickname =
            peer_it == peers.end() ? peer_id : peer_it->second.nickname;
        bool verified = false;
        if (packet.signature != nullptr && peer_it != peers.end() &&
            peer_it->second.has_signing_public_key) {
            verified = verify_packet_signature(
                data, packet, peer_it->second.signing_public_key);
            if (!verified) {
                ESP_LOGW(kPeerTag, "Invalid message signature from %s", peer_id);
                return;
            }
        }
        if (verified) {
            remember_network_timestamp(packet.timestamp_ms);
        }
        uint8_t decompressed[kMaxDecompressedMessageSize];
        const uint8_t* message_data = packet.payload;
        size_t message_length = packet.payload_length;
        if (packet.compressed &&
            !decompress_message(packet, decompressed, sizeof(decompressed),
                                &message_length)) {
            ESP_LOGW(kPeerTag,
                     "Could not decompress message from %s (%u bytes)",
                     peer_id, static_cast<unsigned>(packet.payload_length));
            return;
        }
        if (packet.compressed) {
            message_data = decompressed;
        }
        const std::string message = safe_text(message_data, message_length, 220);
        ESP_LOGI(kPeerTag, "Message from %s (%s)%s: %s",
                 nickname.c_str(), peer_id,
                 verified ? "" : " [unverified]", message.c_str());
        return;
    }

    if (packet.type == kNoiseHandshakeType ||
        packet.type == kEncryptedMessageType) {
        ESP_LOGI(kPeerTag,
                 "Encrypted Bitchat packet from %s (%s), %u bytes; content unavailable",
                 peer_id, peers.count(peer_id_key(packet.sender_id)) != 0
                     ? peers[peer_id_key(packet.sender_id)].nickname.c_str()
                     : "unknown",
                 static_cast<unsigned>(packet.payload_length));
    }
}
