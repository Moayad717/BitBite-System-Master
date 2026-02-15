#include "DeviceManager.h"
#include "LogManager.h"
#include <time.h>

// ============================================================================
// CONSTRUCTOR
// ============================================================================

DeviceManager::DeviceManager() {
    deviceId_[0] = '\0';
    devicePath_[0] = '\0';
    deviceInfoPath_[0] = '\0';
    displayNamePath_[0] = '\0';
    statusPath_[0] = '\0';
    commandsPath_[0] = '\0';
    schedulePath_[0] = '\0';
    historyPath_[0] = '\0';
    faultsPath_[0] = '\0';
}

// ============================================================================
// INITIALIZATION
// ============================================================================

bool DeviceManager::begin() {
    LOG_INFO("Initializing DeviceManager...");

    if (!generateDeviceID()) {
        LOG_ERROR("Failed to generate device ID");
        return false;
    }

    setupPaths();

    LOG_INFO("Device ID: %s", deviceId_);
    LOG_DEBUG("Device path: %s", devicePath_);

    return true;
}

// ============================================================================
// DEVICE ID GENERATION
// ============================================================================

bool DeviceManager::generateDeviceID() {
    // If already generated, skip
    if (deviceId_[0] != '\0') {
        return true;
    }

    // Get MAC address
    uint8_t mac[6];
    WiFi.macAddress(mac);

    // Convert MAC to hex string
    char macStr[13] = {0};
    for (int i = 0; i < 6; i++) {
        sprintf(&macStr[i * 2], "%02X", mac[i]);
    }

    // Generate deterministic random suffix from MAC
    unsigned long seed = 0;
    for (int i = 0; i < 12; i++) {
        seed = seed * 31 + macStr[i];
    }

    randomSeed(seed);
    char randomSuffix[7] = {0};
    for (int i = 0; i < 6; i++) {
        sprintf(&randomSuffix[i], "%x", random(0, 15));
    }

    // Format: ESP_AABBCCDDEEFF_a1b2c3
    if (snprintf(deviceId_, sizeof(deviceId_), "ESP_%s_%s", macStr, randomSuffix) >= sizeof(deviceId_)) {
        LOG_ERROR("Device ID buffer overflow");
        return false;
    }

    return true;
}

// ============================================================================
// PATH SETUP
// ============================================================================

void DeviceManager::setupPaths() {
    snprintf(devicePath_, sizeof(devicePath_), "/devices/%s", deviceId_);
    snprintf(deviceInfoPath_, sizeof(deviceInfoPath_), "%s/info", devicePath_);
    snprintf(displayNamePath_, sizeof(displayNamePath_), "%s/displayName", deviceInfoPath_);
    snprintf(statusPath_, sizeof(statusPath_), "%s/status", devicePath_);
    snprintf(commandsPath_, sizeof(commandsPath_), "%s/commands", devicePath_);
    snprintf(schedulePath_, sizeof(schedulePath_), "%s/schedules", devicePath_);
    snprintf(historyPath_, sizeof(historyPath_), "%s/history", devicePath_);
    snprintf(faultsPath_, sizeof(faultsPath_), "%s/faults", devicePath_);
}

// ============================================================================
// TIMESTAMP UTILITIES
// ============================================================================

void DeviceManager::getTimestamp(char* buffer, size_t bufferSize) {
    struct tm timeinfo;

    if (getLocalTime(&timeinfo)) {
        strftime(buffer, bufferSize, "%Y-%m-%dT%H:%M:%S", &timeinfo);
        return;
    }

    // Fallback to millis if time not synced
    snprintf(buffer, bufferSize, "uptime_%lu", millis());
}

bool DeviceManager::hasValidTime() const {
    struct tm timeinfo;
    return getLocalTime(&timeinfo);
}
