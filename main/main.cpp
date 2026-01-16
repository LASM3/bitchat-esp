#include "mesh.h"
#include "blehandler.h"
#include "nvs.h"
#include "struct.h"

#include <iostream>
#include <thread>
#include <chrono>

using namespace std;

extern "C" int app_main() {
  
  cout << "Hello, world!" << endl;

  static esp_bt_mode_t mode = ESP_BT_MODE_BLE;
  
  // Non-Volatile storage initialization
  nvs_init();
  
  esp_err_t bleinit = ble_init();

  if (bleinit == 0) {
    cout << "Bluetooth Initialized!" << endl;
    
    // Bluetooth enabling
    ble_enable(mode);
  }

  return 0;
}
