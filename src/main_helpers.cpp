// Helper functions for main.cpp
#include <Arduino.h>
#include <Firebase_ESP_Client.h>
#include "core/DeviceManager.h"
#include "core/LogManager.h"
#include "connectivity/FirebaseManager.h"
#include "connectivity/WiFiConnectionManager.h"

// External references
extern DeviceManager deviceManager;
extern FirebaseManager firebaseManager;
extern WiFiConnectionManager wifiManager;

// Forward declarations
void sendStatusToFirebase();

// ============================================================================
// DEVICE INITIALIZATION
// ============================================================================

void initializeDevice() {
    LOG_INFO("Initializing device in Firebase...");

    // Check if device already exists
    if (firebaseManager.getShallowData(deviceManager.getDevicePath())) {
        LOG_INFO("Device already registered in Firebase");
    } else {
        LOG_INFO("Creating new device in Firebase...");

        // Create device info
        char timestamp[25];
        deviceManager.getTimestamp(timestamp, sizeof(timestamp));

        FirebaseJson json;
        json.set("deviceId", deviceManager.getDeviceID());
        json.set("createdAt", timestamp);

        if (firebaseManager.setJSON(deviceManager.getDeviceInfoPath(), &json)) {
            LOG_INFO("Device created successfully");
        } else {
            LOG_ERROR("Failed to create device: %s", firebaseManager.getLastError().c_str());
        }

        json.clear();
    }

    // Send initial status
    sendStatusToFirebase();
}

// ============================================================================
// STATUS UPDATE
// ============================================================================

void sendStatusToFirebase() {
    if (!firebaseManager.isReady()) {
        LOG_WARN("Cannot send status - Firebase not ready");
        return;
    }

    char timestamp[25];
    deviceManager.getTimestamp(timestamp, sizeof(timestamp));

    // Note: SerialProtocol automatically sends full status updates (including sensor data)
    // when it receives data from Feeding ESP. This function only sends connectivity heartbeat
    // to update lastSeen timestamp and WiFi signal even when no sensor updates arrive.

    FirebaseJson json;
    json.set("isOnline", true);
    json.set("wifiSignal", wifiManager.getRSSI());
    json.set("lastSeen", timestamp);

    if (firebaseManager.updateJSON(deviceManager.getStatusPath(), &json)) {
        LOG_DEBUG("Heartbeat sent to Firebase");
    } else {
        LOG_ERROR("Failed to send heartbeat: %s", firebaseManager.getLastError().c_str());
    }

    json.clear();
}
