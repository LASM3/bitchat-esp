#include <iostream>
#include <algorithm>
#include <cstdint>

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

void parse_advertisement(uint8_t* adv, uint8_t adv_len) {  
  // Log raw advertisement data for debugging  
  // ESP_LOGI(TAG, "ADVERTISEMENT");  
  // ESP_LOG_BUFFER_HEX(MESH_TAG, adv, adv_len);  
    
  uint8_t* ptr = adv;  
  uint8_t* end = adv + adv_len;  

  while (ptr < end) {  
    uint8_t len = *ptr++;  
    if (len == 0) break;  
    uint8_t type = *ptr++;  
    uint8_t* payload = ptr;  
    ptr += len - 1;  

    // ESP_LOGI(TAG, "AD type=0x%02X, len=%d", type, len-1);  

    // Check complete 128-bit UUID (type 0x07)  
    if (type == 0x07 && len == 17) {  
      if (memcmp(payload, TESTNET_UUID_LE, 16) == 0) {  
        ESP_LOGI(MESH_TAG, "✓ BitChat TESTNET device detected!");  
        return;  
      }  
      if (memcmp(payload, MAINNET_UUID_LE, 16) == 0) {  
        ESP_LOGI(MESH_TAG, "✓ BitChat MAINNET device detected!");  
        return;  
      }  
    }  
  }  
}  

