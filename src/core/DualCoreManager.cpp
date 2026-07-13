#include "DualCoreManager.h"
#include "LogManager.h"
#include "../utils/Watchdog.h"
#include "../connectivity/WiFiConnectionManager.h"
#include "../connectivity/FirebaseManager.h"
#include "../messaging/StreamManager.h"
#include "../tasks/TaskScheduler.h"
#include "../storage/OfflineQueueManager.h"
#include "../core/DeviceManager.h"
#include "../ota/OTAManager.h"
#include "../ota/SerialOTAForwarder.h"

// External references (defined in main.cpp)
extern WiFiConnectionManager wifiManager;
extern FirebaseManager firebaseManager;
extern StreamManager streamManager;
extern TaskScheduler taskScheduler;
extern OfflineQueueManager offlineQueue;
extern DeviceManager deviceManager;
extern OTAManager otaManager;
extern SerialOTAForwarder serialOTAForwarder;

// ============================================================================
// GLOBAL HANDLES
// ============================================================================

QueueHandle_t firebaseQueue   = nullptr;
QueueHandle_t serial2CmdQueue = nullptr;
SemaphoreHandle_t logMutex    = nullptr;

static TaskHandle_t networkTaskHandle = nullptr;

// ============================================================================
// QUEUE HELPERS
// ============================================================================

bool enqueueSerial2Cmd(const char* cmd) {
    if (!serial2CmdQueue || !cmd) return false;
    char buf[64];
    strncpy(buf, cmd, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    if (xQueueSend(serial2CmdQueue, buf, 0) != pdTRUE) {
        LOG_WARN("Serial2 cmd queue full, dropping: %s", cmd);
        return false;
    }
    return true;
}

static const char* queueItemTypeName(QueueItemType type) {
    switch (type) {
        case QueueItemType::STATUS:          return "STATUS";
        case QueueItemType::LOG:             return "LOG";
        case QueueItemType::FAULT_SET:       return "FAULT_SET";
        case QueueItemType::FAULT_DELETE:    return "FAULT_DELETE";
        case QueueItemType::SYNC_STATUS:     return "SYNC_STATUS";
        case QueueItemType::SCHEDULE_STATUS: return "SCHEDULE_STATUS";
        case QueueItemType::SCHEDULE_EXECUTED: return "SCHEDULE_EXECUTED";
        default:                             return "UNKNOWN";
    }
}

bool enqueueFirebaseWrite(QueueItemType type, const char* jsonData, const char* key) {
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
    item.key[0] = '\0';
    if (key) {
        strncpy(item.key, key, sizeof(item.key) - 1);
        item.key[sizeof(item.key) - 1] = '\0';
    }

    // Non-blocking send — if the queue is full, LOG/FAULT_SET/FAULT_DELETE
    // fall back to the offline queue (mirrors processFirebaseQueue()'s
    // existing fallback for items that fail *after* dequeuing). STATUS and
    // SCHEDULE_EXECUTED are left to drop silently — they're either stale by
    // the time connectivity returns or resync naturally.
    if (xQueueSend(firebaseQueue, &item, 0) != pdTRUE) {
        if (type == QueueItemType::LOG || type == QueueItemType::FAULT_SET ||
            type == QueueItemType::FAULT_DELETE) {
            OfflineEntryType offlineType =
                type == QueueItemType::LOG ? OfflineEntryType::LOG :
                type == QueueItemType::FAULT_SET ? OfflineEntryType::FAULT_SET :
                                                    OfflineEntryType::FAULT_DELETE;
            if (offlineQueue.enqueue(offlineType, item.jsonData, item.key)) {
                LOG_WARN("Firebase queue full, %s item routed to offline storage", queueItemTypeName(type));
                return true;
            }
        }

        LOG_WARN("Firebase queue full, dropping %s item", queueItemTypeName(type));
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
            } else if (item.type == QueueItemType::LOG) {
                char path[128];
                snprintf(path, sizeof(path), "%s/logs", deviceManager.getDevicePath());
                success = firebaseManager.pushJSON(path, &json);
                if (success) {
                    LOG_DEBUG("Queue: Log sent to Firebase");
                }
            } else if (item.type == QueueItemType::FAULT_SET) {
                char path[160];
                snprintf(path, sizeof(path), "%s/%s", deviceManager.getFaultsPath(), item.key);
                success = firebaseManager.setJSON(path, &json);
                if (success) {
                    LOG_DEBUG("Queue: Fault set (%s) sent to Firebase", item.key);
                }
            } else if (item.type == QueueItemType::FAULT_DELETE) {
                char path[160];
                snprintf(path, sizeof(path), "%s/%s", deviceManager.getFaultsPath(), item.key);
                success = firebaseManager.deleteNode(path);
                if (success) {
                    LOG_DEBUG("Queue: Fault clear (%s) sent to Firebase", item.key);
                }
            } else if (item.type == QueueItemType::SCHEDULE_EXECUTED) {
                // Leaf boolean write — not a JSON object, so this bypasses
                // the `json` built above (FirebaseJson is for object/array
                // literals; a dedicated setBool() is used instead).
                char path[160];
                snprintf(path, sizeof(path), "%s/schedules/%s/executedToday", deviceManager.getDevicePath(), item.key);
                success = firebaseManager.setBool(path, true);
                if (success) {
                    LOG_DEBUG("Queue: Schedule executed (%s) sent to Firebase", item.key);
                }
            }

            if (!success) {
                LOG_ERROR("Queue: Firebase write failed: %s", firebaseManager.getLastError().c_str());
                // Only persist logs and faults to offline storage — not operational status items
                if (item.type == QueueItemType::LOG || item.type == QueueItemType::FAULT_SET ||
                    item.type == QueueItemType::FAULT_DELETE) {
                    OfflineEntryType offlineType =
                        item.type == QueueItemType::LOG ? OfflineEntryType::LOG :
                        item.type == QueueItemType::FAULT_SET ? OfflineEntryType::FAULT_SET :
                                                                 OfflineEntryType::FAULT_DELETE;
                    offlineQueue.enqueue(offlineType, item.jsonData, item.key);
                }
            }

            json.clear();
        } else {
            // Firebase not ready — queue logs/faults to offline storage
            if (item.type == QueueItemType::LOG || item.type == QueueItemType::FAULT_SET ||
                item.type == QueueItemType::FAULT_DELETE) {
                OfflineEntryType offlineType =
                    item.type == QueueItemType::LOG ? OfflineEntryType::LOG :
                    item.type == QueueItemType::FAULT_SET ? OfflineEntryType::FAULT_SET :
                                                             OfflineEntryType::FAULT_DELETE;
                offlineQueue.enqueue(offlineType, item.jsonData, item.key);
                LOG_DEBUG("Queue: %s queued to offline storage", queueItemTypeName(item.type));
            }
        }

        processed++;

        // Heartbeat after EACH item, not just once when the whole batch of
        // up to 3 finishes. A single slow Firebase call can individually
        // approach the library's own serverResponse timeout; three of those
        // in one pass can add up with no heartbeat opportunity in between.
        Watchdog::taskHeartbeat("networkTask");
    }
}

