#pragma once

#include <Arduino.h>
#include <Firebase_ESP_Client.h>
// Forward declarations
class FirebaseManager;
class DeviceManager;

// ============================================================================
// STREAM MANAGER
// ============================================================================
// Manages Firebase stream for real-time command listening
// Handles command queueing, duplicate prevention, and deletion

class StreamManager {
public:
    StreamManager();

    // Initialize with dependencies
    void begin(FirebaseManager* fbManager, DeviceManager* devManager);

    // Stream lifecycle
    bool startStream();
    bool restartStream();
    void stopStream();
    bool isActive() const;
    void markInactive();  // Called when Firebase reinitializes and clears stream state

    // Command deletion queue processing
    void tick();  // Process queued deletions

    // Set command callback
    typedef void (*CommandCallback)(const String& commandType, const String& commandId);
    void setCommandCallback(CommandCallback callback);

private:
    // Dependencies
    FirebaseManager* firebaseManager_;
    DeviceManager* deviceManager_;
    CommandCallback commandCallback_;

    // State
    bool streamActive_;
    bool streamStarted_;          // True once stream has been started at least once
    unsigned long lastDeleteTime_;
    unsigned long lastRestartAttempt_;

    // Command deletion queue (prevents duplicate processing)
    static const int DELETE_QUEUE_SIZE = 10;
    char deleteQueue_[DELETE_QUEUE_SIZE][120];
    int deleteQueueCount_;

    // Stream callbacks (static for Firebase library)
    static void streamCallbackWrapper(FirebaseStream data);
    static void streamTimeoutCallbackWrapper(bool timeout);

    // Instance pointer for callbacks
    static StreamManager* instance_;

    // Internal processing
    void handleStreamEvent(FirebaseStream& data);
    void handleStreamTimeout(bool timeout);
    void processSingleCommand(FirebaseJson& json, const String& commandId);
    void processMultipleCommands(FirebaseJson& json);
    void queueCommandDeletion(const char* commandId);
    void processDeleteQueue();
    bool isCommandInDeleteQueue(const char* commandId) const;
};
