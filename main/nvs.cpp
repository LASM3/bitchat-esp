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

esp_err_t nvs_init () {
  return nvs_flash_init();
}

