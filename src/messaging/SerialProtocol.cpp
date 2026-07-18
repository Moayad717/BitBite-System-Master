#include "SerialProtocol.h"
#include "../connectivity/FirebaseManager.h"
#include "../core/DeviceManager.h"
#include "../core/TimeManager.h"
#include "../core/LogManager.h"
#include "../core/DualCoreManager.h"
#include "../storage/OfflineQueueManager.h"
#include "../config/TimingConfig.h"
#include "../config/Version.h"

// ============================================================================
// CONSTRUCTOR
// ============================================================================

SerialProtocol::SerialProtocol()
    : firebaseManager_(nullptr),
      deviceManager_(nullptr),
      timeManager_(nullptr),
      offlineQueue_(nullptr),
      statusCallback_(nullptr),
      lineIdx_(0),
      discarding_(false) {

    scheduleSyncState_.waitingForConfirmation = false;
    scheduleSyncState_.expectedHash = "";
    scheduleSyncState_.syncTime = 0;

    timeSyncState_.waitingForConfirmation = false;
    timeSyncState_.syncTime = 0;
    timeSyncState_.retryCount = 0;

    lineBuf_[0] = '\0';
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

    if (!timeManager_->isSynced()) {
        LOG_WARN("NTP not synced - skipping TIME send to Feeding ESP");
        return;
    }

    char timestamp[32];
    deviceManager_->getTimestamp(timestamp, sizeof(timestamp));

    Serial2.printf("TIME:%s\n", timestamp);
    LOG_INFO("Time sent to Feeding ESP: %s", timestamp);

    timeSyncState_.waitingForConfirmation = true;
    timeSyncState_.syncTime = millis();
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

    // Check for time sync timeout — retry automatically since there's no
    // app/user action that can trigger a resend of TIME:.
    if (timeSyncState_.waitingForConfirmation &&
        (millis() - timeSyncState_.syncTime >= TIME_SYNC_TIMEOUT)) {
        timeSyncState_.waitingForConfirmation = false;

        if (timeSyncState_.retryCount < TIME_SYNC_MAX_RETRIES) {
            timeSyncState_.retryCount++;
            LOG_WARN("Time sync confirmation timed out - retrying (%u/%u)",
                     timeSyncState_.retryCount, TIME_SYNC_MAX_RETRIES);
            sendTime();
        } else {
            LOG_ERROR("Time sync failed after %u attempts - Feeding ESP RTC may be unsynced",
                       TIME_SYNC_MAX_RETRIES);
        }
    }

    while (Serial2.available()) {
        char c = (char)Serial2.read();

        if (discarding_) {
            // Resyncing after an overflow — keep discarding across as many
            // calls as it takes until we actually see a newline.
            if (c == '\n') discarding_ = false;
            continue;
        }

        if (c == '\n') {
            lineBuf_[lineIdx_] = '\0';

            if (lineIdx_ > 0) {
                String message = String(lineBuf_);
                message.trim();  // strips the trailing '\r' from Serial2.println() on the Feeder side

                if (message.length() > 0) {
                    LOG_DEBUG("Serial RX: %s", message.c_str());
                    processMessage(message);
                }
            }

            lineIdx_ = 0;
            return;  // One line per tick() — keeps Core 1's loop responsive
        }

        if (lineIdx_ < MAX_LINE_LEN - 1) {
            lineBuf_[lineIdx_++] = c;
        } else {
            // Line too long (noise/corruption with no '\n') — discard and
            // resync at the next real newline, however many calls that
            // takes, instead of only checking bytes already buffered.
            LOG_WARN("Serial RX line too long (>%u bytes) — discarding (resyncing)", (unsigned)MAX_LINE_LEN);
            lineIdx_ = 0;
            discarding_ = true;
        }
    }
}

