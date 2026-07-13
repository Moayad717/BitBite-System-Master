#pragma once

#include <Arduino.h>
#include <Firebase_ESP_Client.h>
#include <Preferences.h>

// ============================================================================
// FIREBASE MANAGER
// ============================================================================
// Manages Firebase Realtime Database connection and operations

class FirebaseManager {
public:
    // Callback types for dependency injection (avoids extern references)
    typedef bool (*WiFiCheckCallback)();
    typedef void (*StreamInactiveCallback)();

    FirebaseManager();

    // Set callbacks (call before begin)
    void setWiFiCheckCallback(WiFiCheckCallback callback);
    void setStreamInactiveCallback(StreamInactiveCallback callback);

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
    bool setBool(const char* path, bool value);

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
    uint8_t reconnectFailures_;   // Consecutive reinit failures (drives backoff)
    String lastError_;

    // Callbacks
    WiFiCheckCallback wifiCheckCallback_;
    StreamInactiveCallback streamInactiveCallback_;

    // Anonymous auth token persistence — avoids creating a new anonymous
    // Firebase user on every boot/reconnect. Separate NVS namespace from
    // anything else in this codebase.
    Preferences tokenPrefs_;
    String loadSavedRefreshToken();
    void saveRefreshToken(const String& token);
    void clearSavedRefreshToken();

    // Starts a Firebase auth attempt: restores from a saved refresh token if
    // one exists, otherwise a fresh anonymous signUp(). Does not wait for
    // Firebase.ready() - callers keep their own existing wait-loop. Returns
    // true if a saved token was used (so the caller knows whether a signUp()
    // fallback + retry is warranted if this attempt doesn't authenticate).
    bool startAuth();

    // Connection helpers
    void handleAuthStateChange(bool wasReady, bool isReady);
};
