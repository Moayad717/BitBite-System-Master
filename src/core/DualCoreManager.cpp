#include "DualCoreManager.h"
#include "LogManager.h"
#include "../connectivity/WiFiConnectionManager.h"
#include "../connectivity/FirebaseManager.h"
#include "../messaging/StreamManager.h"
#include "../tasks/TaskScheduler.h"
#include "../storage/OfflineQueueManager.h"
#include "../core/DeviceManager.h"

// External references (defined in main.cpp)
extern WiFiConnectionManager wifiManager;
extern FirebaseManager firebaseManager;
extern StreamManager streamManager;
extern TaskScheduler taskScheduler;
extern OfflineQueueManager offlineQueue;
extern DeviceManager deviceManager;

// ============================================================================
// GLOBAL HANDLES
// ============================================================================

QueueHandle_t firebaseQueue = nullptr;
SemaphoreHandle_t logMutex = nullptr;

static TaskHandle_t networkTaskHandle = nullptr;

// ============================================================================
// QUEUE HELPER
// ============================================================================

bool enqueueFirebaseWrite(QueueItemType type, const char* jsonData) {
    if (!firebaseQueue || !jsonData) {
        return false;
    }

    if (strlen(jsonData) >= sizeof(FirebaseQueueItem::jsonData)) {
        LOG_WARN("Firebase queue item truncated (%d bytes > %d limit)",
                 strlen(jsonData), sizeof(FirebaseQueueItem::jsonData) - 1);
    }

    FirebaseQueueItem item;
    item.type = type;
    strncpy(item.jsonData, jsonData, sizeof(item.jsonData) - 1);
    item.jsonData[sizeof(item.jsonData) - 1] = '\0';

    // Non-blocking send — drop if queue is full
    if (xQueueSend(firebaseQueue, &item, 0) != pdTRUE) {
        LOG_WARN("Firebase queue full, dropping %s item",
                 type == QueueItemType::STATUS ? "STATUS" :
                 type == QueueItemType::LOG ? "LOG" : "FAULT");
        return false;
    }

    return true;
}

// ============================================================================
// QUEUE PROCESSING (runs on Core 0)
// ============================================================================

static void processFirebaseQueue() {
    FirebaseQueueItem item;
    int processed = 0;

    // Process up to 3 items per loop iteration to avoid starving other tasks
    while (processed < 3 && xQueueReceive(firebaseQueue, &item, 0) == pdTRUE) {
        if (firebaseManager.isReady()) {
            FirebaseJson json;
            json.setJsonData(item.jsonData);

            bool success = false;

            if (item.type == QueueItemType::STATUS) {
                success = firebaseManager.setJSON(deviceManager.getStatusPath(), &json);
                if (success) {
                    LOG_DEBUG("Queue: Status sent to Firebase");
                }
            } else if (item.type == QueueItemType::SYNC_STATUS) {
                char path[128];
                snprintf(path, sizeof(path), "%s/lastScheduleSync", deviceManager.getDevicePath());
                success = firebaseManager.setJSON(path, &json);
                if (success) {
                    LOG_DEBUG("Queue: Schedule sync status sent to Firebase");
                }
            } else if (item.type == QueueItemType::SCHEDULE_STATUS) {
                char path[128];
                snprintf(path, sizeof(path), "%s/scheduleStatus", deviceManager.getDevicePath());
                success = firebaseManager.setJSON(path, &json);
                if (success) {
                    LOG_INFO("Queue: Schedule status sent to Firebase");
                }
            } else {
                // LOG and FAULT use pushJSON to their respective paths
                char path[128];
                if (item.type == QueueItemType::LOG) {
                    snprintf(path, sizeof(path), "%s/logs", deviceManager.getDevicePath());
                } else {
                    snprintf(path, sizeof(path), "%s/faults", deviceManager.getDevicePath());
                }

                success = firebaseManager.pushJSON(path, &json);
                if (success) {
                    LOG_DEBUG("Queue: %s sent to Firebase",
                             item.type == QueueItemType::LOG ? "Log" : "Fault");
                }
            }

            if (!success) {
                LOG_ERROR("Queue: Firebase write failed: %s", firebaseManager.getLastError().c_str());
                // Only persist logs and faults to offline storage — not operational status items
                if (item.type == QueueItemType::LOG || item.type == QueueItemType::FAULT) {
                    OfflineEntryType offlineType = (item.type == QueueItemType::LOG)
                        ? OfflineEntryType::LOG : OfflineEntryType::FAULT;
                    offlineQueue.enqueue(offlineType, item.jsonData);
                }
            }

            json.clear();
        } else {
            // Firebase not ready — queue logs/faults to offline storage
            if (item.type != QueueItemType::STATUS) {
                OfflineEntryType offlineType = (item.type == QueueItemType::LOG)
                    ? OfflineEntryType::LOG : OfflineEntryType::FAULT;
                offlineQueue.enqueue(offlineType, item.jsonData);
                LOG_DEBUG("Queue: %s queued to offline storage",
                         item.type == QueueItemType::LOG ? "Log" : "Fault");
            }
        }

        processed++;
    }
}

// ============================================================================
// NETWORK TASK (Core 0)
// ============================================================================

static void networkTask(void* param) {
    LOG_INFO("Network task started on Core %d", xPortGetCoreID());

    for (;;) {
        // WiFi maintenance (reconnection)
        wifiManager.tick();

        // Firebase maintenance (reconnection)
        firebaseManager.tick();

        // Process queued Firebase writes from SerialProtocol
        processFirebaseQueue();

        // Stream command deletion queue
        streamManager.tick();

        // Scheduled tasks (heartbeat, time sync, offline flush)
        taskScheduler.tick();

        // Yield to other Core 0 tasks (WiFi stack, etc.)
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// ============================================================================
// STARTUP
// ============================================================================

void startNetworkTask() {
    // Create the inter-core queue (10 items deep)
    firebaseQueue = xQueueCreate(10, sizeof(FirebaseQueueItem));
    if (!firebaseQueue) {
        LOG_ERROR("Failed to create Firebase queue!");
        return;
    }

    // Create the log mutex
    logMutex = xSemaphoreCreateMutex();
    if (!logMutex) {
        LOG_ERROR("Failed to create log mutex!");
        return;
    }

    // Create network task pinned to Core 0
    BaseType_t result = xTaskCreatePinnedToCore(
        networkTask,            // Task function
        "NetworkTask",          // Name
        8192,                   // Stack size (Firebase SSL needs large stack)
        nullptr,                // Parameters
        1,                      // Priority (lower than Arduino loop which is 1, but same is fine)
        &networkTaskHandle,     // Task handle
        0                       // Core 0
    );

    if (result == pdPASS) {
        LOG_INFO("Network task created on Core 0 (stack: 8192 bytes)");
    } else {
        LOG_ERROR("Failed to create network task!");
    }
}
