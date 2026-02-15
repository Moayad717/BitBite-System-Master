#include "TimeSyncTask.h"
#include "../connectivity/WiFiConnectionManager.h"
#include "../core/TimeManager.h"
#include "../core/LogManager.h"

// ============================================================================
// CONSTRUCTOR
// ============================================================================

TimeSyncTask::TimeSyncTask(WiFiConnectionManager* wifi, TimeManager* time, unsigned long intervalMs)
    : Task("TimeSync", intervalMs),
      wifiManager_(wifi),
      timeManager_(time) {
}

// ============================================================================
// EXECUTE
// ============================================================================

void TimeSyncTask::execute() {
    if (!wifiManager_ || !timeManager_) {
        return;
    }

    if (wifiManager_->isConnected() && timeManager_->needsResync()) {
        LOG_INFO("Resyncing time from NTP...");
        timeManager_->syncTime();
    }
}
