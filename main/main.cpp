#include <stdio.h>
#include <iostream>
#include "blehandler.h"
#include "esp_err.h"
#include "nvs.h"

using namespace std;
  
extern "C" void app_main(void) {
  cout << "Hello, world!" << endl;
  static esp_bt_mode_t mode = ESP_BT_MODE_BLE;
  
  // Non-Volatile storage initialization
  nvs_init();

  // Bluetooth initialization
  esp_err_t bleinit = ble_init();

  if (bleinit == 0) {
    cout << "Bluetooth Initialized!" << endl;
    
    // Bluetooth enabling
    ble_enable(mode);

  }

  return;
}

