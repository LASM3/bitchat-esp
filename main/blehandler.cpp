#include "blehandler.hpp"
#include <algorithm>
#include <cstdint>

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_log.h"

#include "esp_gap_ble_api.h"
#include "esp_bt_defs.h"

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "struct.hpp"
#include "mesh.hpp"

#include <stdint.h>

using namespace std;

// static const char* TAG = "BITCHAT_BLE";

map<uint64_t, PeerInfo> peers;

void gap_callback(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* param) {  
    if (event != ESP_GAP_BLE_SCAN_RESULT_EVT) return;  
  
    auto& scan = param->scan_rst;  
      
    // Log every device found  
    // ESP_LOGI(BLE_TAG, "Device: RSSI=%d, adv_len=%d", scan.rssi, scan.adv_data_len);  
  
    if (scan.search_evt == ESP_GAP_SEARCH_INQ_RES_EVT ||   
        scan.search_evt == ESP_GAP_SEARCH_INQ_CMPL_EVT) {  
        parse_advertisement(scan.ble_adv, scan.adv_data_len);  
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
