#pragma once

#include <Arduino.h>

// Forward declarations
class FirebaseManager;
class DeviceManager;

// ============================================================================
// OFFLINE ENTRY TYPE
// ============================================================================

enum class OfflineEntryType : uint8_t {
    LOG = 0,
    FAULT_SET = 1,
    FAULT_DELETE = 2
};

// ============================================================================
// OFFLINE QUEUE MANAGER
// ============================================================================
// Stores failed Firebase operations to SPIFFS for later retry.
// Uses JSON Lines format for efficient append/read operations.

class OfflineQueueManager {
public:
    OfflineQueueManager();

    // Initialize with dependencies
    void begin(FirebaseManager* fbManager, DeviceManager* devManager);

    // Queue operations. `key` is only meaningful for FAULT_SET/FAULT_DELETE
    // (the fixed RTDB child key, e.g. "DHT_ERROR") and ignored for LOG.
    bool enqueue(OfflineEntryType type, const String& jsonData, const char* key = nullptr);
    bool hasEntries() const;
    size_t getEntryCount() const;

    // Flush to Firebase (returns true if entry was sent successfully)
    bool flushOne();

    // Clear all queued entries
    void clear();

    // Statistics
    size_t getDroppedCount() const { return droppedCount_; }
    void resetDroppedCount() { droppedCount_ = 0; }

private:
    FirebaseManager* firebaseManager_;
    DeviceManager* deviceManager_;

    // Statistics
    size_t droppedCount_;

    // Helpers
    String buildQueueEntry(OfflineEntryType type, const String& jsonData, const String& key);
    bool parseQueueEntry(const String& line, OfflineEntryType& type, String& jsonData, String& key);
    bool sendToFirebase(OfflineEntryType type, const String& jsonData, const String& key);
    void enforceMaxEntries();
};
