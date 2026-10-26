#include "mesh.hpp"
#include "blehandler.hpp"
#include "nvs.hpp"

#include "struct.hpp"

#include <iostream>
#include <thread>
#include <chrono>
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

using namespace std;

extern "C" int app_main() {
  
  ESP_LOGI(MAIN_TAG, "Welcome to Bitchat");
  
  // Non-Volatile storage initialization
  nvs_init();
  
  esp_err_t bleinit = ble_init();
    
  if (bleinit != ESP_OK) {

    ESP_LOGE(BLE_TAG, "Bluetooth low energy initialization failed!, code=%d", bleinit);
    return bleinit;
  }
  uint32_t scan_dur = 0;
  
  esp_err_t ble_start = ble_start_scan(scan_dur);

  if (ble_start != ESP_OK) {

    ESP_LOGE(BLE_TAG, "Bluetooth low energy scan start failed!, code=%d", ble_start);
    return ble_start;
  }

  esp_err_t relay_start = ble_start_relay();
  if (relay_start != ESP_OK) {
    ESP_LOGE(BLE_TAG, "Bluetooth relay start failed, code=%d", relay_start);
    return relay_start;
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
  while (true) {
      vTaskDelay(pdMS_TO_TICKS(1000));
  }
  return 0;
}
