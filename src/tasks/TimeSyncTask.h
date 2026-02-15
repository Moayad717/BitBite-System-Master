#pragma once

#include "Task.h"

class WiFiConnectionManager;
class TimeManager;

// ============================================================================
// TIME SYNC TASK
// ============================================================================
// Periodically resyncs time from NTP when WiFi is connected.

class TimeSyncTask : public Task {
public:
    TimeSyncTask(WiFiConnectionManager* wifi, TimeManager* time, unsigned long intervalMs);
    void execute() override;

private:
    WiFiConnectionManager* wifiManager_;
    TimeManager* timeManager_;
};
