#pragma once

#include <Arduino.h>
#include <esp_task_wdt.h>

// ============================================================================
// WATCHDOG TIMER
// ============================================================================
// Prevents system hangs by automatically resetting if not fed periodically
// Also tracks task heartbeats to detect frozen tasks

class Watchdog {
public:
    // Initialize hardware watchdog
    static void begin(uint32_t timeoutMs = 30000);

    // Feed the watchdog (call regularly to prevent reset)
    static void feed();

    // Disable watchdog (use with caution!)
    static void disable();

    // Task monitoring
    static void registerTask(const char* name, uint32_t maxPeriodMs);
    static void taskHeartbeat(const char* name);
    static void checkTaskHealth();

private:
    static bool initialized_;
    static uint32_t timeoutMs_;

    // Task tracking (up to 10 tasks)
    struct TaskInfo {
        const char* name;
        unsigned long lastHeartbeat;
        uint32_t maxPeriodMs;
        bool active;
    };

    static const size_t MAX_TASKS = 10;
    static TaskInfo tasks_[MAX_TASKS];
    static size_t taskCount_;

    static TaskInfo* findTask(const char* name);
};
