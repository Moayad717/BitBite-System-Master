#include "TimeManager.h"
#include "LogManager.h"
#include "../config/TimingConfig.h"
#include <time.h>

// ============================================================================
// CONSTRUCTOR
// ============================================================================

TimeManager::TimeManager()
    : timeSynced_(false),
      lastSyncTime_(0) {
}

// ============================================================================
// INITIALIZATION
// ============================================================================

bool TimeManager::begin() {
    LOG_INFO("Initializing TimeManager...");

    // Configure timezone
    configTzTime(TIMEZONE, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);

    // Initial sync
    return syncTime();
}

// ============================================================================
// NTP SYNCHRONIZATION
// ============================================================================

bool TimeManager::syncTime() {
    LOG_INFO("Syncing time from NTP servers...");

    int retries = 0;
    struct tm timeinfo;

    while (!::getLocalTime(&timeinfo) && retries < 10) {
        Serial.print(".");
        delay(500);
        retries++;
    }
    Serial.println();

    if (::getLocalTime(&timeinfo)) {
        timeSynced_ = true;
        lastSyncTime_ = millis();

        char timeStr[32];
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);

        LOG_INFO("Time synced successfully: %s", timeStr);
        return true;
    }

    LOG_ERROR("NTP sync failed after %d retries", retries);
    timeSynced_ = false;
    return false;
}

bool TimeManager::needsResync() const {
    if (!timeSynced_) {
        return true;
    }

    unsigned long timeSinceSync = millis() - lastSyncTime_;
    return timeSinceSync >= TIME_SYNC_INTERVAL;
}

// ============================================================================
// TIME RETRIEVAL
// ============================================================================

bool TimeManager::getLocalTime(struct tm& timeinfo) {
    return ::getLocalTime(&timeinfo);
}

void TimeManager::getTimeString(char* buffer, size_t bufferSize, const char* format) {
    struct tm timeinfo;

    if (::getLocalTime(&timeinfo)) {
        strftime(buffer, bufferSize, format, &timeinfo);
    } else {
        snprintf(buffer, bufferSize, "NOT_SYNCED");
    }
}
