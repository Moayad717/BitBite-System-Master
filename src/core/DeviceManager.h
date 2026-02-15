#pragma once

#include <Arduino.h>
#include <WiFi.h>

// ============================================================================
// DEVICE MANAGER
// ============================================================================
// Manages device identification and Firebase path construction

class DeviceManager {
public:
    DeviceManager();

    // Initialize device ID and paths
    bool begin();

    // Device ID
    const char* getDeviceID() const { return deviceId_; }

    // Firebase paths
    const char* getDevicePath() const { return devicePath_; }
    const char* getDeviceInfoPath() const { return deviceInfoPath_; }
    const char* getDisplayNamePath() const { return displayNamePath_; }
    const char* getStatusPath() const { return statusPath_; }
    const char* getCommandsPath() const { return commandsPath_; }
    const char* getSchedulePath() const { return schedulePath_; }
    const char* getHistoryPath() const { return historyPath_; }
    const char* getFaultsPath() const { return faultsPath_; }

    // Timestamp utilities
    void getTimestamp(char* buffer, size_t bufferSize);
    bool hasValidTime() const;

private:
    // Device identification
    char deviceId_[33];

    // Firebase paths
    char devicePath_[60];
    char deviceInfoPath_[70];
    char displayNamePath_[80];
    char statusPath_[70];
    char commandsPath_[70];
    char schedulePath_[70];
    char historyPath_[70];
    char faultsPath_[80];

    // Helper methods
    bool generateDeviceID();
    void setupPaths();
};
