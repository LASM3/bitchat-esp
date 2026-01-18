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

void ScanforPeers() {
  
  Packet found;
  
  static const char *TAG = "BITCHAT";

  ESP_LOGI(TAG, "Bitchat is running!");

  // cout << found.type << endl;

  while (found.version == 1) {
  
    ESP_LOGI(TAG, "Scanning...");
    
    vTaskDelay(pdMS_TO_TICKS(scan_interval * 1000)); 

  }
  
  return;
}
