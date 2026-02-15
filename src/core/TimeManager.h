#pragma once

#include <Arduino.h>

// ============================================================================
// TIME MANAGER
// ============================================================================
// Manages NTP synchronization and time-related operations

class TimeManager {
public:
    TimeManager();

    // Initialize NTP
    bool begin();

    // Sync time from NTP servers
    bool syncTime();

    // Check if time needs resync
    bool needsResync() const;

    // Get current time
    bool getLocalTime(struct tm& timeinfo);
    void getTimeString(char* buffer, size_t bufferSize, const char* format = "%Y-%m-%d %H:%M:%S");

    // Status
    bool isSynced() const { return timeSynced_; }
    unsigned long getLastSyncTime() const { return lastSyncTime_; }

private:
    bool timeSynced_;
    unsigned long lastSyncTime_;
};
