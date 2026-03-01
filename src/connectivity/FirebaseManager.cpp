#include "FirebaseManager.h"
#include "../core/LogManager.h"
#include "../config/TimingConfig.h"
#include "../config/BufferConfig.h"
#include "addons/TokenHelper.h"

// ============================================================================
// CONSTRUCTOR
// ============================================================================

FirebaseManager::FirebaseManager()
    : initialized_(false),
      authenticated_(false),
      lastReconnectAttempt_(0),
      reconnectFailures_(0),
      wifiCheckCallback_(nullptr),
      streamInactiveCallback_(nullptr) {
}

void FirebaseManager::setWiFiCheckCallback(WiFiCheckCallback callback) {
    wifiCheckCallback_ = callback;
}

void FirebaseManager::setStreamInactiveCallback(StreamInactiveCallback callback) {
    streamInactiveCallback_ = callback;
}

// ============================================================================
// INITIALIZATION
// ============================================================================

bool FirebaseManager::begin(const char* apiKey, const char* databaseUrl) {
    LOG_INFO("Initializing Firebase...");

    // Configure Firebase
    config_.api_key = apiKey;
    config_.database_url = databaseUrl;
    config_.token_status_callback = tokenStatusCallback;
    config_.timeout.serverResponse = 10000;
    config_.timeout.socketConnection = 10000;

    // Anonymous sign-up (required by Firebase library for API key auth)
    Firebase.signUp(&config_, &auth_, "", "");

    // Begin Firebase
    Firebase.begin(&config_, &auth_);

    // Configure buffer sizes
    fbdo_.setResponseSize(FIREBASE_RESPONSE_SIZE);
    fbdo_.setBSSLBufferSize(FIREBASE_SSL_BUFFER_SIZE, FIREBASE_SSL_RECEIVE_BUFFER);

    cmdFbdo_.setResponseSize(FIREBASE_RESPONSE_SIZE);
    cmdFbdo_.setBSSLBufferSize(FIREBASE_SSL_BUFFER_SIZE, FIREBASE_SSL_RECEIVE_BUFFER);

    Firebase.reconnectWiFi(true);

    LOG_INFO("Waiting for Firebase authentication...");
    unsigned long authStart = millis();

    while (!Firebase.ready() && millis() - authStart < 15000) {
        Serial.print(".");
        delay(500);
    }
    Serial.println();

    if (Firebase.ready()) {
        LOG_INFO("Firebase connected and authenticated");
        initialized_ = true;
        authenticated_ = true;
        return true;
    }

    LOG_ERROR("Firebase authentication timeout");
    lastError_ = "Authentication timeout";
    return false;
}

bool FirebaseManager::reinitialize() {
    LOG_INFO("Reinitializing Firebase... (attempt #%u)", reconnectFailures_ + 1);

    // Notify StreamManager that stream state is about to be cleared
    if (streamInactiveCallback_) {
        streamInactiveCallback_();
    }

    // Step 1: Stop the persistent stream — closes the SSL/TCP session and
    // sets the internal stream_stop flag so the callback task exits cleanly.
    Firebase.RTDB.endStream(&streamFbdo_);

    // Step 2: Clear internal state of all FirebaseData objects
    streamFbdo_.clear();
    fbdo_.clear();
    cmdFbdo_.clear();

    // Step 3: Reset auth token state. This wipes the cached token and forces
    // the library to perform a fresh token exchange on the next begin().
    // Without this, a stale/expired token is silently reused.
    Firebase.reset(&config_);

    // Step 4: Re-run anonymous sign-up, then begin.
    // signUp must precede begin() — this is a library contract for anonymous auth.
    Firebase.signUp(&config_, &auth_, "", "");
    Firebase.begin(&config_, &auth_);

    // Step 5: Restore buffer sizes (cleared when objects were cleared)
    fbdo_.setResponseSize(FIREBASE_RESPONSE_SIZE);
    fbdo_.setBSSLBufferSize(FIREBASE_SSL_BUFFER_SIZE, FIREBASE_SSL_RECEIVE_BUFFER);
    cmdFbdo_.setResponseSize(FIREBASE_RESPONSE_SIZE);
    cmdFbdo_.setBSSLBufferSize(FIREBASE_SSL_BUFFER_SIZE, FIREBASE_SSL_RECEIVE_BUFFER);

    // Step 6: Wait for authentication — 10 s gives the token exchange time to
    // complete without blocking Core 0 for too long
    LOG_INFO("Waiting for Firebase authentication...");
    unsigned long start = millis();

    while (!Firebase.ready() && millis() - start < 10000) {
        delay(250);
    }

    if (Firebase.ready()) {
        LOG_INFO("Firebase reinitialized successfully");
        authenticated_ = true;
        reconnectFailures_ = 0;
        return true;
    }

    reconnectFailures_++;
    LOG_ERROR("Firebase reinitialization failed (attempt #%u): %s",
              reconnectFailures_, fbdo_.errorReason().c_str());
    lastError_ = fbdo_.errorReason();
    authenticated_ = false;
    return false;
}

