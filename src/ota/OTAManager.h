#pragma once

#include <Arduino.h>
#include <Preferences.h>

class SerialOTAForwarder;

// ============================================================================
// OTA MANAGER
// ============================================================================
// Runs on Core 0 (network task). Every OTA_CHECK_INTERVAL_MS it:
//   1. Polls GitHub Releases API for a new WiFi ESP version
//      → If newer: downloads wifi-firmware.bin and self-flashes via Update.h
//   2. Polls GitHub Releases API for a new Feeder ESP version
//      → If newer: downloads feeder-firmware.bin to SPIFFS
//      → Sets feederUpdateReady() so Core 1 can forward it over Serial2
//
// Versions are stored in NVS under the "ota" namespace.

class OTAManager {
public:
    OTAManager();

    // Call once from setup() after SPIFFS and WiFi are ready
    void begin(SerialOTAForwarder* forwarder);

    // Call from Core 0 network task — checks for updates on a timer
    void tick();

    // Returns true when feeder firmware has been downloaded and is ready to forward
    bool feederUpdateReady() const { return feederUpdateReady_; }

    // Call from Core 1 after SerialOTAForwarder finishes forwarding
    void clearFeederUpdateFlag() { feederUpdateReady_ = false; }

    // Path on SPIFFS where feeder firmware is stored
    const char* getFeederFirmwarePath() const { return "/feeder_ota.bin"; }

private:
    SerialOTAForwarder* forwarder_;
    Preferences prefs_;
    unsigned long lastCheckMs_;
    bool feederUpdateReady_;

    // Check and apply WiFi ESP self-update — returns true if update was applied
    bool checkWiFiUpdate();

    // Check and download Feeder firmware — sets feederUpdateReady_ if newer found
    bool checkFeederUpdate();

    // Fetch latest release info from GitHub.
    // Fills outTag (e.g. "v1.2.3") and outAssetUrl (direct download URL).
    // assetName is the filename to look for in the release assets (e.g. "wifi-firmware.bin").
    // Returns false on network error or if asset not found.
    bool fetchReleaseInfo(const char* repo, String& outTag, String& outAssetUrl,
                          const char* assetName);

    // Download firmware binary via HTTPS and apply it with Update.h.
    // Reboots on success — never returns true.
    bool applyWiFiFirmware(const String& url);

    // Download binary via HTTPS and save to SPIFFS path.
    bool downloadToSPIFFS(const String& url, const char* spiffsPath);

    // Returns true if remoteTag is a higher version than currentTag.
    // Both must be in vMAJOR.MINOR.PATCH format.
    bool isNewer(const String& remoteTag, const String& currentTag);

    // NVS helpers
    String getStoredVersion(const char* key);
    void setStoredVersion(const char* key, const String& tag);

    // Extract a quoted string value from JSON body: finds "key":"VALUE"
    String extractJsonValue(const String& body, const char* key);
};
