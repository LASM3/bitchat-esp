#include "mesh.hpp"
#include "blehandler.hpp"
#include "nvs.h"
#include "struct.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include "esp_log.h"

using namespace std;

static const char *TAG = "BITCHAT";

extern "C" int app_main() {
  
  ESP_LOGI(TAG, "Welcome to Bitchat");
  
  static esp_bt_mode_t mode = ESP_BT_MODE_BLE;
  
  // Non-Volatile storage initialization
  nvs_init();
  
  esp_err_t bleinit = ble_init();
    
  if (bleinit != ESP_OK) {

    ESP_LOGE("BITCHAT_BLE", "Bluetooth low energy initialization failed!, code=%d", bleinit);
  }
  uint32_t scan_dur = 10;
  
  esp_err_t ble_start = ble_start_scan(scan_dur);
  
  if (ble_start != ESP_OK) {

    ESP_LOGE("BITCHAT_BLE", "Bluetooth low energy scan start failed!, code=%d", bleinit);
  }


  //if (bleinit == 0) {
  //  ESP_LOGI(TAG, "Bluetooth Initialized!");
    
    // Bluetooth enabling
  //  ble_enable(mode);
  //}

  //while (true) {
    //vTaskDelay(pdMS_TO_TICKS(1000));
    //ESP_LOGI(TAG, "Looping");
  //}
  

  // ScanforPeers();

  return 0;
}
