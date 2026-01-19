#include <vector>
#include <string>
#include <map>
#include <functional>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include "esp_bt.h"

esp_err_t ble_init();

void ble_enable(esp_bt_mode_t mode);

esp_err_t ble_start_scan(uint32_t scan_duration_ms);

