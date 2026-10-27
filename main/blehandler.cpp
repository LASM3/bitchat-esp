#include "blehandler.hpp"
#include <algorithm>
#include <cstdint>

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_log.h"

#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_defs.h"
#include "esp_gatt_common_api.h"
#include "sdkconfig.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "struct.hpp"
#include "mesh.hpp"
#include "bitchat_protocol.hpp"

#include <stdint.h>

using namespace std;

// static const char* TAG = "BITCHAT_BLE";

map<uint64_t, PeerInfo> peers;

namespace {

constexpr uint16_t kRelayAppId = 0x55;
constexpr uint16_t kMaxRelayConnections = CONFIG_BTDM_CTRL_BLE_MAX_CONN;
constexpr size_t kForwardedPacketCacheSize = 64;
constexpr uint16_t kCccdUuid = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
constexpr size_t kProtocolQueueLength = 8;

static const char* RELAY_TAG = "BITCHAT_RELAY";
static const uint8_t kCharacteristicUuid[16] = {
    0x5D, 0x4C, 0x3B, 0x2A, 0x1F, 0x0E, 0x9D, 0x8C,
    0x5B, 0x4A, 0xF6, 0xE5, 0xD4, 0xC3, 0xB2, 0xA1
};

#if defined(CONFIG_BITCHAT_NETWORK_MAINNET)
static const uint8_t* kActiveServiceUuid = MAINNET_UUID_LE;
#else
static const uint8_t* kActiveServiceUuid = TESTNET_UUID_LE;
#endif

struct RelayConnection {
    uint16_t conn_id;
    uint16_t mtu;
    bool active;
    bool subscribed;
    bool announced;
};

struct PacketFingerprint {
    uint64_t value;
    bool valid;
};

enum class ProtocolWorkType : uint8_t {
    Packet,
    AnnounceConnection
};

struct ProtocolWorkItem {
    ProtocolWorkType type;
    uint16_t conn_id;
    uint16_t length;
    uint8_t* data;
};

static RelayConnection s_connections[kMaxRelayConnections] = {};
static portMUX_TYPE s_connections_mux = portMUX_INITIALIZER_UNLOCKED;
static PacketFingerprint s_forwarded_packets[kForwardedPacketCacheSize] = {};
static size_t s_next_packet_fingerprint = 0;
static esp_gatt_if_t s_gatts_if = ESP_GATT_IF_NONE;
static uint16_t s_service_handle = 0;
static uint16_t s_characteristic_handle = 0;
static uint16_t s_cccd_handle = 0;
static uint8_t s_characteristic_value = 0;
static uint8_t s_cccd_value[2] = {};
static bool s_service_started = false;
static bool s_advertising_data_ready = false;
static bool s_scan_response_ready = false;
static bool s_advertising = false;
static bool s_advertising_start_pending = false;
static QueueHandle_t s_protocol_queue = nullptr;

static esp_ble_adv_params_t s_advertising_params = {};

static RelayConnection* find_connection(uint16_t conn_id) {
    for (size_t i = 0; i < kMaxRelayConnections; ++i) {
        if (s_connections[i].active && s_connections[i].conn_id == conn_id) {
            return &s_connections[i];
        }
    }
    return nullptr;
}

static bool enqueue_protocol_work(ProtocolWorkType type, uint16_t conn_id = 0,
                                  uint16_t length = 0,
                                  uint8_t* data = nullptr) {
    if (s_protocol_queue == nullptr) {
        free(data);
        ESP_LOGE(RELAY_TAG, "Protocol worker is not available");
        return false;
    }

    const ProtocolWorkItem item = {type, conn_id, length, data};
    if (xQueueSend(s_protocol_queue, &item, 0) != pdTRUE) {
        free(data);
        ESP_LOGW(RELAY_TAG, "Protocol work queue is full; dropping work item");
        return false;
    }
    return true;
}

static bool enqueue_packet_for_processing(const uint8_t* data, uint16_t length) {
    uint8_t* packet_copy = static_cast<uint8_t*>(malloc(length));
    if (packet_copy == nullptr) {
        ESP_LOGE(RELAY_TAG, "Could not allocate %u-byte packet copy",
                 static_cast<unsigned>(length));
        return false;
    }
    memcpy(packet_copy, data, length);
    return enqueue_protocol_work(ProtocolWorkType::Packet, 0, length,
                                 packet_copy);
}

static bool get_connection_snapshot(uint16_t conn_id,
                                   RelayConnection* snapshot) {
    bool found = false;
    portENTER_CRITICAL(&s_connections_mux);
    RelayConnection* connection = find_connection(conn_id);
    if (connection != nullptr) {
        *snapshot = *connection;
        found = true;
    }
    portEXIT_CRITICAL(&s_connections_mux);
    return found;
}

static void announce_to_connection(uint16_t conn_id) {
    RelayConnection connection = {};
    if (!get_connection_snapshot(conn_id, &connection) ||
        !connection.active || !connection.subscribed || connection.announced) {
        return;
    }

    uint8_t announcement[256];
    const size_t length =
        bitchat_make_announcement(announcement, sizeof(announcement));
    if (length == 0) {
        ESP_LOGD(RELAY_TAG,
                 "Waiting for a valid clock before announcing to connection %u",
                 conn_id);
        return;
    }
    if (length > connection.mtu - 3) {
        ESP_LOGW(RELAY_TAG,
                 "Cannot announce to connection %u: packet %u bytes exceeds MTU %u",
                 conn_id, static_cast<unsigned>(length),
                 connection.mtu);
        return;
    }

    const esp_err_t err = esp_ble_gatts_send_indicate(
        s_gatts_if, conn_id, s_characteristic_handle,
        static_cast<uint16_t>(length), announcement, false);
    if (err != ESP_OK) {
        ESP_LOGW(RELAY_TAG, "Bitchat announce to connection %u failed: %s",
                 conn_id, esp_err_to_name(err));
        return;
    }

    portENTER_CRITICAL(&s_connections_mux);
    RelayConnection* current = find_connection(conn_id);
    if (current != nullptr && current->subscribed) {
        current->announced = true;
    }
    portEXIT_CRITICAL(&s_connections_mux);
    ESP_LOGI(RELAY_TAG, "Announced ESP32 as a Bitchat peer to connection %u",
             conn_id);
}

static void announce_to_subscribers() {
    RelayConnection connections[kMaxRelayConnections];
    portENTER_CRITICAL(&s_connections_mux);
    for (size_t i = 0; i < kMaxRelayConnections; ++i) {
        connections[i] = s_connections[i];
    }
    portEXIT_CRITICAL(&s_connections_mux);

    for (size_t i = 0; i < kMaxRelayConnections; ++i) {
        if (connections[i].active && connections[i].subscribed &&
            !connections[i].announced) {
            announce_to_connection(connections[i].conn_id);
        }
    }
}

static void protocol_worker_task(void*) {
    ESP_LOGI(RELAY_TAG, "Bitchat protocol worker started");
    ProtocolWorkItem item = {};
    while (true) {
        if (xQueueReceive(s_protocol_queue, &item, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        switch (item.type) {
            case ProtocolWorkType::Packet:
                bitchat_process_packet(item.data, item.length);
                announce_to_subscribers();
                free(item.data);
                break;
            case ProtocolWorkType::AnnounceConnection:
                announce_to_connection(item.conn_id);
                break;
        }
    }
}

static void maybe_start_advertising() {
    if (!s_advertising_data_ready || !s_scan_response_ready ||
        !s_service_started || s_advertising || s_advertising_start_pending) {
        return;
    }

    s_advertising_start_pending = true;
    const esp_err_t err = esp_ble_gap_start_advertising(&s_advertising_params);
    if (err != ESP_OK) {
        s_advertising_start_pending = false;
        ESP_LOGE(RELAY_TAG, "Could not start advertising: %s", esp_err_to_name(err));
    }
}

static uint64_t packet_fingerprint(const uint8_t* data, size_t length) {
    size_t end = length;
    if (length >= 22) {
        const uint8_t version = data[0];
        const size_t header_size = version == 1 ? 14 : (version >= 2 ? 16 : 0);
        if (header_size != 0) {
            const uint8_t flags = data[11];
            size_t offset = header_size + 8;
            uint32_t payload_size = 0;

            if (header_size == 14) {
                payload_size = (static_cast<uint32_t>(data[12]) << 8) | data[13];
            } else {
                payload_size = (static_cast<uint32_t>(data[12]) << 24) |
                    (static_cast<uint32_t>(data[13]) << 16) |
                    (static_cast<uint32_t>(data[14]) << 8) | data[15];
            }

            bool valid = offset <= length;
            if (valid && (flags & 0x01) != 0) {
                valid = length - offset >= 8;
                offset += valid ? 8 : 0;
            }
            if (valid && header_size == 16 && (flags & 0x08) != 0) {
                valid = offset < length;
                if (valid) {
                    const size_t route_size = 1 + static_cast<size_t>(data[offset]) * 8;
                    valid = route_size <= length - offset;
                    offset += valid ? route_size : 0;
                }
            }
            if (valid) {
                valid = payload_size <= length - offset;
                offset += valid ? payload_size : 0;
            }
            if (valid && (flags & 0x02) != 0) {
                valid = length - offset >= 64;
                offset += valid ? 64 : 0;
            }
            if (valid) {
                end = offset;
            }
        }
    }

    // Exclude TTL and transport padding so relayed copies share an identity.
    uint64_t hash = 14695981039346656037ULL;
    for (size_t i = 0; i < end; ++i) {
        if (i == 2) {
            continue;
        }
        hash ^= data[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

static bool packet_was_forwarded(uint64_t fingerprint) {
    for (size_t i = 0; i < kForwardedPacketCacheSize; ++i) {
        if (s_forwarded_packets[i].valid &&
            s_forwarded_packets[i].value == fingerprint) {
            return true;
        }
    }

    s_forwarded_packets[s_next_packet_fingerprint].value = fingerprint;
    s_forwarded_packets[s_next_packet_fingerprint].valid = true;
    s_next_packet_fingerprint =
        (s_next_packet_fingerprint + 1) % kForwardedPacketCacheSize;
    return false;
}

static void forward_packet(uint16_t source_conn_id, uint16_t attribute_handle,
                           uint8_t* data, uint16_t length) {
    enqueue_packet_for_processing(data, length);

    if (length == 0 || packet_was_forwarded(packet_fingerprint(data, length))) {
        return;
    }

    RelayConnection connections[kMaxRelayConnections];
    portENTER_CRITICAL(&s_connections_mux);
    for (size_t i = 0; i < kMaxRelayConnections; ++i) {
        connections[i] = s_connections[i];
    }
    portEXIT_CRITICAL(&s_connections_mux);

    size_t forwarded_count = 0;
    for (size_t i = 0; i < kMaxRelayConnections; ++i) {
        RelayConnection& connection = connections[i];
        if (!connection.active || !connection.subscribed ||
            connection.conn_id == source_conn_id) {
            continue;
        }

        if (length > connection.mtu - 3) {
            ESP_LOGW(RELAY_TAG,
                     "Dropping %u-byte frame for connection %u (MTU %u)",
                     length, connection.conn_id, connection.mtu);
            continue;
        }

        const esp_err_t err = esp_ble_gatts_send_indicate(
            s_gatts_if, connection.conn_id, attribute_handle, length, data, false);
        if (err != ESP_OK) {
            ESP_LOGW(RELAY_TAG, "Forward to connection %u failed: %s",
                     connection.conn_id, esp_err_to_name(err));
        } else {
            ++forwarded_count;
        }
    }
    ESP_LOGI(RELAY_TAG, "Received %u-byte frame from connection %u; forwarded to %u peer(s)",
             length, source_conn_id, static_cast<unsigned>(forwarded_count));
}

static void gatts_callback(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
                           esp_ble_gatts_cb_param_t* param) {
    switch (event) {
        case ESP_GATTS_REG_EVT: {
            if (param->reg.status != ESP_GATT_OK) {
                ESP_LOGE(RELAY_TAG, "GATT app registration failed: %d",
                         param->reg.status);
                return;
            }
            s_gatts_if = gatts_if;

            const esp_err_t name_err =
                esp_ble_gap_set_device_name("Bitchat ESP32 Relay");
            if (name_err != ESP_OK) {
                ESP_LOGE(RELAY_TAG, "Could not set relay name: %s",
                         esp_err_to_name(name_err));
                return;
            }

            esp_ble_adv_data_t advertising_data = {};
            advertising_data.set_scan_rsp = false;
            advertising_data.include_name = false;
            advertising_data.include_txpower = false;
            advertising_data.service_uuid_len = 16;
            advertising_data.p_service_uuid =
                const_cast<uint8_t*>(kActiveServiceUuid);
            advertising_data.flag = ESP_BLE_ADV_FLAG_GEN_DISC |
                                    ESP_BLE_ADV_FLAG_BREDR_NOT_SPT;

            esp_ble_adv_data_t scan_response = {};
            scan_response.set_scan_rsp = true;
            scan_response.include_name = true;
            scan_response.include_txpower = false;

            esp_err_t err = esp_ble_gap_config_adv_data(&advertising_data);
            if (err != ESP_OK) {
                ESP_LOGE(RELAY_TAG, "Advertising data config failed: %s",
                         esp_err_to_name(err));
                return;
            }
            err = esp_ble_gap_config_adv_data(&scan_response);
            if (err != ESP_OK) {
                ESP_LOGE(RELAY_TAG, "Scan response config failed: %s",
                         esp_err_to_name(err));
                return;
            }

            esp_gatt_srvc_id_t service_id = {};
            service_id.is_primary = true;
            service_id.id.inst_id = 0;
            service_id.id.uuid.len = ESP_UUID_LEN_128;
            memcpy(service_id.id.uuid.uuid.uuid128, kActiveServiceUuid, 16);
            err = esp_ble_gatts_create_service(gatts_if, &service_id, 4);
            if (err != ESP_OK) {
                ESP_LOGE(RELAY_TAG, "GATT service creation failed: %s",
                         esp_err_to_name(err));
            }
            break;
        }

        case ESP_GATTS_CREATE_EVT: {
            if (param->create.status != ESP_GATT_OK) {
                ESP_LOGE(RELAY_TAG, "GATT service creation failed: %d",
                         param->create.status);
                return;
            }
            s_service_handle = param->create.service_handle;
            esp_bt_uuid_t characteristic_uuid = {};
            characteristic_uuid.len = ESP_UUID_LEN_128;
            memcpy(characteristic_uuid.uuid.uuid128, kCharacteristicUuid, 16);
            const esp_gatt_char_prop_t properties =
                static_cast<esp_gatt_char_prop_t>(
                    ESP_GATT_CHAR_PROP_BIT_WRITE |
                    ESP_GATT_CHAR_PROP_BIT_WRITE_NR |
                    ESP_GATT_CHAR_PROP_BIT_NOTIFY);
            esp_attr_value_t characteristic_value = {};
            characteristic_value.attr_max_len = ESP_GATT_MAX_ATTR_LEN;
            characteristic_value.attr_len = sizeof(s_characteristic_value);
            characteristic_value.attr_value = &s_characteristic_value;
            const esp_err_t err = esp_ble_gatts_add_char(
                s_service_handle, &characteristic_uuid,
                ESP_GATT_PERM_WRITE, properties, &characteristic_value, nullptr);
            if (err != ESP_OK) {
                ESP_LOGE(RELAY_TAG, "GATT characteristic creation failed: %s",
                         esp_err_to_name(err));
            }
            break;
        }

        case ESP_GATTS_ADD_CHAR_EVT: {
            if (param->add_char.status != ESP_GATT_OK) {
                ESP_LOGE(RELAY_TAG, "GATT characteristic creation failed: %d",
                         param->add_char.status);
                return;
            }
            s_characteristic_handle = param->add_char.attr_handle;
            esp_bt_uuid_t descriptor_uuid = {};
            descriptor_uuid.len = ESP_UUID_LEN_16;
            descriptor_uuid.uuid.uuid16 = kCccdUuid;
            esp_attr_value_t cccd_value = {};
            cccd_value.attr_max_len = sizeof(s_cccd_value);
            cccd_value.attr_len = sizeof(s_cccd_value);
            cccd_value.attr_value = s_cccd_value;
            const esp_err_t err = esp_ble_gatts_add_char_descr(
                s_service_handle, &descriptor_uuid,
                ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, &cccd_value, nullptr);
            if (err != ESP_OK) {
                ESP_LOGE(RELAY_TAG, "CCCD creation failed: %s",
                         esp_err_to_name(err));
            }
            break;
        }

        case ESP_GATTS_ADD_CHAR_DESCR_EVT:
            if (param->add_char_descr.status != ESP_GATT_OK) {
                ESP_LOGE(RELAY_TAG, "CCCD creation failed: %d",
                         param->add_char_descr.status);
                return;
            }
            s_cccd_handle = param->add_char_descr.attr_handle;
            if (s_service_handle != 0) {
                const esp_err_t err = esp_ble_gatts_start_service(s_service_handle);
                if (err != ESP_OK) {
                    ESP_LOGE(RELAY_TAG, "Could not start GATT service: %s",
                             esp_err_to_name(err));
                }
            }
            break;

        case ESP_GATTS_START_EVT:
            if (param->start.status != ESP_GATT_OK) {
                ESP_LOGE(RELAY_TAG, "GATT service start failed: %d",
                         param->start.status);
                return;
            }
            s_service_started = true;
            maybe_start_advertising();
            ESP_LOGI(RELAY_TAG, "GATT relay service started");
            break;

        case ESP_GATTS_CONNECT_EVT: {
            RelayConnection* connection = nullptr;
            const uint16_t conn_id = param->connect.conn_id;
            portENTER_CRITICAL(&s_connections_mux);
            for (size_t i = 0; i < kMaxRelayConnections; ++i) {
                if (!s_connections[i].active) {
                    connection = &s_connections[i];
                    break;
                }
            }
            if (connection == nullptr) {
                portEXIT_CRITICAL(&s_connections_mux);
                ESP_LOGW(RELAY_TAG, "No connection slot for peer %u", conn_id);
                break;
            }
            connection->conn_id = conn_id;
            connection->mtu = 23;
            connection->active = true;
            connection->subscribed = false;
            connection->announced = false;
            portEXIT_CRITICAL(&s_connections_mux);
            ESP_LOGI(RELAY_TAG, "Peer connected (connection %u)",
                     conn_id);
            s_advertising = false;
            s_advertising_start_pending = false;
            maybe_start_advertising();
            break;
        }

        case ESP_GATTS_DISCONNECT_EVT: {
            portENTER_CRITICAL(&s_connections_mux);
            RelayConnection* connection =
                find_connection(param->disconnect.conn_id);
            if (connection != nullptr) {
                connection->active = false;
                connection->subscribed = false;
            }
            portEXIT_CRITICAL(&s_connections_mux);
            s_advertising = false;
            s_advertising_start_pending = false;
            maybe_start_advertising();
            ESP_LOGI(RELAY_TAG, "Peer disconnected (connection %u)",
                     param->disconnect.conn_id);
            break;
        }

        case ESP_GATTS_MTU_EVT: {
            const uint16_t conn_id = param->mtu.conn_id;
            portENTER_CRITICAL(&s_connections_mux);
            RelayConnection* connection = find_connection(conn_id);
            const bool found = connection != nullptr;
            if (connection != nullptr) {
                connection->mtu = param->mtu.mtu;
            }
            portEXIT_CRITICAL(&s_connections_mux);
            if (found) {
                ESP_LOGI(RELAY_TAG, "Connection %u negotiated MTU %u",
                         conn_id, param->mtu.mtu);
                enqueue_protocol_work(ProtocolWorkType::AnnounceConnection,
                                      conn_id);
            }
            break;
        }

        case ESP_GATTS_WRITE_EVT: {
            esp_gatt_status_t status = ESP_GATT_OK;
            if (param->write.is_prep) {
                status = ESP_GATT_REQ_NOT_SUPPORTED;
            } else if (param->write.handle == s_cccd_handle &&
                       param->write.len == 2) {
                bool subscribed = false;
                bool found = false;
                uint16_t conn_id = param->write.conn_id;
                portENTER_CRITICAL(&s_connections_mux);
                RelayConnection* connection = find_connection(param->write.conn_id);
                if (connection != nullptr) {
                    found = true;
                    connection->subscribed =
                        param->write.value[0] == 0x01 &&
                        param->write.value[1] == 0x00;
                    subscribed = connection->subscribed;
                    if (!connection->subscribed) {
                        connection->announced = false;
                    }
                }
                portEXIT_CRITICAL(&s_connections_mux);
                if (found) {
                    ESP_LOGI(RELAY_TAG, "Connection %u notifications %s",
                             conn_id, subscribed ? "enabled" : "disabled");
                    if (subscribed) {
                        enqueue_protocol_work(
                            ProtocolWorkType::AnnounceConnection, conn_id);
                    }
                }
            } else if (param->write.handle == s_characteristic_handle &&
                       param->write.len > 0) {
                forward_packet(param->write.conn_id, param->write.handle,
                               param->write.value, param->write.len);
            } else {
                status = ESP_GATT_INVALID_ATTR_LEN;
            }
            if (param->write.need_rsp) {
                esp_ble_gatts_send_response(gatts_if, param->write.conn_id,
                                            param->write.trans_id, status, nullptr);
            }
            break;
        }

        case ESP_GATTS_EXEC_WRITE_EVT:
            if (param->exec_write.exec_write_flag == ESP_GATT_PREP_WRITE_EXEC) {
                esp_ble_gatts_send_response(gatts_if, param->exec_write.conn_id,
                                            param->exec_write.trans_id,
                                            ESP_GATT_REQ_NOT_SUPPORTED, nullptr);
            } else {
                esp_ble_gatts_send_response(gatts_if, param->exec_write.conn_id,
                                            param->exec_write.trans_id, ESP_GATT_OK,
                                            nullptr);
            }
            break;

        default:
            break;
    }
}

}  // namespace

void gap_callback(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* param) {  
    if (event == ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT) {
        if (param->adv_data_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            s_advertising_data_ready = true;
            maybe_start_advertising();
        } else {
            ESP_LOGE(RELAY_TAG, "Advertising data setup failed: %d",
                     param->adv_data_cmpl.status);
        }
        return;
    }
    if (event == ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT) {
        if (param->scan_rsp_data_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            s_scan_response_ready = true;
            maybe_start_advertising();
        } else {
            ESP_LOGE(RELAY_TAG, "Scan response setup failed: %d",
                     param->scan_rsp_data_cmpl.status);
        }
        return;
    }
    if (event == ESP_GAP_BLE_ADV_START_COMPLETE_EVT) {
        s_advertising_start_pending = false;
        s_advertising = param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS;
        if (s_advertising) {
#if defined(CONFIG_BITCHAT_NETWORK_MAINNET)
            ESP_LOGI(RELAY_TAG, "Bitchat MAINNET relay advertising started");
#else
            ESP_LOGI(RELAY_TAG, "Bitchat TESTNET relay advertising started");
#endif
        } else {
            ESP_LOGE(RELAY_TAG, "Advertising start failed: %d",
                     param->adv_start_cmpl.status);
        }
        return;
    }
    if (event != ESP_GAP_BLE_SCAN_RESULT_EVT) return;  
  
    auto& scan = param->scan_rst;  
      
    // Log every device found  
    // ESP_LOGI(BLE_TAG, "Device: RSSI=%d, adv_len=%d", scan.rssi, scan.adv_data_len);  
  
    if (scan.search_evt == ESP_GAP_SEARCH_INQ_RES_EVT ||   
        scan.search_evt == ESP_GAP_SEARCH_INQ_CMPL_EVT) {  
        parse_advertisement(scan.ble_adv, scan.adv_data_len);  
        if (scan.scan_rsp_len > 0) {
            parse_advertisement(scan.ble_adv + scan.adv_data_len,
                                scan.scan_rsp_len);
        }
    }  
}

const char* bt_controller_status_to_str(esp_bt_controller_status_t status)
{
    switch (status) {
        case ESP_BT_CONTROLLER_STATUS_IDLE:
            return "IDLE";
        case ESP_BT_CONTROLLER_STATUS_INITED:
            return "INITED";
        case ESP_BT_CONTROLLER_STATUS_ENABLED:
            return "ENABLED";
        default:
            return "UNKNOWN";
    }
}

esp_err_t ble_start_scan(uint32_t scan_duration_ms = 0)  
{  
    esp_err_t ret;  
  
    // Register GAP callback  
    ret = esp_ble_gap_register_callback(gap_callback);  
    if (ret != ESP_OK) {  
        ESP_LOGE(BLE_TAG, "GAP callback register failed: %d", ret);  
        return ret;  
    }  

    // Set scan parameters  
    esp_ble_scan_params_t scan_params = {  
        .scan_type = BLE_SCAN_TYPE_ACTIVE,  
        .own_addr_type = BLE_ADDR_TYPE_PUBLIC,  
        .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL, // Keep original  
        .scan_interval = 0x50,  
        .scan_window = 0x30,  
        .scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE  
    };  
    ret = esp_ble_gap_set_scan_params(&scan_params);  
    if (ret != ESP_OK) {  
        ESP_LOGE(BLE_TAG, "Set scan params failed: %d", ret);  
        return ret;  
    }  
  
    // Always start scanning, 0 = continuous  
    ret = esp_ble_gap_start_scanning(scan_duration_ms == 0 ? 0 : scan_duration_ms / 1000);  
    if (ret != ESP_OK) {  
        ESP_LOGE(BLE_TAG, "Start scanning failed: %d", ret);  
        return ret;  
    }  
  
    ESP_LOGI(BLE_TAG, "BLE scan started, duration=%d ms", scan_duration_ms);  
    return ESP_OK;  
}

esp_err_t ble_start_relay() {
    s_advertising_params.adv_int_min = 0x100;
    s_advertising_params.adv_int_max = 0x180;
    s_advertising_params.adv_type = ADV_TYPE_IND;
    s_advertising_params.own_addr_type = BLE_ADDR_TYPE_PUBLIC;
    s_advertising_params.channel_map = ADV_CHNL_ALL;
    s_advertising_params.adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY;

    esp_err_t err = esp_ble_gatt_set_local_mtu(517);
    if (err != ESP_OK) {
        ESP_LOGE(RELAY_TAG, "Could not set local MTU: %s",
                 esp_err_to_name(err));
        return err;
    }

    s_protocol_queue = xQueueCreate(kProtocolQueueLength,
                                    sizeof(ProtocolWorkItem));
    if (s_protocol_queue == nullptr) {
        ESP_LOGE(RELAY_TAG, "Could not create protocol work queue");
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(protocol_worker_task, "bitchat_protocol", 8192, nullptr,
                    5, nullptr) != pdPASS) {
        ESP_LOGE(RELAY_TAG, "Could not create protocol worker task");
        vQueueDelete(s_protocol_queue);
        s_protocol_queue = nullptr;
        return ESP_ERR_NO_MEM;
    }

    err = esp_ble_gatts_register_callback(gatts_callback);
    if (err != ESP_OK) {
        ESP_LOGE(RELAY_TAG, "GATTS callback registration failed: %s",
                 esp_err_to_name(err));
        return err;
    }

    err = esp_ble_gatts_app_register(kRelayAppId);
    if (err != ESP_OK) {
        ESP_LOGE(RELAY_TAG, "GATT app registration failed: %s",
                 esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(RELAY_TAG, "Starting Bitchat BLE relay");
    return ESP_OK;
}


void ble_enable(esp_bt_mode_t mode) {
  esp_err_t esp_bt_enable = esp_bt_controller_enable(mode); 
  
  cout << esp_bt_enable << endl;
}

esp_err_t ble_init() {
  esp_err_t ret;
  esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();

  // Release classic BT memory (we only need BLE)
  ret = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
  if (ret != ESP_OK) {
      ESP_LOGE(ERR_TAG, "Failed to release classic BT memory: %d", ret);
      return ret;
  }

  // Initialize controller
  ret = esp_bt_controller_init(&bt_cfg);
  if (ret != ESP_OK) {
      ESP_LOGE(ERR_TAG, "BT controller init failed: %d", ret);
      return ret;
  }

  // Enable BLE mode
  ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
  if (ret != ESP_OK) {
      ESP_LOGE(ERR_TAG, "BT controller enable failed: %d", ret);
      return ret;
  }

  // Initialize Bluedroid stack
  ret = esp_bluedroid_init();
  if (ret != ESP_OK) {
      ESP_LOGE(ERR_TAG, "Bluedroid init failed: %d", ret);
      return ret;
  }

  // Enable Bluedroid
  ret = esp_bluedroid_enable();
  if (ret != ESP_OK) {
      ESP_LOGE(ERR_TAG, "Bluedroid enable failed: %d", ret);
      return ret;
  }

  ESP_LOGI(ERR_TAG, "Bluetooth initialized successfully!");
  return ESP_OK;
}
