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
    FAULT_SET,        // setJSON to {faultsPath}/{key} — active fault (fixed key, no duplicates)
    FAULT_DELETE,     // deleteNode {faultsPath}/{key} — fault resolved
    SYNC_STATUS,      // setJSON to {devicePath}/lastScheduleSync (Core 1 → Core 0)
    SCHEDULE_STATUS,  // setJSON to {devicePath}/scheduleStatus   (Core 1 → Core 0)
    SCHEDULE_EXECUTED // setBool true at {devicePath}/schedules/{key}/executedToday
};

struct FirebaseQueueItem {
    QueueItemType type;
    char jsonData[2048];    // Pre-formatted JSON string (2048 to fit schedule status payloads)
    char key[32];           // Fault key (e.g. "DHT_ERROR") — only used by FAULT_SET/FAULT_DELETE
};

// ============================================================================
// DUAL CORE MANAGER
// ============================================================================
// Manages Core 0 network task and inter-core communication queue.

// Queue and mutex handles (defined in DualCoreManager.cpp)
extern QueueHandle_t firebaseQueue;
extern QueueHandle_t serial2CmdQueue;
extern SemaphoreHandle_t logMutex;

// Queue helpers — push items from any core (non-blocking)
bool enqueueFirebaseWrite(QueueItemType type, const char* jsonData, const char* key = nullptr);
bool enqueueSerial2Cmd(const char* cmd);

// Start the network task on Core 0
void startNetworkTask();
