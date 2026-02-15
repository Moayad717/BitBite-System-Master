#include "FirebaseManager.h"
#include "../core/LogManager.h"
#include "../config/TimingConfig.h"
#include "../config/BufferConfig.h"
#include "addons/TokenHelper.h"
#include "WiFiConnectionManager.h"
#include "../messaging/StreamManager.h"

// External references (defined in main.cpp)
extern WiFiConnectionManager wifiManager;
extern StreamManager streamManager;

// ============================================================================
// CONSTRUCTOR
// ============================================================================

FirebaseManager::FirebaseManager()
    : initialized_(false),
      authenticated_(false),
      lastReconnectAttempt_(0) {
}

// ============================================================================
// INITIALIZATION
// ============================================================================

bool FirebaseManager::begin(const char* apiKey, const char* databaseUrl) {
    LOG_INFO("Initializing Firebase...");

    if (!checkInternetConnection()) {
        LOG_ERROR("No internet connection - Firebase initialization failed");
        return false;
    }

    // Configure Firebase
    config_.api_key = apiKey;
    config_.database_url = databaseUrl;
    config_.token_status_callback = tokenStatusCallback;
    config_.timeout.serverResponse = 10000;
    config_.timeout.socketConnection = 10000;

    // Begin Firebase
    Firebase.begin(&config_, &auth_);
    Firebase.reconnectWiFi(true);

    // Configure buffer sizes
    fbdo_.setResponseSize(FIREBASE_RESPONSE_SIZE);
    fbdo_.setBSSLBufferSize(FIREBASE_SSL_BUFFER_SIZE, FIREBASE_SSL_RECEIVE_BUFFER);

    cmdFbdo_.setResponseSize(FIREBASE_RESPONSE_SIZE);
    cmdFbdo_.setBSSLBufferSize(FIREBASE_SSL_BUFFER_SIZE, FIREBASE_SSL_RECEIVE_BUFFER);

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
    LOG_INFO("Reinitializing Firebase...");

    if (!checkInternetConnection()) {
        LOG_ERROR("No internet connection");
        return false;
    }

    // Notify StreamManager that stream state is about to be cleared
    streamManager.markInactive();

    // Clear all instances
    fbdo_.clear();
    streamFbdo_.clear();
    cmdFbdo_.clear();

    // Reset Firebase
    Firebase.begin(&config_, &auth_);

    // Reconfigure buffer sizes
    fbdo_.setResponseSize(FIREBASE_RESPONSE_SIZE);
    fbdo_.setBSSLBufferSize(FIREBASE_SSL_BUFFER_SIZE, FIREBASE_SSL_RECEIVE_BUFFER);
    cmdFbdo_.setResponseSize(FIREBASE_RESPONSE_SIZE);
    cmdFbdo_.setBSSLBufferSize(FIREBASE_SSL_BUFFER_SIZE, FIREBASE_SSL_RECEIVE_BUFFER);

    // Wait for authentication (reduced from 20s to 5s to avoid blocking Core 0 too long)
    LOG_INFO("Waiting for Firebase authentication...");
    unsigned long start = millis();

    while (!Firebase.ready() && millis() - start < 5000) {
        delay(250);
    }

    if (Firebase.ready()) {
        LOG_INFO("Firebase reinitialized successfully");
        authenticated_ = true;
        return true;
    }

    LOG_ERROR("Firebase reinitialization failed");
    LOG_ERROR("Error: %s", fbdo_.errorReason().c_str());
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
        handleAuthStateChange(authenticated_, isReady);
    }

    // Attempt reconnection if not ready (only when WiFi is connected)
    if (!isReady && wifiManager.isConnected()) {
        unsigned long now = millis();

        if (now - lastReconnectAttempt_ >= FIREBASE_RECONNECT_INTERVAL) {
            lastReconnectAttempt_ = now;
            LOG_INFO("Attempting Firebase reconnection...");
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
