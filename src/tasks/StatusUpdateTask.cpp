#include "StatusUpdateTask.h"
#include "../connectivity/FirebaseManager.h"
#include "../connectivity/WiFiConnectionManager.h"
#include "../core/DeviceManager.h"
#include "../core/LogManager.h"
#include <Firebase_ESP_Client.h>

// ============================================================================
// CONSTRUCTOR
// ============================================================================

StatusUpdateTask::StatusUpdateTask(FirebaseManager* fb, DeviceManager* dev,
                                   WiFiConnectionManager* wifi, unsigned long intervalMs)
    : Task("StatusUpdate", intervalMs),
      firebaseManager_(fb),
      deviceManager_(dev),
      wifiManager_(wifi) {
}

// ============================================================================
// EXECUTE
// ============================================================================

void StatusUpdateTask::execute() {
    if (!firebaseManager_ || !deviceManager_ || !wifiManager_) {
        return;
    }

    if (!firebaseManager_->isReady()) {
        return;
    }

    char timestamp[25];
    deviceManager_->getTimestamp(timestamp, sizeof(timestamp));

    FirebaseJson json;
    json.set("isOnline", true);
    json.set("wifiSignal", wifiManager_->getRSSI());
    json.set("lastSeen", timestamp);

    if (firebaseManager_->updateJSON(deviceManager_->getStatusPath(), &json)) {
        LOG_DEBUG("Heartbeat sent to Firebase");
    } else {
        LOG_ERROR("Failed to send heartbeat: %s", firebaseManager_->getLastError().c_str());
    }

    json.clear();
}
