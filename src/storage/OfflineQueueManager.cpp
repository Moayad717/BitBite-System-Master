#include "OfflineQueueManager.h"
#include "SPIFFSHelper.h"
#include "../config/StorageConfig.h"
#include "../connectivity/FirebaseManager.h"
#include "../core/DeviceManager.h"
#include "../core/LogManager.h"

// ============================================================================
// CONSTRUCTOR
// ============================================================================

OfflineQueueManager::OfflineQueueManager()
    : firebaseManager_(nullptr),
      deviceManager_(nullptr),
      droppedCount_(0) {
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void OfflineQueueManager::begin(FirebaseManager* fbManager, DeviceManager* devManager) {
    firebaseManager_ = fbManager;
    deviceManager_ = devManager;

    if (SPIFFSHelper::isReady()) {
        size_t count = getEntryCount();
        if (count > 0) {
            LOG_INFO("OfflineQueue: %u entries pending", count);
        }
    }

    LOG_INFO("OfflineQueueManager initialized");
}

// ============================================================================
// QUEUE OPERATIONS
// ============================================================================

bool OfflineQueueManager::enqueue(OfflineEntryType type, const String& jsonData) {
    if (!SPIFFSHelper::isReady()) {
        LOG_WARN("SPIFFS not ready - cannot queue offline entry");
        return false;
    }

    // Check storage space
    if (SPIFFSHelper::getFreeBytes() < SPIFFS_MIN_FREE_BYTES) {
        LOG_WARN("SPIFFS low on space - dropping oldest entries");
        enforceMaxEntries();
    }

    // Build and append entry
    String entry = buildQueueEntry(type, jsonData);
    if (entry.length() > MAX_ENTRY_SIZE) {
        LOG_WARN("Entry too large (%u bytes), dropping to avoid corrupt JSON", entry.length());
        return false;
    }

    if (SPIFFSHelper::appendLine(OFFLINE_QUEUE_FILE, entry)) {
        LOG_DEBUG("Queued offline %s entry", type == OfflineEntryType::LOG ? "LOG" : "FAULT");
        enforceMaxEntries();
        return true;
    }

    LOG_ERROR("Failed to queue offline entry");
    return false;
}

bool OfflineQueueManager::hasEntries() const {
    if (!SPIFFSHelper::isReady()) return false;
    return SPIFFSHelper::fileExists(OFFLINE_QUEUE_FILE) &&
           SPIFFSHelper::getFileSize(OFFLINE_QUEUE_FILE) > 0;
}

size_t OfflineQueueManager::getEntryCount() const {
    if (!SPIFFSHelper::isReady()) return 0;
    return SPIFFSHelper::getLineCount(OFFLINE_QUEUE_FILE);
}

// ============================================================================
// FLUSH OPERATIONS
// ============================================================================

bool OfflineQueueManager::flushOne() {
    if (!SPIFFSHelper::isReady() || !hasEntries()) {
        return false;
    }

    if (!firebaseManager_ || !firebaseManager_->isReady()) {
        return false;
    }

    // Read first entry
    String line = SPIFFSHelper::readFirstLine(OFFLINE_QUEUE_FILE);
    if (line.length() == 0) {
        return false;
    }

    // Parse entry
    OfflineEntryType type;
    String jsonData;
    if (!parseQueueEntry(line, type, jsonData)) {
        LOG_WARN("Invalid queue entry, removing");
        SPIFFSHelper::removeFirstLine(OFFLINE_QUEUE_FILE);
        return false;
    }

    // Attempt to send
    if (sendToFirebase(type, jsonData)) {
        SPIFFSHelper::removeFirstLine(OFFLINE_QUEUE_FILE);
        LOG_INFO("Flushed offline %s entry", type == OfflineEntryType::LOG ? "LOG" : "FAULT");
        return true;
    }

    // Send failed, leave in queue for next attempt
    return false;
}

void OfflineQueueManager::clear() {
    if (SPIFFSHelper::isReady()) {
        SPIFFSHelper::deleteFile(OFFLINE_QUEUE_FILE);
        LOG_INFO("Offline queue cleared");
    }
}

// ============================================================================
// HELPERS
// ============================================================================

String OfflineQueueManager::buildQueueEntry(OfflineEntryType type, const String& jsonData) {
    // Format: {"t":0,"d":{...}}
    String entry = "{\"t\":";
    entry += String((uint8_t)type);
    entry += ",\"d\":";
    entry += jsonData;
    entry += "}";
    return entry;
}

bool OfflineQueueManager::parseQueueEntry(const String& line, OfflineEntryType& type, String& jsonData) {
    // Parse: {"t":0,"d":{...}}
    int typeStart = line.indexOf("\"t\":");
    int dataStart = line.indexOf("\"d\":");

    if (typeStart == -1 || dataStart == -1) {
        return false;
    }

    // Extract type
    int typeValue = line.substring(typeStart + 4, dataStart - 1).toInt();
    type = static_cast<OfflineEntryType>(typeValue);

    // Extract data (everything after "d": until last })
    int dataEnd = line.lastIndexOf('}');
    if (dataEnd <= dataStart) {
        return false;
    }

    jsonData = line.substring(dataStart + 4, dataEnd);
    return jsonData.length() > 0;
}

bool OfflineQueueManager::sendToFirebase(OfflineEntryType type, const String& jsonData) {
    if (!firebaseManager_ || !deviceManager_) {
        return false;
    }

    FirebaseJson json;
    json.setJsonData(jsonData);

    char path[128];
    if (type == OfflineEntryType::LOG) {
        snprintf(path, sizeof(path), "%s/logs", deviceManager_->getDevicePath());
    } else {
        snprintf(path, sizeof(path), "%s/faults", deviceManager_->getDevicePath());
    }

    bool success = firebaseManager_->pushJSON(path, &json);
    json.clear();
    return success;
}

void OfflineQueueManager::enforceMaxEntries() {
    size_t count = getEntryCount();
    while (count > MAX_OFFLINE_ENTRIES) {
        SPIFFSHelper::removeFirstLine(OFFLINE_QUEUE_FILE);
        droppedCount_++;
        count--;
        LOG_WARN("Dropped oldest offline entry (queue full)");
    }
}
