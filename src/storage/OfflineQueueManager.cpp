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

static const char* offlineEntryTypeName(OfflineEntryType type) {
    switch (type) {
        case OfflineEntryType::LOG:          return "LOG";
        case OfflineEntryType::FAULT_SET:    return "FAULT_SET";
        case OfflineEntryType::FAULT_DELETE: return "FAULT_DELETE";
        default:                             return "UNKNOWN";
    }
}

bool OfflineQueueManager::enqueue(OfflineEntryType type, const String& jsonData, const char* key) {
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
    String entry = buildQueueEntry(type, jsonData, key ? String(key) : String(""));
    if (entry.length() > MAX_ENTRY_SIZE) {
        LOG_WARN("Entry too large (%u bytes), dropping to avoid corrupt JSON", entry.length());
        return false;
    }

    if (SPIFFSHelper::appendLine(OFFLINE_QUEUE_FILE, entry)) {
        LOG_DEBUG("Queued offline %s entry", offlineEntryTypeName(type));
        enforceMaxEntries();
        return true;
    }

    LOG_ERROR("Failed to queue offline entry");
    droppedCount_++;
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
    String key;
    if (!parseQueueEntry(line, type, jsonData, key)) {
        LOG_WARN("Invalid queue entry, removing");
        SPIFFSHelper::removeFirstLine(OFFLINE_QUEUE_FILE);
        return false;
    }

    // Attempt to send
    if (sendToFirebase(type, jsonData, key)) {
        SPIFFSHelper::removeFirstLine(OFFLINE_QUEUE_FILE);
        LOG_INFO("Flushed offline %s entry", offlineEntryTypeName(type));
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

String OfflineQueueManager::buildQueueEntry(OfflineEntryType type, const String& jsonData, const String& key) {
    // Format: {"t":0,"k":"<key>","d":{...}}
    // "k" is only meaningful for FAULT_SET/FAULT_DELETE; "d" defaults to {}
    // for deletes (which carry no JSON payload) so the line stays valid JSON.
    String entry = "{\"t\":";
    entry += String((uint8_t)type);
    entry += ",\"k\":\"";
    entry += key;
    entry += "\",\"d\":";
    entry += (jsonData.length() > 0) ? jsonData : String("{}");
    entry += "}";
    return entry;
}

bool OfflineQueueManager::parseQueueEntry(const String& line, OfflineEntryType& type, String& jsonData, String& key) {
    // Parse: {"t":0,"k":"<key>","d":{...}}
    // "k" is looked up independently and defaults to "" if absent, so a
    // queue entry written by a pre-upgrade firmware (no "k" field) still
    // parses instead of getting dropped as invalid.
    int typeStart = line.indexOf("\"t\":");
    int dataStart = line.indexOf("\"d\":");

    if (typeStart == -1 || dataStart == -1) {
        return false;
    }

    // Extract type — bounded by the next comma, not by dataStart, since "k"
    // may sit between "t" and "d".
    int typeValueEnd = line.indexOf(',', typeStart + 4);
    if (typeValueEnd == -1 || typeValueEnd > dataStart) {
        return false;
    }
    int typeValue = line.substring(typeStart + 4, typeValueEnd).toInt();
    type = static_cast<OfflineEntryType>(typeValue);

    // Extract key (optional)
    int keyStart = line.indexOf("\"k\":\"");
    if (keyStart != -1 && keyStart < dataStart) {
        int keyValueStart = keyStart + 5;  // length of "k":"
        int keyValueEnd = line.indexOf('"', keyValueStart);
        key = (keyValueEnd != -1) ? line.substring(keyValueStart, keyValueEnd) : "";
    } else {
        key = "";
    }

    // Extract data (everything after "d": until last })
    int dataValueStart = dataStart + 4;
    int dataEnd = line.lastIndexOf('}');
    if (dataEnd <= dataValueStart) {
        return false;
    }

    jsonData = line.substring(dataValueStart, dataEnd);
    return true;
}

bool OfflineQueueManager::sendToFirebase(OfflineEntryType type, const String& jsonData, const String& key) {
    if (!firebaseManager_ || !deviceManager_) {
        return false;
    }

    if (type == OfflineEntryType::LOG) {
        FirebaseJson json;
        json.setJsonData(jsonData);
        char path[128];
        snprintf(path, sizeof(path), "%s/logs", deviceManager_->getDevicePath());
        bool success = firebaseManager_->pushJSON(path, &json);
        json.clear();
        return success;
    }

    // FAULT_SET / FAULT_DELETE — fixed key under the faults path
    char path[160];
    snprintf(path, sizeof(path), "%s/%s", deviceManager_->getFaultsPath(), key.c_str());

    if (type == OfflineEntryType::FAULT_SET) {
        FirebaseJson json;
        json.setJsonData(jsonData);
        bool success = firebaseManager_->setJSON(path, &json);
        json.clear();
        return success;
    }

    // FAULT_DELETE — no payload
    return firebaseManager_->deleteNode(path);
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
