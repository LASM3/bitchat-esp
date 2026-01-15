#include <stdio.h>
#include <iostream>

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_log.h"

using namespace std;

const char* bt_controller_status_to_str(esp_bt_controller_status_t status)
{
    switch (status) {
        case ESP_BT_CONTROLLER_STATUS_IDLE:
            return "IDLE";
        case ESP_BT_CONTROLLER_STATUS_INITED:
            return "INITED";
        case ESP_BT_CONTROLLER_STATUS_ENABLED:
            return "ENABLED";
        default:
            return "UNKNOWN";
    }
}


void ble_enable(esp_bt_mode_t mode) {
  esp_err_t esp_bt_enable = esp_bt_controller_enable(mode); 
  
  cout << esp_bt_enable << endl;
}

esp_err_t ble_init() {
  esp_err_t init_bl;

  esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  
  init_bl = esp_bt_controller_init(&bt_cfg);
  
  return init_bl;
//  if (init_bl == 0) {
//    cout << "Bluetooth initialized!" << endl;
//    static esp_bt_mode_t mode = ESP_BT_MODE_BLE;
//
//    esp_bt_controller_status_t status = esp_bt_controller_get_status();

//    cout << bt_controller_status_to_str(status) << endl;
//}
//  else {
//      cout << "Bluetooth failed initializing!" << endl;
  //  }
}
