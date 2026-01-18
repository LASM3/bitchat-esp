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

using namespace std;
static const char* TAG = "BITCHAT_BLE";

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


void gap_callback(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    if (event == ESP_GAP_BLE_SCAN_RESULT_EVT) {
      esp_ble_gap_cb_param_t *p = param;
auto &scan = p->scan_rst; 
        ESP_LOGI(TAG, "Device found: %02X:%02X:%02X:%02X:%02X:%02X RSSI=%d",
                 scan.bda[0], scan.bda[1], scan.bda[2],
                 scan.bda[3], scan.bda[4], scan.bda[5],
                 scan.rssi);

        // Access raw advertisement data
        uint8_t *adv_data = scan.ble_adv;
        uint8_t adv_len  = scan.adv_data_len;

        // Parse AD elements to detect BitChat UUID (example parsing)
        uint8_t *ptr = adv_data;
        while (ptr < adv_data + adv_len) {
            uint8_t len = *ptr++;
            if (len == 0) break;
            uint8_t type = *ptr++;
            uint8_t *data = ptr;
            ptr += len - 1;

            // check for 128-bit Service UUID (Bitchat detection)
            if (type == ESP_BLE_AD_TYPE_SOL_SRV_UUID) {
      
                ESP_LOGI(TAG, "Found 128-bit Service UUID (possible BitChat packet)");
                // we will compare it with the bitchat UUID here
            }
        }
    }
}

esp_err_t ble_start_scan(uint32_t scan_duration_ms = 0)
{
    esp_err_t ret;

    // Register GAP callback
    ret = esp_ble_gap_register_callback(gap_callback);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GAP callback register failed: %d", ret);
        return ret;
    }

    // Set scan parameters
    esp_ble_scan_params_t scan_params = {
        .scan_type = BLE_SCAN_TYPE_ACTIVE,
        .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
        .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
        .scan_interval = 0x50,
        .scan_window = 0x30,
        .scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE
    };

    ret = esp_ble_gap_set_scan_params(&scan_params);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Set scan params failed: %d", ret);
        return ret;
    }

    // Optionally start scanning immediately for a duration (0 = continuous)
    if (scan_duration_ms > 0) {
        ret = esp_ble_gap_start_scanning(scan_duration_ms / 1000); // converts ms to seconds
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Start scanning failed: %d", ret);
            return ret;
        }
    }

    ESP_LOGI(TAG, "BLE scan initialized successfully");
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
      ESP_LOGE(TAG, "Failed to release classic BT memory: %d", ret);
      return ret;
  }

  // Initialize controller
  ret = esp_bt_controller_init(&bt_cfg);
  if (ret != ESP_OK) {
      ESP_LOGE(TAG, "BT controller init failed: %d", ret);
      return ret;
  }

  // Enable BLE mode
  ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
  if (ret != ESP_OK) {
      ESP_LOGE(TAG, "BT controller enable failed: %d", ret);
      return ret;
  }

  // Initialize Bluedroid stack
  ret = esp_bluedroid_init();
  if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Bluedroid init failed: %d", ret);
      return ret;
  }

  // Enable Bluedroid
  ret = esp_bluedroid_enable();
  if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Bluedroid enable failed: %d", ret);
      return ret;
  }

  ESP_LOGI(TAG, "Bluetooth initialized successfully!");
  return ESP_OK;
}


void get_ble_adverising () {
  return;
}
