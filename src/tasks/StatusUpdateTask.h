#pragma once

#include "Task.h"

class FirebaseManager;
class DeviceManager;
class WiFiConnectionManager;

// ============================================================================
// STATUS UPDATE TASK
// ============================================================================
// Periodically sends heartbeat status to Firebase.

class StatusUpdateTask : public Task {
public:
    StatusUpdateTask(FirebaseManager* fb, DeviceManager* dev,
                     WiFiConnectionManager* wifi, unsigned long intervalMs);
    void execute() override;

private:
    FirebaseManager* firebaseManager_;
    DeviceManager* deviceManager_;
    WiFiConnectionManager* wifiManager_;
};
