#include <stdio.h>
#include <iostream>
#include "esp_bt.h"

extern "C" {
  #include "nvs_flash.h"
}

#include "struct.hpp"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_log.h"

using namespace std;

esp_err_t nvs_init () {
  const esp_err_t err = nvs_flash_init();
  if (err == ESP_OK) {
    ESP_LOGI(NVS_TAG, "Non-Volatile Storage initialized!");
  }
  return err;
}
