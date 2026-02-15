#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>

// ============================================================================
// WiFi CONNECTION MANAGER
// ============================================================================
// Manages WiFi connection with WiFiManager portal and auto-reconnection

class WiFiConnectionManager {
public:
    WiFiConnectionManager();

    // Initialize and connect
    bool begin();

    // Check connection and handle reconnection
    void tick();

    // Connection status
    bool isConnected() const;
    int getRSSI() const;
    String getSSID() const;
    IPAddress getLocalIP() const;

    // Manual reconnection
    void reconnect();

    // Portal
    void startConfigPortal();

private:
    WiFiManager wifiManager_;
    bool connected_;
    unsigned long lastReconnectAttempt_;

    void handleConnection();
    void handleDisconnection();
};