// ============================================================================
// PERIODIC MAINTENANCE
// ============================================================================

void FirebaseManager::tick() {
    if (!initialized_) {
        return;
    }

    bool isReady = Firebase.ready();

    // Handle authentication state changes
    if (isReady != authenticated_) {
        if (isReady && !authenticated_) {
            reconnectFailures_ = 0;  // Reset backoff counter on successful reconnect
        }
        handleAuthStateChange(authenticated_, isReady);
    }

    // Attempt reconnection if not ready (only when WiFi is connected)
    if (!isReady && wifiCheckCallback_ && wifiCheckCallback_()) {
        unsigned long now = millis();

        // Exponential backoff: FIREBASE_RECONNECT_INTERVAL * 2^failures, capped at 5 min.
        // Prevents hammering Firebase on repeated failures.
        unsigned long backoffMs = FIREBASE_RECONNECT_INTERVAL;
        for (uint8_t i = 0; i < reconnectFailures_ && backoffMs < 300000UL; i++) {
            backoffMs *= 2;
        }
        if (backoffMs > 300000UL) backoffMs = 300000UL;

        if (now - lastReconnectAttempt_ >= backoffMs) {
            lastReconnectAttempt_ = now;
            LOG_INFO("Attempting Firebase reconnection (backoff: %lus)...", backoffMs / 1000);
            reinitialize();
        }
    }
}

// ============================================================================
// CONNECTION STATUS
// ============================================================================

bool FirebaseManager::isReady() const {
    return initialized_ && Firebase.ready();
}

bool FirebaseManager::isAuthenticated() const {
    return authenticated_;
}

// ============================================================================
// DATABASE OPERATIONS
// ============================================================================

bool FirebaseManager::setJSON(const char* path, FirebaseJson* json) {
    if (!json) {
        lastError_ = "Null JSON pointer";
        return false;
    }
    if (!isReady()) {
        lastError_ = "Firebase not ready";
        return false;
    }

    if (Firebase.RTDB.setJSON(&fbdo_, path, json)) {
        fbdo_.clear();
        return true;
    }

    lastError_ = fbdo_.errorReason();
    LOG_ERROR("Firebase setJSON failed: %s", lastError_.c_str());
    fbdo_.clear();
    return false;
}

bool FirebaseManager::updateJSON(const char* path, FirebaseJson* json) {
    if (!json) {
        lastError_ = "Null JSON pointer";
        return false;
    }
    if (!isReady()) {
        lastError_ = "Firebase not ready";
        return false;
    }

    if (Firebase.RTDB.updateNode(&fbdo_, path, json)) {
        fbdo_.clear();
        return true;
    }

    lastError_ = fbdo_.errorReason();
    LOG_ERROR("Firebase updateJSON failed: %s", lastError_.c_str());
    fbdo_.clear();
    return false;
}

bool FirebaseManager::pushJSON(const char* path, FirebaseJson* json) {
    if (!json) {
        lastError_ = "Null JSON pointer";
        return false;
    }
    if (!isReady()) {
        lastError_ = "Firebase not ready";
        return false;
    }

    if (Firebase.RTDB.pushJSON(&fbdo_, path, json)) {
        fbdo_.clear();
        return true;
    }

    lastError_ = fbdo_.errorReason();
    LOG_ERROR("Firebase pushJSON failed: %s", lastError_.c_str());
    fbdo_.clear();
    return false;
}