void SerialProtocol::processMessage(const String& message) {
    if (message.startsWith("LOG:")) {
        handleLogEntry(message.substring(4));
    } else if (message.startsWith("FAULT_SET:")) {
        handleFaultSet(message.substring(10));
    } else if (message.startsWith("FAULT_CLEAR:")) {
        handleFaultClear(message.substring(12));
    } else if (message.startsWith("SCHEDULE_HASH:")) {
        handleScheduleHash(message.substring(14));
    } else if (message.startsWith("TIME_ACK")) {
        handleTimeAck();
    } else if (message.startsWith("SCHEDULE_EXECUTED:")) {
        handleScheduleExecuted(message.substring(18));
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

    // The Feeder ESP reports its own version as "firmwareVersion" — rename
    // it to feederFirmwareVersion so it doesn't collide with wifiFirmwareVersion below.
    FirebaseJsonData versionData;
    if (json.get(versionData, "firmwareVersion")) {
        json.set("feederFirmwareVersion", versionData.stringValue);
        json.remove("firmwareVersion");
    }

    char timestamp[25];
    deviceManager_->getUTCTimestamp(timestamp, sizeof(timestamp));
    json.set("isOnline", true);
    json.set("wifiSignal", WiFi.RSSI());
    json.set("lastSeen", timestamp);
    json.set("wifiFirmwareVersion", FIRMWARE_VERSION);
    json.set("wifiFreeHeap", (int)ESP.getFreeHeap());
    json.set("wifiUptimeMs", (double)millis());
    json.set("wifiResetReason", deviceManager_->getResetReasonString());

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

    // Timestamp is already embedded by the feeder ESP — do not overwrite it.
    if (!enqueueFirebaseWrite(QueueItemType::LOG, sanitized.c_str())) {
        LOG_WARN("Failed to queue log entry");
    }
}

void SerialProtocol::handleFaultSet(const String& payload) {
    // payload = "<key>:<json>" — split on the FIRST colon since the json
    // body itself contains colons.
    int colon = payload.indexOf(':');
    if (colon <= 0) {
        LOG_WARN("Malformed FAULT_SET (no key/json separator): %s", payload.c_str());
        return;
    }

    String key = payload.substring(0, colon);
    String faultJson = payload.substring(colon + 1);
    String sanitized = sanitizeJson(faultJson);
    LOG_WARN("Fault SET from Feeding ESP: %s", key.c_str());

    // Timestamp is already embedded by the feeder ESP — do not overwrite it.
    if (!enqueueFirebaseWrite(QueueItemType::FAULT_SET, sanitized.c_str(), key.c_str())) {
        LOG_WARN("Failed to queue fault set");
    }
}

void SerialProtocol::handleFaultClear(const String& key) {
    String trimmedKey = key;
    trimmedKey.trim();
    if (trimmedKey.length() == 0) {
        LOG_WARN("Malformed FAULT_CLEAR (empty key)");
        return;
    }

    LOG_INFO("Fault CLEAR from Feeding ESP: %s", trimmedKey.c_str());

    if (!enqueueFirebaseWrite(QueueItemType::FAULT_DELETE, "", trimmedKey.c_str())) {
        LOG_WARN("Failed to queue fault clear");
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

void SerialProtocol::handleTimeAck() {
    if (timeSyncState_.waitingForConfirmation) {
        LOG_INFO("Time sync verified by Feeding ESP (%u retries)", timeSyncState_.retryCount);
        timeSyncState_.waitingForConfirmation = false;
        timeSyncState_.retryCount = 0;
    }
}

void SerialProtocol::handleScheduleExecuted(const String& scheduleId) {
    String trimmedId = scheduleId;
    trimmedId.trim();
    if (trimmedId.length() == 0) {
        LOG_WARN("Malformed SCHEDULE_EXECUTED (empty scheduleId)");
        return;
    }

    LOG_INFO("Schedule executed: %s", trimmedId.c_str());

    if (!enqueueFirebaseWrite(QueueItemType::SCHEDULE_EXECUTED, "", trimmedId.c_str())) {
        LOG_WARN("Failed to queue schedule executed update");
    }
}

// ============================================================================
// HELPERS
// ============================================================================

void SerialProtocol::sendScheduleSyncStatus(bool success, const char* message) {
    FirebaseJson json;
    json.set("success", success);
    json.set("message", message);

    char timestamp[25];
    deviceManager_->getTimestamp(timestamp, sizeof(timestamp));
    json.set("timestamp", timestamp);

    String jsonStr;
    json.toString(jsonStr);
    json.clear();

    // Route through Core 0 queue — never call Firebase directly from Core 1
    if (!enqueueFirebaseWrite(QueueItemType::SYNC_STATUS, jsonStr.c_str())) {
        LOG_WARN("Failed to queue schedule sync status");
    }
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
