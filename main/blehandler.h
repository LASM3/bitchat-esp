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

using namespace std;

class BLEHandler {
private:
    vector<string> discoveredPeers;          // List of discovered peer IDs
    map<string, void*> activePeers;          // Map of peer ID to BLE connection handle
    string myAdvertisedPeerID;               // Current advertised peer ID
    string myAdvertisedNickname;             // Current advertised nickname
    bool isScanning;
    bool isAdvertising;
    
    // Callback for mesh packets
    function<void(const vector<uint8_t>&, const string&)> meshPacketCallback;
    
public:
    BLEHandler();
    ~BLEHandler();

    
    // Scanning and Discovery
    
    /// Start scanning for nearby BLE devices (BitChat peers)
    bool startScanning();
    
    /// Stop scanning for devices
    bool stopScanning();
    
    // Advertising
    
    /// Start advertising this peer on the network
    /// @param peerID Unique identifier for this peer
    /// @param nickname Human-readable name for this peer
    bool startAdvertising(const string& peerID, const string& nickname);
    
    /// Stop advertising this peer
    bool stopAdvertising();
    
    // Connection Management
    
    /// Connect to a discovered peer
    /// @param peerID The ID of the peer to connect to
    bool connectToPeer(const string& peerID);
    
    /// Disconnect from a connected peer
    /// @param peerID The ID of the peer to disconnect from
    bool disconnectFromPeer(const string& peerID);
    
    /// Check if a peer is currently connected
    bool isPeerConnected(const string& peerID) const;
    
    /// Get number of active peer connections
    int getActivePeerCount() const;
    
    // Packet Transmission
    
    /// Send a packet to a connected peer
    /// @param peerID The ID of the peer to send to
    /// @param data The packet data to send
    bool sendPacket(const string& peerID, const vector<uint8_t>& data);
    
    // Peer Discovery Information
    
    /// Get list of discovered peers
    vector<string> getDiscoveredPeers();
    void setMeshCallback(function<void(const vector<uint8_t>&, const string&)> callback);
        
    /// Called by BLE stack when a packet is received from a peer
    void onPacketReceived(const vector<uint8_t>& data, const string& fromPeerID);
    
    /// Called when a new peer is discovered during scan
    void onPeerDiscovered(const string& peerID, const string& nickname);
    
    /// Called when a peer successfully connects to us
    void onPeerConnected(const string& peerID);
    
    /// Called when a peer disconnects from us
    void onPeerDisconnected(const string& peerID);
};