// ============================================================================
// NETWORK TASK (Core 0)
// ============================================================================

// Traces how long each networkTask step took, and heartbeats immediately
// after — both the software heartbeat (checkTaskHealth()) and, indirectly,
// visibility into what was running if the hardware watchdog fires instead.
// Threshold is generous (500ms) so normal execution stays silent; only a
// step that's actually stalling should ever print here.
static void traceStep(const char* name, unsigned long stepStart) {
    unsigned long elapsed = millis() - stepStart;
    if (elapsed >= 500) {
        LOG_WARN("[TRACE] networkTask step '%s' took %lums", name, elapsed);
    }
    Watchdog::taskHeartbeat("networkTask");
}

static void networkTask(void* param) {
    LOG_INFO("Network task started on Core %d", xPortGetCoreID());

    for (;;) {
        unsigned long stepStart;

        // WiFi maintenance (reconnection)
        stepStart = millis();
        wifiManager.tick();
        traceStep("wifiManager.tick", stepStart);

        // Firebase maintenance (reconnection)
        stepStart = millis();
        firebaseManager.tick();
        traceStep("firebaseManager.tick", stepStart);

        // Process queued Firebase writes from SerialProtocol
        stepStart = millis();
        processFirebaseQueue();
        traceStep("processFirebaseQueue", stepStart);

        // Stream command deletion queue
        stepStart = millis();
        streamManager.tick();
        traceStep("streamManager.tick", stepStart);

        // Scheduled tasks (heartbeat, time sync, offline flush)
        stepStart = millis();
        taskScheduler.tick();
        traceStep("taskScheduler.tick", stepStart);

        // OTA update check — polls GitHub every 30 min, downloads if newer.
        // Skipped while Core 1 is forwarding feeder firmware: applyWiFiFirmware()
        // calls Update.write() which disables the ESP32 flash cache on BOTH cores,
        // causing concurrent SPIFFS reads on Core 1 to return 0 bytes and abort
        // the feeder transfer.
        if (!serialOTAForwarder.isForwarding()) {
            stepStart = millis();
            otaManager.tick();
            traceStep("otaManager.tick", stepStart);
        }

        // Yield to other Core 0 tasks (WiFi stack, etc.)
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// ============================================================================
// STARTUP
// ============================================================================

void startNetworkTask() {
    // Create the inter-core Firebase queue (10 items deep)
    firebaseQueue = xQueueCreate(10, sizeof(FirebaseQueueItem));
    if (!firebaseQueue) {
        LOG_ERROR("Failed to create Firebase queue!");
        return;
    }

    // Create the Serial2 command queue (8 slots × 64 bytes — Core 0 enqueues, Core 1 drains)
    serial2CmdQueue = xQueueCreate(8, 64);
    if (!serial2CmdQueue) {
        LOG_ERROR("Failed to create Serial2 command queue!");
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
