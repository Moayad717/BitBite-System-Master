#pragma once

#include <Arduino.h>

// Forward declarations
class FirebaseManager;
class DeviceManager;
class TimeManager;
class OfflineQueueManager;

// ============================================================================
// SERIAL PROTOCOL
// ============================================================================
// Handles Serial2 communication with Feeding ESP
// Protocol:
//   TX (WiFi → Feeding):
//     - TIME:YYYY-MM-DD HH:MM:SS
//     - NAME:Display Name
//     - SCHEDULES:{json}
//     - FEED_NOW, TARE, CLEAR_FAULTS (commands)
//
//   RX (Feeding → WiFi):
//     - {json} (status update)
//     - LOG:{json} (log entry)
//     - FAULT_SET:<key>:{json} (fault active — set()'d to devices/{id}/faults/{key})
//     - FAULT_CLEAR:<key> (fault resolved — deleted from devices/{id}/faults/{key})
//     - SCHEDULE_HASH:12345 (schedule sync confirmation)
//     - TIME_ACK (time sync confirmation — TIME: is retried automatically
//       on timeout, up to TIME_SYNC_MAX_RETRIES, if this isn't seen)
//     - SCHEDULE_EXECUTED:<scheduleId> (a scheduled feed ran — sets
//       devices/{id}/schedules/{scheduleId}/executedToday = true)

class SerialProtocol {
public:
    SerialProtocol();

    // Initialize with dependencies
    void begin(FirebaseManager* fbManager, DeviceManager* devManager, TimeManager* timeManager,
               OfflineQueueManager* offlineQueue = nullptr);

    // Periodic tick (call from main loop)
    void tick();

    // Send messages to Feeding ESP
    void sendTime();
    void sendDisplayName();
    void syncSchedules();

    // Set callback for status updates
    typedef void (*StatusUpdateCallback)(const String& statusJson);
    void setStatusUpdateCallback(StatusUpdateCallback callback);

private:
    // Dependencies
    FirebaseManager* firebaseManager_;
    DeviceManager* deviceManager_;
    TimeManager* timeManager_;
    OfflineQueueManager* offlineQueue_;
    StatusUpdateCallback statusCallback_;

    // Schedule sync state
    struct ScheduleSyncState {
        bool waitingForConfirmation;
        String expectedHash;
        unsigned long syncTime;
    } scheduleSyncState_;

    // Time sync state — TIME: gets no reply loop-back other than TIME_ACK,
    // so unlike schedules there's no content to re-verify, just delivery.
    struct TimeSyncState {
        bool waitingForConfirmation;
        unsigned long syncTime;
        uint8_t retryCount;
    } timeSyncState_;

    // Incoming line buffer — fixed size, non-blocking (replaces the old
    // Serial2.readStringUntil() which had no length bound and blocked the
    // whole Core 1 loop for up to 1s per call while waiting for '\n').
    static const size_t MAX_LINE_LEN = 1024;
    char lineBuf_[MAX_LINE_LEN];
    size_t lineIdx_;

    // True while resyncing after an overflow — ignore all bytes until the
    // next real newline, even if that takes multiple tick() calls.
    bool discarding_;

    // Message handling
    void handleIncomingData();
    void processMessage(const String& message);
    void handleStatusUpdate(const String& statusJson);
    void handleLogEntry(const String& logJson);
    void handleFaultSet(const String& payload);
    void handleFaultClear(const String& key);
    void handleScheduleHash(const String& hash);
    void handleTimeAck();
    void handleScheduleExecuted(const String& scheduleId);

    // Helpers
    void sendScheduleSyncStatus(bool success, const char* message);
    String sanitizeJson(const String& json);
    unsigned long calculateJsonHash(const String& json);
};
