#pragma once

#include <Arduino.h>
#include <Firebase_ESP_Client.h>

// ============================================================================
// FIREBASE MANAGER
// ============================================================================
// Manages Firebase Realtime Database connection and operations

class FirebaseManager {
public:
    FirebaseManager();

    // Initialize Firebase
    bool begin(const char* apiKey, const char* databaseUrl);

    // Reinitialize (for reconnection after WiFi loss)
    bool reinitialize();

    // Check connection and handle reconnection
    void tick();

    // Connection status
    bool isReady() const;
    bool isAuthenticated() const;

    // Database operations (thread-safe)
    bool setJSON(const char* path, FirebaseJson* json);
    bool updateJSON(const char* path, FirebaseJson* json);
    bool pushJSON(const char* path, FirebaseJson* json);
    bool getJSON(const char* path, FirebaseJson* json);
    bool getString(const char* path, String& outValue);
    bool deleteNode(const char* path);
    bool getShallowData(const char* path);

    // Stream operations
    bool beginStream(const char* path,
                    FirebaseData::StreamEventCallback callback,
                    FirebaseData::StreamTimeoutCallback timeoutCallback);
    bool endStream();
    bool readStream();

    // Error handling
    String getLastError() const;
    void clearError();

    // Get Firebase instances (for advanced operations)
    FirebaseData& getMainInstance() { return fbdo_; }
    FirebaseData& getStreamInstance() { return streamFbdo_; }
    FirebaseData& getCmdInstance() { return cmdFbdo_; }

private:
    // Firebase instances
    FirebaseAuth auth_;
    FirebaseConfig config_;
    FirebaseData fbdo_;         // Main operations
    FirebaseData streamFbdo_;   // Stream operations
    FirebaseData cmdFbdo_;      // Command operations (large data)

    // State
    bool initialized_;
    bool authenticated_;
    unsigned long lastReconnectAttempt_;
    String lastError_;

    // Connection helpers
    bool checkInternetConnection();
    void handleAuthStateChange(bool wasReady, bool isReady);
};
