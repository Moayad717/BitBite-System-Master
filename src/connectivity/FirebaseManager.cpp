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
// ANONYMOUS AUTH TOKEN PERSISTENCE
// ============================================================================
// Avoids creating a brand new anonymous Firebase user on every boot/reconnect
// by saving the refresh token to NVS and restoring from it via
// Firebase.setIdToken() instead of calling Firebase.signUp() again.

String FirebaseManager::loadSavedRefreshToken() {
    tokenPrefs_.begin("firebase", true);  // Read-only
    String token = tokenPrefs_.getString("refresh_token", "");
    tokenPrefs_.end();
    return token;
}

void FirebaseManager::saveRefreshToken(const String& token) {
    if (token.length() == 0) {
        return;  // Nothing meaningful to persist
    }
    tokenPrefs_.begin("firebase", false);  // Read-write
    tokenPrefs_.putString("refresh_token", token);
    tokenPrefs_.end();
    LOG_DEBUG("Firebase refresh token saved to NVS");
}

void FirebaseManager::clearSavedRefreshToken() {
    tokenPrefs_.begin("firebase", false);
    tokenPrefs_.remove("refresh_token");
    tokenPrefs_.end();
}

bool FirebaseManager::startAuth() {
    String savedToken = loadSavedRefreshToken();

    if (savedToken.length() > 0) {
        LOG_INFO("Found saved refresh token - restoring session (skipping signUp)");
        Firebase.setIdToken(&config_, "", 3600, savedToken.c_str());
        Firebase.begin(&config_, &auth_);
        return true;
    }

    LOG_INFO("No saved refresh token - creating new anonymous user");
    Firebase.signUp(&config_, &auth_, "", "");
    Firebase.begin(&config_, &auth_);
    return false;
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

    // Restore from a saved refresh token if we have one; otherwise a fresh
    // anonymous signUp() (also calls Firebase.begin() internally).
    bool usingSavedToken = startAuth();

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
        saveRefreshToken(Firebase.getRefreshToken());
        return true;
    }

    // The saved token didn't work (revoked, expired beyond refresh, etc.) —
    // fall back to a fresh anonymous sign-up rather than leaving the device
    // permanently unable to authenticate.
    if (usingSavedToken) {
        LOG_WARN("Saved refresh token failed to authenticate - falling back to signUp()");
        clearSavedRefreshToken();

        Firebase.signUp(&config_, &auth_, "", "");
        Firebase.begin(&config_, &auth_);

        authStart = millis();
        while (!Firebase.ready() && millis() - authStart < 15000) {
            Serial.print(".");
            delay(500);
        }
        Serial.println();

        if (Firebase.ready()) {
            LOG_INFO("Firebase connected and authenticated (fresh sign-up after saved-token failure)");
            initialized_ = true;
            authenticated_ = true;
            saveRefreshToken(Firebase.getRefreshToken());
            return true;
        }
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

    // Step 4: Restore from a saved refresh token if we have one; otherwise a
    // fresh anonymous signUp(). Either way this also calls Firebase.begin().
    bool usingSavedToken = startAuth();

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
        saveRefreshToken(Firebase.getRefreshToken());
        return true;
    }

    // Step 7: Saved token didn't work — fall back to a fresh anonymous
    // sign-up and retry once within the same reinitialize() call.
    if (usingSavedToken) {
        LOG_WARN("Saved refresh token failed to authenticate - falling back to signUp()");
        clearSavedRefreshToken();

        Firebase.signUp(&config_, &auth_, "", "");
        Firebase.begin(&config_, &auth_);

        start = millis();
        while (!Firebase.ready() && millis() - start < 10000) {
            delay(250);
        }

        if (Firebase.ready()) {
            LOG_INFO("Firebase reinitialized successfully (fresh sign-up after saved-token failure)");
            authenticated_ = true;
            reconnectFailures_ = 0;
            saveRefreshToken(Firebase.getRefreshToken());
            return true;
        }
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

bool FirebaseManager::setBool(const char* path, bool value) {
    if (!isReady()) {
        lastError_ = "Firebase not ready";
        return false;
    }

    if (Firebase.RTDB.setBool(&fbdo_, path, value)) {
        fbdo_.clear();
        return true;
    }

    lastError_ = fbdo_.errorReason();
    LOG_ERROR("Firebase setBool failed: %s", lastError_.c_str());
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

void FirebaseManager::handleAuthStateChange(bool wasReady, bool isReady) {
    if (isReady && !wasReady) {
        LOG_INFO("Firebase authentication restored");
        authenticated_ = true;
    } else if (!isReady && wasReady) {
        LOG_WARN("Firebase authentication lost");
        authenticated_ = false;
    }
}
