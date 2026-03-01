#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

// ============================================================================
// FIREBASE QUEUE TYPES
// ============================================================================

enum class QueueItemType : uint8_t {
    STATUS,           // setJSON to device status path
    LOG,              // pushJSON to device logs path
    FAULT,            // pushJSON to device faults path
    SYNC_STATUS,      // setJSON to {devicePath}/lastScheduleSync (Core 1 → Core 0)
    SCHEDULE_STATUS   // setJSON to {devicePath}/scheduleStatus   (Core 1 → Core 0)
};

struct FirebaseQueueItem {
    QueueItemType type;
    char jsonData[2048];    // Pre-formatted JSON string (2048 to fit schedule status payloads)
};

// ============================================================================
// DUAL CORE MANAGER
// ============================================================================
// Manages Core 0 network task and inter-core communication queue.

// Queue and mutex handles (defined in DualCoreManager.cpp)
extern QueueHandle_t firebaseQueue;
extern SemaphoreHandle_t logMutex;

// Queue helper — push an item from any core (non-blocking)
bool enqueueFirebaseWrite(QueueItemType type, const char* jsonData);

// Start the network task on Core 0
void startNetworkTask();
