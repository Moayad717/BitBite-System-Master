#include "WiFiConnectionManager.h"
#include "../core/LogManager.h"
#include "../config/TimingConfig.h"

// ============================================================================
// CONSTRUCTOR
// ============================================================================

WiFiConnectionManager::WiFiConnectionManager()
    : connected_(false),
      lastReconnectAttempt_(0) {
}

// ============================================================================
// INITIALIZATION
// ============================================================================

bool WiFiConnectionManager::begin() {
    LOG_INFO("Initializing WiFi...");

    // Configure WiFi mode
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(false);  // We handle reconnection manually
    WiFi.persistent(true);
    delay(100);

    // Configure WiFiManager
    wifiManager_.setConfigPortalTimeout(240);  // 4 minutes
    wifiManager_.setConnectTimeout(30);        // 30 seconds per attempt
    wifiManager_.setConnectRetries(3);         // Retry 3 times

    // Disable debug output to prevent LED blinking issues
    wifiManager_.setDebugOutput(false);

    // Generate unique AP name from MAC
    String apName = "BitBite-" + String((uint32_t)ESP.getEfuseMac(), HEX);

    LOG_INFO("Starting WiFi connection (AP: %s)...", apName.c_str());

    // Give WiFi hardware time to stabilize
    delay(500);

    // Attempt connection (opens portal if no saved credentials)
    if (wifiManager_.autoConnect(apName.c_str(), "bitbite123")) {
        handleConnection();
        return true;
    }

    LOG_ERROR("WiFi connection failed");
    return false;
}

// ============================================================================
// PERIODIC MAINTENANCE
// ============================================================================

void WiFiConnectionManager::tick() {
    bool isConnected = (WiFi.status() == WL_CONNECTED);

    // Connection state changed
    if (isConnected != connected_) {
        if (isConnected) {
            handleConnection();
        } else {
            handleDisconnection();
        }
    }

    // Periodic reconnection attempt
    if (!isConnected) {
        unsigned long now = millis();

        if (now - lastReconnectAttempt_ >= WIFI_RECONNECT_INTERVAL) {
            lastReconnectAttempt_ = now;
            reconnect();
        }
    }
}

// ============================================================================
// CONNECTION STATUS
// ============================================================================

bool WiFiConnectionManager::isConnected() const {
    return connected_ && (WiFi.status() == WL_CONNECTED);
}

int WiFiConnectionManager::getRSSI() const {
    if (!isConnected()) {
        return 0;
    }
    return WiFi.RSSI();
}

String WiFiConnectionManager::getSSID() const {
    if (!isConnected()) {
        return "";
    }
    return WiFi.SSID();
}

IPAddress WiFiConnectionManager::getLocalIP() const {
    return WiFi.localIP();
}

// ============================================================================
// RECONNECTION
// ============================================================================

void WiFiConnectionManager::reconnect() {
    LOG_INFO("Attempting WiFi reconnection...");

    // Properly disconnect before reconnecting
    WiFi.disconnect(true);  // true = wifioff
    delay(500);
    WiFi.mode(WIFI_STA);
    delay(100);
    WiFi.begin();  // Reconnect with saved credentials
}

void WiFiConnectionManager::startConfigPortal() {
    LOG_INFO("Starting configuration portal...");

    String apName = "BitBite-" + String((uint32_t)ESP.getEfuseMac(), HEX);
    wifiManager_.startConfigPortal(apName.c_str(), "bitbite123");
}

// ============================================================================
// EVENT HANDLERS
// ============================================================================

void WiFiConnectionManager::handleConnection() {
    connected_ = true;

    LOG_INFO("WiFi connected!");
    LOG_INFO("  SSID: %s", WiFi.SSID().c_str());
    LOG_INFO("  IP: %s", WiFi.localIP().toString().c_str());
    LOG_INFO("  RSSI: %d dBm", WiFi.RSSI());
}

void WiFiConnectionManager::handleDisconnection() {
    connected_ = false;
    LOG_WARN("WiFi disconnected");
}
