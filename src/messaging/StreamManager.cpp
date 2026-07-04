#include "StreamManager.h"
#include "../connectivity/FirebaseManager.h"
#include "../core/DeviceManager.h"
#include "../core/LogManager.h"
#include "../config/TimingConfig.h"

// Static instance for callbacks
StreamManager* StreamManager::instance_ = nullptr;

// ============================================================================
// CONSTRUCTOR
// ============================================================================

StreamManager::StreamManager()
    : firebaseManager_(nullptr),
      deviceManager_(nullptr),
      commandCallback_(nullptr),
      streamActive_(false),
      streamStarted_(false),
      lastDeleteTime_(0),
      lastRestartAttempt_(0),
      deleteQueue_(nullptr),
      deleteTrackingCount_(0),
      deleteTrackingMutex_(nullptr),
      pendingCmdQueue_(nullptr) {

    instance_ = this;
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void StreamManager::begin(FirebaseManager* fbManager, DeviceManager* devManager) {
    firebaseManager_ = fbManager;
    deviceManager_ = devManager;

    pendingCmdQueue_ = xQueueCreate(PENDING_CMD_QUEUE_SIZE, sizeof(PendingCommand));
    if (!pendingCmdQueue_) {
        LOG_ERROR("Failed to create pending command queue!");
    }

    deleteQueue_ = xQueueCreate(DELETE_QUEUE_SIZE, sizeof(DeleteQueueItem));
    if (!deleteQueue_) {
        LOG_ERROR("Failed to create delete queue!");
    }

    deleteTrackingMutex_ = xSemaphoreCreateMutex();
    if (!deleteTrackingMutex_) {
        LOG_ERROR("Failed to create delete tracking mutex!");
    }

    LOG_INFO("StreamManager initialized");
}

void StreamManager::setCommandCallback(CommandCallback callback) {
    commandCallback_ = callback;
}

// ============================================================================
// STREAM LIFECYCLE
// ============================================================================

bool StreamManager::startStream() {
    if (!firebaseManager_ || !deviceManager_) {
        LOG_ERROR("StreamManager not properly initialized");
        return false;
    }

    if (!firebaseManager_->isReady()) {
        LOG_ERROR("Firebase not ready for stream setup");
        return false;
    }

    LOG_INFO("Starting Firebase command stream...");

    bool success = firebaseManager_->beginStream(
        deviceManager_->getCommandsPath(),
        streamCallbackWrapper,
        streamTimeoutCallbackWrapper
    );

    if (success) {
        streamActive_ = true;
        streamStarted_ = true;
        LOG_INFO("Command stream started successfully");
    } else {
        LOG_ERROR("Failed to start command stream: %s", firebaseManager_->getLastError().c_str());
    }

    return success;
}

bool StreamManager::restartStream() {
    LOG_INFO("Restarting command stream...");

    stopStream();
    delay(2000);  // Give SSL teardown time to complete

    return startStream();
}

void StreamManager::stopStream() {
    // Always end the stream regardless of streamActive_ state.
    // If a timeout cleared streamActive_ before we get here, the underlying
    // streamFbdo_ SSL connection is still open.  Calling endStream()+clear()
    // unconditionally is the only way to free those buffers and prevent the
    // "Incoming protocol or record version unsupported" SSL error on restart.
    firebaseManager_->endStream();
    streamActive_ = false;
    LOG_INFO("Command stream stopped");
}

bool StreamManager::isActive() const {
    return streamActive_;
}

void StreamManager::markInactive() {
    streamActive_ = false;
    LOG_WARN("Stream marked inactive (external trigger)");
}

// ============================================================================
// PERIODIC MAINTENANCE
// ============================================================================

void StreamManager::tick() {
    // Dispatch commands received in stream callback — must run here (networkTask
    // context, Core 0) so that blocking Firebase calls like getString() work.
    dispatchPendingCommands();

    // Auto-restart stream if it was started before but is now inactive
    if (streamStarted_ && !streamActive_ && firebaseManager_->isReady()) {
        unsigned long now = millis();
        if (now - lastRestartAttempt_ >= STREAM_RESTART_INTERVAL) {
            lastRestartAttempt_ = now;
            LOG_WARN("Stream inactive - attempting auto-restart...");
            restartStream();  // Must go through stopStream() to clear stale SSL state
        }
    }

    processDeleteQueue();
}

void StreamManager::dispatchPendingCommands() {
    if (!pendingCmdQueue_ || !commandCallback_) {
        return;
    }

    PendingCommand cmd;
    while (xQueueReceive(pendingCmdQueue_, &cmd, 0) == pdTRUE) {
        commandCallback_(String(cmd.type), String(cmd.id));
    }
}

// ============================================================================
// STREAM CALLBACKS (STATIC)
// ============================================================================

void StreamManager::streamCallbackWrapper(FirebaseStream data) {
    if (instance_) {
        instance_->handleStreamEvent(data);
    }
}

void StreamManager::streamTimeoutCallbackWrapper(bool timeout) {
    if (instance_) {
        instance_->handleStreamTimeout(timeout);
    }
}

// ============================================================================
// STREAM EVENT HANDLING
// ============================================================================

void StreamManager::handleStreamEvent(FirebaseStream& data) {
    LOG_INFO("Stream event received");
    LOG_DEBUG("  Path: %s", data.dataPath().c_str());
    LOG_DEBUG("  Type: %s", data.dataType().c_str());

    if (data.dataType() == "json") {
        FirebaseJson json = data.to<FirebaseJson>();

        String path = data.dataPath();
        path.replace("/", "");

        // Case 1: Single command added (path = command ID)
        if (path.length() > 0) {
            processSingleCommand(json, path);
        }
        // Case 2: Multiple commands (stream initialization or bulk update)
        else {
            processMultipleCommands(json);
        }

        json.clear();
    } else if (data.dataType() == "null") {
        LOG_INFO("Commands cleared");
    }
}

void StreamManager::handleStreamTimeout(bool timeout) {
    if (timeout) {
        LOG_WARN("Stream timeout detected");
        streamActive_ = false;
    }
}

// ============================================================================
// COMMAND PROCESSING
// ============================================================================

void StreamManager::processSingleCommand(FirebaseJson& json, const String& commandId) {
    // Check if already queued (prevents duplicate processing on reconnect)
    if (isCommandInDeleteQueue(commandId.c_str())) {
        LOG_DEBUG("Command already queued, skipping: %s", commandId.c_str());
        return;
    }

    FirebaseJsonData typeData;
    if (json.get(typeData, "type")) {
        String commandType = typeData.stringValue;
        LOG_INFO("Received command: %s (ID: %s)", commandType.c_str(), commandId.c_str());

        // Push to pending queue — the callback is dispatched from tick() (networkTask
        // context) to avoid calling blocking Firebase ops inside the stream callback.
        bool dispatched = false;
        if (pendingCmdQueue_) {
            PendingCommand cmd;
            strncpy(cmd.type, commandType.c_str(), sizeof(cmd.type) - 1);
            cmd.type[sizeof(cmd.type) - 1] = '\0';
            strncpy(cmd.id, commandId.c_str(), sizeof(cmd.id) - 1);
            cmd.id[sizeof(cmd.id) - 1] = '\0';
            if (xQueueSend(pendingCmdQueue_, &cmd, 0) == pdTRUE) {
                dispatched = true;
            } else {
                LOG_WARN("Pending command queue full, dropping: %s", commandType.c_str());
            }
        }

        // Only delete from Firebase once dispatch actually succeeded — if the
        // pending queue was full, leave the command in place so it's picked
        // up on the next pass instead of being silently lost.
        if (dispatched) {
            queueCommandDeletion(commandId.c_str());
        }
    } else {
        LOG_WARN("Command missing 'type' field");
    }
}

void StreamManager::processMultipleCommands(FirebaseJson& json) {
    LOG_INFO("Processing multiple queued commands...");

    size_t count = json.iteratorBegin();
    LOG_INFO("Found %d pending commands", count);

    int processedCount = 0;
    int skippedCount = 0;

    for (size_t i = 0; i < count; i++) {
        int type;
        String commandId;
        String commandData;

        json.iteratorGet(i, type, commandId, commandData);

        // Check if already queued
        if (isCommandInDeleteQueue(commandId.c_str())) {
            LOG_DEBUG("  [%d/%d] Skipping already queued: %s", i+1, count, commandId.c_str());
            skippedCount++;
            continue;
        }

        // Parse command
        FirebaseJson cmdJson;
        cmdJson.setJsonData(commandData);

        FirebaseJsonData typeData;
        if (cmdJson.get(typeData, "type")) {
            String commandType = typeData.stringValue;
            LOG_INFO("  [%d/%d] Command: %s (ID: %s)", i+1, count, commandType.c_str(), commandId.c_str());

            // Push to pending queue (dispatched from tick(), not from callback context)
            bool dispatched = false;
            if (pendingCmdQueue_) {
                PendingCommand cmd;
                strncpy(cmd.type, commandType.c_str(), sizeof(cmd.type) - 1);
                cmd.type[sizeof(cmd.type) - 1] = '\0';
                strncpy(cmd.id, commandId.c_str(), sizeof(cmd.id) - 1);
                cmd.id[sizeof(cmd.id) - 1] = '\0';
                if (xQueueSend(pendingCmdQueue_, &cmd, 0) == pdTRUE) {
                    dispatched = true;
                } else {
                    LOG_WARN("  Pending command queue full, dropping: %s", commandType.c_str());
                }
            }

            // Only delete from Firebase once dispatch actually succeeded —
            // otherwise leave it in place so it's retried next pass.
            if (dispatched) {
                queueCommandDeletion(commandId.c_str());
                processedCount++;
            }
        }

        cmdJson.clear();
    }

    json.iteratorEnd();
    LOG_INFO("Processed %d new commands, skipped %d duplicates", processedCount, skippedCount);
}

// ============================================================================
// COMMAND DELETION QUEUE
// ============================================================================

void StreamManager::queueCommandDeletion(const char* commandId) {
    char path[120];
    snprintf(path, sizeof(path), "%s/%s", deviceManager_->getCommandsPath(), commandId);

    // Track BEFORE sending so isCommandInDeleteQueue() can never miss an
    // in-flight command due to a race between these two steps.
    addToDeleteTracking(path);

    if (!deleteQueue_) {
        removeFromDeleteTracking(path);
        return;
    }

    DeleteQueueItem item;
    strncpy(item.path, path, sizeof(item.path) - 1);
    item.path[sizeof(item.path) - 1] = '\0';

    if (xQueueSend(deleteQueue_, &item, 0) != pdTRUE) {
        LOG_WARN("Delete queue full - command will remain: %s", commandId);
        // Roll back tracking so this command isn't falsely blocked forever
        removeFromDeleteTracking(path);
        return;
    }

    LOG_DEBUG("Command queued for deletion: %s", commandId);
}

void StreamManager::processDeleteQueue() {
    unsigned long currentMillis = millis();

    // Rate limiting: 100ms between deletions
    if (currentMillis - lastDeleteTime_ < 100) {
        return;
    }

    if (!deleteQueue_ || !firebaseManager_->isReady()) {
        return;
    }

    // Process one deletion per iteration
    DeleteQueueItem item;
    if (xQueueReceive(deleteQueue_, &item, 0) != pdTRUE) {
        return;  // nothing queued
    }

    LOG_DEBUG("Deleting processed command: %s", item.path);

    if (firebaseManager_->deleteNode(item.path)) {
        LOG_DEBUG("Command deleted successfully");
    } else {
        LOG_WARN("Command deletion failed: %s", firebaseManager_->getLastError().c_str());
    }

    removeFromDeleteTracking(item.path);

    lastDeleteTime_ = currentMillis;
}

bool StreamManager::isCommandInDeleteQueue(const char* commandId) const {
    char fullPath[120];
    snprintf(fullPath, sizeof(fullPath), "%s/%s", deviceManager_->getCommandsPath(), commandId);

    bool found = false;
    if (deleteTrackingMutex_ && xSemaphoreTake(deleteTrackingMutex_, portMAX_DELAY) == pdTRUE) {
        for (int i = 0; i < deleteTrackingCount_; i++) {
            if (strcmp(deleteTracking_[i], fullPath) == 0) {
                found = true;
                break;
            }
        }
        xSemaphoreGive(deleteTrackingMutex_);
    }

    return found;
}

void StreamManager::addToDeleteTracking(const char* path) {
    if (!deleteTrackingMutex_) {
        return;
    }
    if (xSemaphoreTake(deleteTrackingMutex_, portMAX_DELAY) == pdTRUE) {
        if (deleteTrackingCount_ < DELETE_QUEUE_SIZE) {
            strncpy(deleteTracking_[deleteTrackingCount_], path, sizeof(deleteTracking_[0]) - 1);
            deleteTracking_[deleteTrackingCount_][sizeof(deleteTracking_[0]) - 1] = '\0';
            deleteTrackingCount_++;
        } else {
            LOG_WARN("Delete tracking set full - duplicate detection may miss: %s", path);
        }
        xSemaphoreGive(deleteTrackingMutex_);
    }
}

void StreamManager::removeFromDeleteTracking(const char* path) {
    if (!deleteTrackingMutex_) {
        return;
    }
    if (xSemaphoreTake(deleteTrackingMutex_, portMAX_DELAY) == pdTRUE) {
        for (int i = 0; i < deleteTrackingCount_; i++) {
            if (strcmp(deleteTracking_[i], path) == 0) {
                for (int j = i; j < deleteTrackingCount_ - 1; j++) {
                    strcpy(deleteTracking_[j], deleteTracking_[j + 1]);
                }
                deleteTrackingCount_--;
                break;
            }
        }
        xSemaphoreGive(deleteTrackingMutex_);
    }
}
