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
      deleteQueueCount_(0) {

    instance_ = this;
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void StreamManager::begin(FirebaseManager* fbManager, DeviceManager* devManager) {
    firebaseManager_ = fbManager;
    deviceManager_ = devManager;

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
    delay(1000);  // Give time for cleanup

    return startStream();
}

void StreamManager::stopStream() {
    if (streamActive_) {
        firebaseManager_->endStream();
        streamActive_ = false;
        LOG_INFO("Command stream stopped");
    }
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
    // Auto-restart stream if it was started before but is now inactive
    if (streamStarted_ && !streamActive_ && firebaseManager_->isReady()) {
        unsigned long now = millis();
        if (now - lastRestartAttempt_ >= STREAM_RESTART_INTERVAL) {
            lastRestartAttempt_ = now;
            LOG_WARN("Stream inactive - attempting auto-restart...");
            startStream();
        }
    }

    processDeleteQueue();
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

        // Execute callback
        if (commandCallback_) {
            commandCallback_(commandType, commandId);
        }

        // Queue deletion
        queueCommandDeletion(commandId.c_str());
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

            // Execute callback
            if (commandCallback_) {
                commandCallback_(commandType, commandId);
            }

            // Queue deletion
            queueCommandDeletion(commandId.c_str());
            processedCount++;
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
    if (deleteQueueCount_ >= DELETE_QUEUE_SIZE) {
        LOG_WARN("Delete queue full - command will remain!");
        return;
    }

    // Build full path
    snprintf(deleteQueue_[deleteQueueCount_], 120, "%s/%s",
             deviceManager_->getCommandsPath(), commandId);
    deleteQueueCount_++;

    LOG_DEBUG("Command queued for deletion: %s", commandId);
}

void StreamManager::processDeleteQueue() {
    unsigned long currentMillis = millis();

    // Rate limiting: 100ms between deletions
    if (currentMillis - lastDeleteTime_ < 100) {
        return;
    }

    if (deleteQueueCount_ == 0 || !firebaseManager_->isReady()) {
        return;
    }

    // Process one deletion per iteration
    char deletePath[120];
    strncpy(deletePath, deleteQueue_[0], 119);
    deletePath[119] = '\0';

    LOG_DEBUG("Deleting processed command: %s", deletePath);

    if (firebaseManager_->deleteNode(deletePath)) {
        LOG_DEBUG("Command deleted successfully");
    } else {
        LOG_WARN("Command deletion failed: %s", firebaseManager_->getLastError().c_str());
    }

    // Shift queue
    for (int i = 0; i < deleteQueueCount_ - 1; i++) {
        strcpy(deleteQueue_[i], deleteQueue_[i + 1]);
    }
    deleteQueueCount_--;

    lastDeleteTime_ = currentMillis;
}

bool StreamManager::isCommandInDeleteQueue(const char* commandId) const {
    // Build full path
    char fullPath[120];
    snprintf(fullPath, 120, "%s/%s", deviceManager_->getCommandsPath(), commandId);

    // Check if already queued
    for (int i = 0; i < deleteQueueCount_; i++) {
        if (strcmp(deleteQueue_[i], fullPath) == 0) {
            return true;
        }
    }

    return false;
}
