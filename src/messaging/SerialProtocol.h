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
//     - FEED_NOW, TARE, CLEAR_FAULTS, GET_SCHEDULE_STATUS (commands)
//
//   RX (Feeding → WiFi):
//     - {json} (status update)
//     - LOG:{json} (log entry)
//     - FAULT:{json} (fault event)
//     - SCHEDULE_HASH:12345 (schedule sync confirmation)
//     - SCHEDULE_STATUS:... / SCHEDULE_ITEM:... / SCHEDULE_STATUS:END (multi-line)

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

    // Schedule status collection state
    struct ScheduleStatusState {
        bool collecting;
        String headerJson;          // Parsed header info (date, time, day, count)
        String itemsJson;           // Accumulated schedule items as JSON array
        int itemCount;
        unsigned long startTime;
    } scheduleStatusState_;

    // Message handling
    void handleIncomingData();
    void processMessage(const String& message);
    void handleStatusUpdate(const String& statusJson);
    void handleLogEntry(const String& logJson);
    void handleFaultEntry(const String& faultJson);
    void handleScheduleHash(const String& hash);
    void handleScheduleStatusHeader(const String& header);
    void handleScheduleItem(const String& item);
    void handleScheduleStatusEnd();
    void sendScheduleStatusToFirebase();

    // Helpers
    void sendScheduleSyncStatus(bool success, const char* message);
    String sanitizeJson(const String& json);
    unsigned long calculateJsonHash(const String& json);
};
