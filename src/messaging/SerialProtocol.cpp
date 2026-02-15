#include "SerialProtocol.h"
#include "../connectivity/FirebaseManager.h"
#include "../core/DeviceManager.h"
#include "../core/TimeManager.h"
#include "../core/LogManager.h"
#include "../core/DualCoreManager.h"
#include "../storage/OfflineQueueManager.h"
#include "../config/TimingConfig.h"

// ============================================================================
// CONSTRUCTOR
// ============================================================================

SerialProtocol::SerialProtocol()
    : firebaseManager_(nullptr),
      deviceManager_(nullptr),
      timeManager_(nullptr),
      offlineQueue_(nullptr),
      statusCallback_(nullptr) {

    scheduleSyncState_.waitingForConfirmation = false;
    scheduleSyncState_.expectedHash = "";
    scheduleSyncState_.syncTime = 0;

    scheduleStatusState_.collecting = false;
    scheduleStatusState_.itemCount = 0;
    scheduleStatusState_.startTime = 0;
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void SerialProtocol::begin(FirebaseManager* fbManager, DeviceManager* devManager, TimeManager* timeManager,
                           OfflineQueueManager* offlineQueue) {
    firebaseManager_ = fbManager;
    deviceManager_ = devManager;
    timeManager_ = timeManager;
    offlineQueue_ = offlineQueue;

    LOG_INFO("SerialProtocol initialized%s", offlineQueue ? " (with offline queue)" : "");
}

void SerialProtocol::setStatusUpdateCallback(StatusUpdateCallback callback) {
    statusCallback_ = callback;
}

// ============================================================================
// PERIODIC TICK
// ============================================================================

void SerialProtocol::tick() {
    handleIncomingData();
}

// ============================================================================
// OUTGOING MESSAGES
// ============================================================================

void SerialProtocol::sendTime() {
    if (!timeManager_) {
        LOG_WARN("TimeManager not initialized");
        return;
    }

    char timestamp[32];
    deviceManager_->getTimestamp(timestamp, sizeof(timestamp));

    Serial2.printf("TIME:%s\n", timestamp);
    LOG_INFO("Time sent to Feeding ESP: %s", timestamp);
}

void SerialProtocol::sendDisplayName() {
    if (!firebaseManager_ || !firebaseManager_->isReady()) {
        LOG_WARN("Firebase not ready - cannot send display name");
        return;
    }

    String displayName;
    if (firebaseManager_->getString(deviceManager_->getDisplayNamePath(), displayName)) {
        Serial2.printf("NAME:%s\n", displayName.c_str());
        LOG_INFO("Display name sent: %s", displayName.c_str());
    } else {
        LOG_ERROR("Failed to get display name: %s", firebaseManager_->getLastError().c_str());
    }
}

void SerialProtocol::syncSchedules() {
    if (!firebaseManager_ || !firebaseManager_->isReady()) {
        LOG_WARN("Firebase not ready - cannot sync schedules");
        return;
    }

    LOG_INFO("Syncing schedules to Feeding ESP...");

    String schedulesJson;
    if (firebaseManager_->getString(deviceManager_->getSchedulePath(), schedulesJson)) {
        if (schedulesJson == "null" || schedulesJson.length() == 0) {
            LOG_INFO("No schedules - clearing Feeding ESP");
            Serial2.println("SCHEDULES:{}");

            scheduleSyncState_.waitingForConfirmation = true;
            scheduleSyncState_.expectedHash = "0";
            scheduleSyncState_.syncTime = millis();
        } else {
            // Calculate hash for verification
            unsigned long hash = calculateJsonHash(schedulesJson);

            // Send schedules
            Serial2.printf("SCHEDULES:%s\n", schedulesJson.c_str());
            Serial2.flush();  // Wait for TX to complete
            delay(100);       // Give feeder ESP time to buffer the data
            LOG_INFO("Schedules sent (%d bytes, hash: %lu)", schedulesJson.length(), hash);

            // Setup verification
            scheduleSyncState_.waitingForConfirmation = true;
            scheduleSyncState_.expectedHash = String(hash);
            scheduleSyncState_.syncTime = millis();
        }
    } else {
        String error = firebaseManager_->getLastError();
        if (error.indexOf("not found") != -1 || error.indexOf("null") != -1) {
            LOG_INFO("Schedules path doesn't exist - sending empty");
            Serial2.println("SCHEDULES:{}");

            scheduleSyncState_.waitingForConfirmation = true;
            scheduleSyncState_.expectedHash = "0";
            scheduleSyncState_.syncTime = millis();
        } else {
            LOG_ERROR("Failed to get schedules: %s", error.c_str());
        }
    }
}

// ============================================================================
// INCOMING MESSAGE HANDLING
// ============================================================================

void SerialProtocol::handleIncomingData() {
    // Check for schedule sync timeout
    if (scheduleSyncState_.waitingForConfirmation &&
        (millis() - scheduleSyncState_.syncTime >= SCHEDULE_SYNC_TIMEOUT)) {
        LOG_WARN("Schedule sync confirmation timed out");
        sendScheduleSyncStatus(false, "Confirmation timeout");
        scheduleSyncState_.waitingForConfirmation = false;
    }

    // Check for schedule status collection timeout
    if (scheduleStatusState_.collecting &&
        (millis() - scheduleStatusState_.startTime >= SCHEDULE_STATUS_TIMEOUT)) {
        LOG_WARN("Schedule status collection timed out");
        scheduleStatusState_.collecting = false;
    }

    if (!Serial2.available()) {
        return;
    }

    String message = Serial2.readStringUntil('\n');
    message.trim();

    if (message.length() == 0) {
        return;
    }

    LOG_DEBUG("Serial RX: %s", message.c_str());
    processMessage(message);
}

void SerialProtocol::processMessage(const String& message) {
    if (message.startsWith("LOG:")) {
        handleLogEntry(message.substring(4));
    } else if (message.startsWith("FAULT:")) {
        handleFaultEntry(message.substring(6));
    } else if (message.startsWith("SCHEDULE_HASH:")) {
        handleScheduleHash(message.substring(14));
    } else if (message.startsWith("SCHEDULE_STATUS:END")) {
        handleScheduleStatusEnd();
    } else if (message.startsWith("SCHEDULE_STATUS:")) {
        handleScheduleStatusHeader(message.substring(16));
    } else if (message.startsWith("SCHEDULE_ITEM:")) {
        handleScheduleItem(message.substring(14));
    } else if (message.startsWith("{") && message.endsWith("}")) {
        handleStatusUpdate(message);
    } else {
        LOG_WARN("Unknown message format: %s", message.c_str());
    }
}

void SerialProtocol::handleStatusUpdate(const String& statusJson) {
    // Sanitize JSON (replace NaN/Inf with -999)
    String sanitized = sanitizeJson(statusJson);

    // Notify callback (runs on Core 1, same core as OLED — no mutex needed)
    if (statusCallback_) {
        statusCallback_(sanitized);
    }

    // Build full status JSON with WiFi info, then queue for Core 0
    FirebaseJson json;
    json.setJsonData(sanitized);

    char timestamp[25];
    deviceManager_->getTimestamp(timestamp, sizeof(timestamp));
    json.set("isOnline", true);
    json.set("wifiSignal", WiFi.RSSI());
    json.set("lastSeen", timestamp);

    String fullJson;
    json.toString(fullJson);
    json.clear();

    if (!enqueueFirebaseWrite(QueueItemType::STATUS, fullJson.c_str())) {
        LOG_WARN("Failed to queue status update");
    }
}

void SerialProtocol::handleLogEntry(const String& logJson) {
    String sanitized = sanitizeJson(logJson);
    LOG_INFO("Log from Feeding ESP");

    // Add timestamp to the data
    char timestamp[25];
    deviceManager_->getTimestamp(timestamp, sizeof(timestamp));

    // Build JSON with timestamp
    String dataWithTimestamp = sanitized;
    // Insert timestamp into JSON (before closing brace)
    int lastBrace = dataWithTimestamp.lastIndexOf('}');
    if (lastBrace >= 0) {
        dataWithTimestamp = dataWithTimestamp.substring(0, lastBrace);
        dataWithTimestamp += ",\"timestamp\":\"";
        dataWithTimestamp += timestamp;
        dataWithTimestamp += "\"}";
    }

    // Queue for Core 0 to send to Firebase (or offline storage if unavailable)
    if (!enqueueFirebaseWrite(QueueItemType::LOG, dataWithTimestamp.c_str())) {
        LOG_WARN("Failed to queue log entry");
    }
}

void SerialProtocol::handleFaultEntry(const String& faultJson) {
    String sanitized = sanitizeJson(faultJson);
    LOG_WARN("Fault from Feeding ESP");

    // Add timestamp to the data
    char timestamp[25];
    deviceManager_->getTimestamp(timestamp, sizeof(timestamp));

    // Build JSON with timestamp
    String dataWithTimestamp = sanitized;
    // Insert timestamp into JSON (before closing brace)
    int lastBrace = dataWithTimestamp.lastIndexOf('}');
    if (lastBrace >= 0) {
        dataWithTimestamp = dataWithTimestamp.substring(0, lastBrace);
        dataWithTimestamp += ",\"timestamp\":\"";
        dataWithTimestamp += timestamp;
        dataWithTimestamp += "\"}";
    }

    // Queue for Core 0 to send to Firebase (or offline storage if unavailable)
    if (!enqueueFirebaseWrite(QueueItemType::FAULT, dataWithTimestamp.c_str())) {
        LOG_WARN("Failed to queue fault entry");
    }
}

void SerialProtocol::handleScheduleHash(const String& hash) {
    String receivedHash = hash;
    receivedHash.trim();

    if (scheduleSyncState_.waitingForConfirmation) {
        if (receivedHash == scheduleSyncState_.expectedHash) {
            LOG_INFO("Schedule sync verified - hash matched!");
            sendScheduleSyncStatus(true, "Schedules synced successfully");
        } else {
            LOG_ERROR("Schedule hash mismatch! Expected: %s, Got: %s",
                     scheduleSyncState_.expectedHash.c_str(), receivedHash.c_str());
            sendScheduleSyncStatus(false, "Hash mismatch - sync failed");
        }
        scheduleSyncState_.waitingForConfirmation = false;
    }
}

// ============================================================================
// SCHEDULE STATUS HANDLING
// ============================================================================

void SerialProtocol::handleScheduleStatusHeader(const String& header) {
    // Parse: Date=20260215,Time=14:30,Day=5,Count=16
    LOG_INFO("Schedule status header received");

    scheduleStatusState_.collecting = true;
    scheduleStatusState_.startTime = millis();
    scheduleStatusState_.itemCount = 0;
    scheduleStatusState_.itemsJson = "[";

    // Parse header key=value pairs into JSON
    scheduleStatusState_.headerJson = "{";
    int start = 0;
    bool first = true;
    while (start < (int)header.length()) {
        int comma = header.indexOf(',', start);
        if (comma == -1) comma = header.length();

        String pair = header.substring(start, comma);
        int eq = pair.indexOf('=');
        if (eq > 0) {
            String key = pair.substring(0, eq);
            String value = pair.substring(eq + 1);
            key.trim();
            value.trim();

            if (!first) scheduleStatusState_.headerJson += ",";
            // Keep numeric values unquoted
            if (key == "Count" || key == "Day") {
                scheduleStatusState_.headerJson += "\"";
                scheduleStatusState_.headerJson += key;
                scheduleStatusState_.headerJson += "\":";
                scheduleStatusState_.headerJson += value;
            } else {
                scheduleStatusState_.headerJson += "\"";
                scheduleStatusState_.headerJson += key;
                scheduleStatusState_.headerJson += "\":\"";
                scheduleStatusState_.headerJson += value;
                scheduleStatusState_.headerJson += "\"";
            }
            first = false;
        }
        start = comma + 1;
    }
    scheduleStatusState_.headerJson += "}";
}

void SerialProtocol::handleScheduleItem(const String& item) {
    // Parse: 0,Time=06:00,Days=0x7F,Amount=0.150,Enabled=1,AppliesNow=1,ExecutedToday=1,LastExec=20260215
    if (!scheduleStatusState_.collecting) {
        LOG_WARN("Received SCHEDULE_ITEM without active collection");
        return;
    }

    if (scheduleStatusState_.itemCount > 0) {
        scheduleStatusState_.itemsJson += ",";
    }

    // First field is the index
    int firstComma = item.indexOf(',');
    String index = (firstComma > 0) ? item.substring(0, firstComma) : "0";
    String fields = (firstComma > 0) ? item.substring(firstComma + 1) : "";

    scheduleStatusState_.itemsJson += "{\"index\":";
    scheduleStatusState_.itemsJson += index;

    // Parse remaining key=value pairs
    int start = 0;
    while (start < (int)fields.length()) {
        int comma = fields.indexOf(',', start);
        if (comma == -1) comma = fields.length();

        String pair = fields.substring(start, comma);
        int eq = pair.indexOf('=');
        if (eq > 0) {
            String key = pair.substring(0, eq);
            String value = pair.substring(eq + 1);
            key.trim();
            value.trim();

            // Numeric fields: Enabled, AppliesNow, ExecutedToday, Amount, Days (hex)
            if (key == "Enabled" || key == "AppliesNow" || key == "ExecutedToday") {
                scheduleStatusState_.itemsJson += ",\"";
                scheduleStatusState_.itemsJson += key;
                scheduleStatusState_.itemsJson += "\":";
                scheduleStatusState_.itemsJson += (value == "1" ? "true" : "false");
            } else if (key == "Amount") {
                scheduleStatusState_.itemsJson += ",\"";
                scheduleStatusState_.itemsJson += key;
                scheduleStatusState_.itemsJson += "\":";
                scheduleStatusState_.itemsJson += value;
            } else {
                scheduleStatusState_.itemsJson += ",\"";
                scheduleStatusState_.itemsJson += key;
                scheduleStatusState_.itemsJson += "\":\"";
                scheduleStatusState_.itemsJson += value;
                scheduleStatusState_.itemsJson += "\"";
            }
        }
        start = comma + 1;
    }

    scheduleStatusState_.itemsJson += "}";
    scheduleStatusState_.itemCount++;
}

void SerialProtocol::handleScheduleStatusEnd() {
    if (!scheduleStatusState_.collecting) {
        LOG_WARN("Received SCHEDULE_STATUS:END without active collection");
        return;
    }

    scheduleStatusState_.collecting = false;
    scheduleStatusState_.itemsJson += "]";

    LOG_INFO("Schedule status complete: %d items", scheduleStatusState_.itemCount);
    sendScheduleStatusToFirebase();
}

void SerialProtocol::sendScheduleStatusToFirebase() {
    if (!firebaseManager_ || !firebaseManager_->isReady()) {
        LOG_WARN("Firebase not ready - cannot send schedule status");
        return;
    }

    // Build combined JSON: { header info + "items": [...] }
    FirebaseJson json;
    json.setJsonData(scheduleStatusState_.headerJson);

    FirebaseJson itemsArray;
    itemsArray.setJsonData(scheduleStatusState_.itemsJson);
    json.set("items", itemsArray);

    char timestamp[25];
    deviceManager_->getTimestamp(timestamp, sizeof(timestamp));
    json.set("timestamp", timestamp);

    char path[128];
    snprintf(path, sizeof(path), "%s/scheduleStatus", deviceManager_->getDevicePath());

    if (firebaseManager_->setJSON(path, &json)) {
        LOG_INFO("Schedule status sent to Firebase");
    } else {
        LOG_ERROR("Failed to send schedule status: %s", firebaseManager_->getLastError().c_str());
    }

    json.clear();
    itemsArray.clear();

    // Free accumulated strings
    scheduleStatusState_.headerJson = "";
    scheduleStatusState_.itemsJson = "";
}

// ============================================================================
// HELPERS
// ============================================================================

void SerialProtocol::sendScheduleSyncStatus(bool success, const char* message) {
    if (!firebaseManager_ || !firebaseManager_->isReady()) {
        return;
    }

    FirebaseJson json;
    json.set("success", success);
    json.set("message", message);

    char timestamp[25];
    deviceManager_->getTimestamp(timestamp, sizeof(timestamp));
    json.set("timestamp", timestamp);

    char syncPath[128];
    snprintf(syncPath, sizeof(syncPath), "%s/lastScheduleSync", deviceManager_->getDevicePath());

    if (firebaseManager_->setJSON(syncPath, &json)) {
        LOG_DEBUG("Schedule sync status sent");
    }

    json.clear();
}

String SerialProtocol::sanitizeJson(const String& json) {
    String result = json;

    // Replace NaN and Inf with -999
    result.replace("NaN", "-999");
    result.replace("nan", "-999");
    result.replace("Infinity", "-999");
    result.replace("infinity", "-999");
    result.replace("-Infinity", "-999");
    result.replace("-infinity", "-999");

    return result;
}

unsigned long SerialProtocol::calculateJsonHash(const String& json) {
    unsigned long hash = 5381;
    for (size_t i = 0; i < json.length(); i++) {
        hash = ((hash << 5) + hash) + json.charAt(i);
    }
    return hash;
}
