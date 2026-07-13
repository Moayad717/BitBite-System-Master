#include "WiFiConnectionManager.h"
#include "../core/LogManager.h"
#include "../config/TimingConfig.h"
#include "../utils/Backoff.h"

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

    // Custom captive portal styling — set once on this persistent WiFiManager
    // instance, so it applies to both autoConnect() below and any later
    // startConfigPortal() call.
    wifiManager_.setCustomHeadElement(
        "<style>"
        "body{background:#1C1612;color:#F5F0E8;font-family:sans-serif;margin:0;padding:10px;}"
        ".wrap{background:#2D2420;border-radius:12px;padding:20px;max-width:400px;margin:auto;}"
        "h1,h2{color:#D4860A;}"
        "input{background:#1C1612;color:#F5F0E8;border:1px solid #5C4A3A;border-radius:6px;padding:8px;width:100%;box-sizing:border-box;}"
        "button,input[type=submit]{background:#D4860A;color:#1C1612;border:none;border-radius:8px;padding:10px;width:100%;font-weight:bold;cursor:pointer;}"
        "button:hover,input[type=submit]:hover{background:#E8960B;}"
        "a{color:#D4860A;}"
        "div{box-sizing:border-box;}"
        "</style>"
    );

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

        if (Backoff::ready(now, lastReconnectAttempt_, WIFI_RECONNECT_INTERVAL)) {
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

    // Force max TX power (19.5dBm ceiling on this chip) and disable modem
    // sleep. Both require STA to actually be running to take effect - calling
    // them in begin() right after WiFi.mode(WIFI_STA) was too early (silently
    // failed with "Neither AP or STA has been started"). Re-applied on every
    // (re)connection since they don't reliably survive a WiFi.disconnect(true).
    WiFi.setTxPower(WIFI_POWER_19_5dBm);
    WiFi.setSleep(false);

    // Keep the DHCP-assigned IP/gateway/subnet but force a fast, reliable
    // public DNS resolver instead of the router's own. A slow/flaky router
    // DNS resolver can hang a lookup (e.g. securetoken.googleapis.com) well
    // past what the Firebase library's own request timeouts bound, which is
    // a plausible contributor to networkTask holding Core 0 long enough to
    // starve the idle task and trip the hardware watchdog. Re-applied on
    // every (re)connection since a full WiFi.disconnect(true) clears it.
    WiFi.config(WiFi.localIP(), WiFi.gatewayIP(), WiFi.subnetMask(),
                IPAddress(8, 8, 8, 8), IPAddress(1, 1, 1, 1));

    LOG_INFO("WiFi connected!");
    LOG_INFO("  SSID: %s", WiFi.SSID().c_str());
    LOG_INFO("  IP: %s", WiFi.localIP().toString().c_str());
    LOG_INFO("  RSSI: %d dBm", WiFi.RSSI());
}

void WiFiConnectionManager::handleDisconnection() {
    connected_ = false;
    LOG_WARN("WiFi disconnected");
}
