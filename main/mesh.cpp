#include <iostream>
#include <algorithm>
#include <cstdint>
#include <cstring>

#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include "struct.hpp"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


using namespace std;

int scan_interval = 1;

namespace {

bool s_mainnet_detected = false;
bool s_testnet_detected = false;

}

void parse_advertisement(uint8_t* adv, uint8_t adv_len) {
  if (adv == nullptr) {
    return;
  }

  uint8_t* ptr = adv;
  uint8_t* end = adv + adv_len;

  while (ptr < end) {
    uint8_t len = *ptr++;
    if (len == 0) break;
    if (len > static_cast<size_t>(end - ptr) || len < 1) break;
    uint8_t type = *ptr++;
    uint8_t* payload = ptr;
    ptr += len - 1;

    // ESP_LOGI(TAG, "AD type=0x%02X, len=%d", type, len-1);  

    // Service UUID lists may be marked incomplete (0x06) or complete (0x07).
    if ((type == 0x06 || type == 0x07) && (len - 1) % 16 == 0) {
      for (size_t offset = 0; offset < len - 1; offset += 16) {
        const uint8_t* uuid = payload + offset;
        if (!s_mainnet_detected &&
            std::memcmp(uuid, MAINNET_UUID_LE, 16) == 0) {
          s_mainnet_detected = true;
          ESP_LOGI(MESH_TAG, "Bitchat MAINNET detected");
        }
        if (!s_testnet_detected &&
            std::memcmp(uuid, TESTNET_UUID_LE, 16) == 0) {
          s_testnet_detected = true;
          ESP_LOGI(MESH_TAG, "Bitchat TESTNET detected");
        }
      }
    }
  }
}
