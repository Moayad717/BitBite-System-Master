#include "Watchdog.h"
#include "../core/LogManager.h"

// ============================================================================
// STATIC MEMBERS
// ============================================================================

bool Watchdog::initialized_ = false;
uint32_t Watchdog::timeoutMs_ = 30000;
Watchdog::TaskInfo Watchdog::tasks_[MAX_TASKS];
size_t Watchdog::taskCount_ = 0;

// ============================================================================
// INITIALIZATION
// ============================================================================

void Watchdog::begin(uint32_t timeoutMs) {
    if (initialized_) {
        LOG_WARN("Watchdog already initialized");
        return;
    }

    timeoutMs_ = timeoutMs;

    // Initialize ESP32 task watchdog
    esp_task_wdt_init(timeoutMs / 1000, true);  // timeout in seconds, panic on timeout
    esp_task_wdt_add(NULL);  // Add current task (usually loop task)

    initialized_ = true;

    LOG_INFO("Watchdog initialized with %u ms timeout", timeoutMs);
}

// ============================================================================
// WATCHDOG FEEDING
// ============================================================================

void Watchdog::feed() {
    if (!initialized_) {
        return;
    }

    esp_task_wdt_reset();
}

void Watchdog::disable() {
    if (!initialized_) {
        return;
    }

    esp_task_wdt_delete(NULL);
    esp_task_wdt_deinit();

    initialized_ = false;

    LOG_WARN("Watchdog disabled");
}

// ============================================================================
// TASK MONITORING
// ============================================================================

void Watchdog::registerTask(const char* name, uint32_t maxPeriodMs) {
    if (taskCount_ >= MAX_TASKS) {
        LOG_ERROR("Cannot register task '%s' - limit reached (%d)", name, MAX_TASKS);
        return;
    }

    TaskInfo& task = tasks_[taskCount_];
    task.name = name;
    task.lastHeartbeat = millis();
    task.maxPeriodMs = maxPeriodMs;
    task.active = true;

    taskCount_++;

    LOG_INFO("Task '%s' registered (max period: %u ms)", name, maxPeriodMs);
}

void Watchdog::taskHeartbeat(const char* name) {
    TaskInfo* task = findTask(name);

    if (task == nullptr) {
        // Auto-register task with default 60s timeout
        LOG_WARN("Task '%s' not registered, auto-registering with 60s timeout", name);
        registerTask(name, 60000);
        task = findTask(name);
    }

    if (task != nullptr) {
        task->lastHeartbeat = millis();
    }
}

void Watchdog::checkTaskHealth() {
    if (taskCount_ == 0) {
        return;
    }

    unsigned long now = millis();
    bool foundFrozen = false;

    for (size_t i = 0; i < taskCount_; i++) {
        TaskInfo& task = tasks_[i];

        if (!task.active) {
            continue;
        }

        unsigned long timeSinceHeartbeat = now - task.lastHeartbeat;

        if (timeSinceHeartbeat > task.maxPeriodMs) {
            LOG_ERROR("Task '%s' FROZEN (no heartbeat for %lu ms, max %u ms)",
                     task.name,
                     timeSinceHeartbeat,
                     task.maxPeriodMs);
            foundFrozen = true;
        }
    }

    if (foundFrozen) {
        LOG_CRITICAL("One or more tasks frozen - system may be unstable");

        // Print all task statuses
        Serial.println("\n=== Task Status ===");
        for (size_t i = 0; i < taskCount_; i++) {
            const TaskInfo& task = tasks_[i];
            unsigned long timeSince = now - task.lastHeartbeat;

            Serial.printf("%-20s: Last heartbeat %lu ms ago (max %u ms) %s\n",
                         task.name,
                         timeSince,
                         task.maxPeriodMs,
                         timeSince > task.maxPeriodMs ? "[FROZEN]" : "[OK]");
        }
        Serial.println("===================\n");
    }
}

// ============================================================================
// HELPER METHODS
// ============================================================================

Watchdog::TaskInfo* Watchdog::findTask(const char* name) {
    for (size_t i = 0; i < taskCount_; i++) {
        if (strcmp(tasks_[i].name, name) == 0) {
            return &tasks_[i];
        }
    }
    return nullptr;
}
