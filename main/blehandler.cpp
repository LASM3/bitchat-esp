#include "blehandler.h"
#include <iostream>
#include <algorithm>
#include <cstdint>

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
}

BLEHandler::BLEHandler() 
    : isScanning(false), isAdvertising(false), meshPacketCallback(nullptr) {
    cout << "[BLE] BLEHandler initialized" << endl;
}

BLEHandler::~BLEHandler() {
    stopScanning();
    stopAdvertising();
}

bool BLEHandler::startScanning() {
    if (isScanning) {
        cout << "[BLE] Already scanning" << endl;
        return true;
    }
    
    isScanning = true;
    cout << "[BLE] Starting BLE scan..." << endl;
    
    // TODO: Implement ESP32 BLE scan
    // Example pseudo-code:
    // BLEDevice::init("MyPeer");
    // BLEScan* pBLEScan = BLEDevice::getScan();
    // pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    // pBLEScan->setActiveScan(true);
    // pBLEScan->start(30);
    
    return true;
}

bool BLEHandler::stopScanning() {
    if (!isScanning) {
        return true;
    }
    
    isScanning = false;
    cout << "[BLE] Stopping BLE scan" << endl;
    
    // TODO: Implement ESP32 BLE scan stop
    // Example: BLEDevice::getScan()->stop();
    
    return true;
}

bool BLEHandler::startAdvertising(const string& peerID, const string& nickname) {
    if (isAdvertising) {
        cout << "[BLE] Already advertising" << endl;
        return true;
    }
    
    isAdvertising = true;
    myAdvertisedPeerID = peerID;
    myAdvertisedNickname = nickname;
    
    cout << "[BLE] Starting advertisement as '" << nickname << "' (" << peerID << ")" << endl;
    
    // TODO: Implement ESP32 BLE advertising
    // Example pseudo-code:
    // BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    // pAdvertising->addServiceUUID(BITCHAT_SERVICE_UUID);
    // pAdvertising->setScanResponse(true);
    // pAdvertising->setMinPreferred(0x06);
    // BLEDevice::startAdvertising();
    
    // TODO: Add packet callback to GATT server
    // Create service with UUID and characteristics for receiving packets
    
    return true;
}

bool BLEHandler::stopAdvertising() {
    if (!isAdvertising) {
        return true;
    }
    
    isAdvertising = false;
    cout << "[BLE] Stopping advertisement" << endl;
    
    // TODO: Implement ESP32 BLE advertising stop
    // Example: BLEDevice::stopAdvertising();
    
    return true;
}

bool BLEHandler::connectToPeer(const string& peerID) {
    cout << "[BLE] Initiating connection to peer: " << peerID << endl;
    
    // TODO: Implement ESP32 BLE connection
    // Example pseudo-code:
    // BLEAddress peerAddress(peerID);
    // BLEClient* pClient = BLEDevice::createClient();
    // pClient->setClientCallbacks(new MyClientCallback());
    // if (!pClient->connect(peerAddress)) {
    //     cout << "[ERROR] Failed to connect to peer" << endl;
    //     return false;
    // }
    
    // TODO: Discover services and characteristics
    // Get the characteristic for receiving packets
    
    // Store connection in activePeers map
    activePeers[peerID] = nullptr; // Store actual BLEClient pointer here
    
    cout << "[BLE] Connected to peer: " << peerID << endl;
    return true;
}

bool BLEHandler::disconnectFromPeer(const string& peerID) {
    cout << "[BLE] Disconnecting from peer: " << peerID << endl;
    
    auto it = activePeers.find(peerID);
    if (it == activePeers.end()) {
        cout << "[WARN] Peer not in active connections: " << peerID << endl;
        return false;
    }
    
    // TODO: Implement ESP32 BLE disconnection
    // Example: if (pClient) pClient->disconnect();
    
    activePeers.erase(it);
    cout << "[BLE] Disconnected from peer: " << peerID << endl;
    return true;
}

bool BLEHandler::sendPacket(const string& peerID, const vector<uint8_t>& data) {
    auto it = activePeers.find(peerID);
    if (it == activePeers.end()) {
        cout << "[ERROR] Peer not connected: " << peerID << endl;
        return false;
    }
    
    cout << "[BLE] Sending " << data.size() << " bytes to " << peerID << endl;
    
    // TODO: Implement ESP32 BLE packet send
    // Handle fragmentation for packets > MTU (typically 20-512 bytes)
    // 
    // Example pseudo-code:
    // BLERemoteCharacteristic* pRemoteCharacteristic = /* get from peer */;
    // if (data.size() <= MTU) {
    //     pRemoteCharacteristic->writeValue(data);
    // } else {
    //     // Fragment and send in chunks
    //     for (size_t i = 0; i < data.size(); i += MTU) {
    //         size_t chunkSize = min(MTU, data.size() - i);
    //         vector<uint8_t> chunk(data.begin() + i, data.begin() + i + chunkSize);
    //         pRemoteCharacteristic->writeValue(chunk);
    //     }
    // }
    
    return true;
}

void BLEHandler::onPacketReceived(const vector<uint8_t>& data, const string& fromPeerID) {
    // Called by the BLE stack when a packet arrives from a peer
    if (data.size() < 13) {
        cout << "[WARN] Received invalid packet size from " << fromPeerID << endl;
        return;
    }
    
    cout << "[BLE] Received " << data.size() << " bytes from " << fromPeerID << endl;
    
    if (meshPacketCallback) {
        meshPacketCallback(data, fromPeerID);
    } else {
        cout << "[WARN] No mesh callback registered" << endl;
    }
}

void BLEHandler::setMeshCallback(function<void(const vector<uint8_t>&, const string&)> callback) {
    meshPacketCallback = callback;
    cout << "[BLE] Mesh packet callback registered" << endl;
}

vector<string> BLEHandler::getDiscoveredPeers() {
    return discoveredPeers;
}

void BLEHandler::onPeerDiscovered(const string& peerID, const string& nickname) {
    // Called when a new peer is discovered during scanning
    cout << "[BLE] Discovered peer: " << nickname << " (" << peerID << ")" << endl;
    
    auto it = find(discoveredPeers.begin(), discoveredPeers.end(), peerID);
    if (it == discoveredPeers.end()) {
        discoveredPeers.push_back(peerID);
    }
}

void BLEHandler::onPeerConnected(const string& peerID) {
    // Called when a peer connects to our device (as a peripheral)
    cout << "[BLE] Peer connected: " << peerID << endl;
}

void BLEHandler::onPeerDisconnected(const string& peerID) {
    // Called when a peer disconnects
    cout << "[BLE] Peer disconnected: " << peerID << endl;
}

int BLEHandler::getActivePeerCount() const {
    return activePeers.size();
}

bool BLEHandler::isPeerConnected(const string& peerID) const {
    return activePeers.find(peerID) != activePeers.end();
}
