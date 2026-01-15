#pragma once
#include "esp_bt.h"

esp_err_t ble_init();
void ble_enable(esp_bt_mode_t mode); 