bool FirebaseManager::getJSON(const char* path, FirebaseJson* json) {
    if (!json) {
        lastError_ = "Null JSON pointer";
        return false;
    }
    if (!isReady()) {
        lastError_ = "Firebase not ready";
        return false;
    }

    if (Firebase.RTDB.getJSON(&fbdo_, path)) {
        *json = fbdo_.to<FirebaseJson>();
        fbdo_.clear();
        return true;
    }

    lastError_ = fbdo_.errorReason();
    LOG_ERROR("Firebase getJSON failed: %s", lastError_.c_str());
    fbdo_.clear();
    return false;
}

bool FirebaseManager::getString(const char* path, String& outValue) {
    if (!isReady()) {
        lastError_ = "Firebase not ready";
        return false;
    }

    if (Firebase.RTDB.getString(&cmdFbdo_, path)) {
        outValue = cmdFbdo_.stringData();
        cmdFbdo_.clear();
        return true;
    }

    lastError_ = cmdFbdo_.errorReason();
    LOG_ERROR("Firebase getString failed: %s", lastError_.c_str());
    cmdFbdo_.clear();
    return false;
}

bool FirebaseManager::deleteNode(const char* path) {
    if (!isReady()) {
        lastError_ = "Firebase not ready";
        return false;
    }

    if (Firebase.RTDB.deleteNode(&fbdo_, path)) {
        fbdo_.clear();
        return true;
    }

    lastError_ = fbdo_.errorReason();
    LOG_ERROR("Firebase deleteNode failed: %s", lastError_.c_str());
    fbdo_.clear();
    return false;
}

bool FirebaseManager::getShallowData(const char* path) {
    if (!isReady()) {
        lastError_ = "Firebase not ready";
        return false;
    }

    if (Firebase.RTDB.getShallowData(&fbdo_, path)) {
        fbdo_.clear();
        return true;
    }

    lastError_ = fbdo_.errorReason();
    fbdo_.clear();
    return false;
}

// ============================================================================
// STREAM OPERATIONS
// ============================================================================

bool FirebaseManager::beginStream(const char* path,
                                  FirebaseData::StreamEventCallback callback,
                                  FirebaseData::StreamTimeoutCallback timeoutCallback) {
    if (!isReady()) {
        LOG_ERROR("Firebase not ready for stream setup");
        return false;
    }

    LOG_INFO("Setting up Firebase stream: %s", path);

    streamFbdo_.setBSSLBufferSize(STREAM_SSL_BUFFER_SIZE, STREAM_SSL_RECEIVE_BUFFER);
    streamFbdo_.setResponseSize(STREAM_RESPONSE_SIZE);

    if (!Firebase.RTDB.beginStream(&streamFbdo_, path)) {
        lastError_ = streamFbdo_.errorReason();
        LOG_ERROR("Stream setup failed: %s", lastError_.c_str());
        return false;
    }

    Firebase.RTDB.setStreamCallback(&streamFbdo_, callback, timeoutCallback);

    LOG_INFO("Stream initialized successfully");
    return true;
}

bool FirebaseManager::endStream() {
    Firebase.RTDB.endStream(&streamFbdo_);
    streamFbdo_.clear();
    LOG_INFO("Stream ended");
    return true;
}

bool FirebaseManager::readStream() {
    if (!isReady()) {
        return false;
    }

    return Firebase.RTDB.readStream(&streamFbdo_);
}

// ============================================================================
// ERROR HANDLING
// ============================================================================

String FirebaseManager::getLastError() const {
    return lastError_;
}

void FirebaseManager::clearError() {
    lastError_ = "";
}

// ============================================================================
// HELPER METHODS
// ============================================================================

bool FirebaseManager::checkInternetConnection() {
    IPAddress result;

    // Try to resolve common hostnames to verify internet connectivity
    if (WiFi.hostByName("www.google.com", result)) {
        return true;
    }

    // Fallback: Try cloudflare
    if (WiFi.hostByName("cloudflare.com", result)) {
        return true;
    }

    return false;
}

void FirebaseManager::handleAuthStateChange(bool wasReady, bool isReady) {
    if (isReady && !wasReady) {
        LOG_INFO("Firebase authentication restored");
        authenticated_ = true;
    } else if (!isReady && wasReady) {
        LOG_WARN("Firebase authentication lost");
        authenticated_ = false;
    }
}
