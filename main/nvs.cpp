#include <stdio.h>
#include <iostream>
#include "esp_bt.h"

extern "C" {
  #include "nvs_flash.h"
}

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_log.h"

using namespace std;

static const char *TAG = "NVS";

esp_err_t nvs_init () {
  ESP_LOGI(TAG, "Non-Volatile Storage initialized!");
  return nvs_flash_init();
}

