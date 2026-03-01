#include "OTAManager.h"
#include "SerialOTAForwarder.h"
#include "../core/LogManager.h"
#include "../utils/Watchdog.h"
#include "../config/Credentials.h"
#include "../config/Version.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Update.h>
#include <SPIFFS.h>

static const char* TAG = "OTA";

// ============================================================================
// CONSTRUCTOR / BEGIN
// ============================================================================

OTAManager::OTAManager()
    : forwarder_(nullptr), lastCheckMs_(0), feederUpdateReady_(false) {}

void OTAManager::begin(SerialOTAForwarder* forwarder) {
    forwarder_ = forwarder;
    prefs_.begin("ota", false);
    LOG_INFO("[%s] Initialized. Running firmware: %s", TAG, FIRMWARE_VERSION);
}

// ============================================================================
// TICK — called from Core 0 network task every OTA_CHECK_INTERVAL_MS
// ============================================================================

void OTAManager::tick() {
    unsigned long now = millis();
    if (now - lastCheckMs_ < OTA_CHECK_INTERVAL_MS) return;
    lastCheckMs_ = now;

    LOG_INFO("[%s] Checking GitHub for firmware updates...", TAG);

    // Self-update check first — if applied, device reboots here and never returns
    checkWiFiUpdate();

    // Feeder update (only if not already downloaded and waiting)
    if (!feederUpdateReady_) {
        checkFeederUpdate();
    }
}

// ============================================================================
// WIFI ESP SELF-UPDATE
// ============================================================================

bool OTAManager::checkWiFiUpdate() {
    String remoteTag, downloadUrl;
    if (!fetchReleaseInfo(OTA_WIFI_REPO, remoteTag, downloadUrl, "wifi-firmware.bin")) {
        LOG_WARN("[%s] WiFi: could not fetch release info", TAG);
        return false;
    }

    String current = String(FIRMWARE_VERSION);
    LOG_INFO("[%s] WiFi: current=%s  latest=%s", TAG, current.c_str(), remoteTag.c_str());

    if (!isNewer(remoteTag, current)) {
        LOG_INFO("[%s] WiFi firmware is up to date", TAG);
        return false;
    }

    LOG_INFO("[%s] WiFi update available! %s -> %s", TAG, current.c_str(), remoteTag.c_str());
    return applyWiFiFirmware(downloadUrl);  // Reboots on success
}

// ============================================================================
// FEEDER ESP UPDATE (downloads to SPIFFS — forwarding happens on Core 1)
// ============================================================================

bool OTAManager::checkFeederUpdate() {
    // Skip if feeder repo not configured
    if (strlen(OTA_FEEDER_REPO) == 0 ||
        strncmp(OTA_FEEDER_REPO, "Moayad717/YOUR-FEEDER-REPO", 26) == 0) {
        return false;
    }

    String remoteTag, downloadUrl;
    if (!fetchReleaseInfo(OTA_FEEDER_REPO, remoteTag, downloadUrl, "feeder-firmware.bin")) {
        LOG_WARN("[%s] Feeder: could not fetch release info", TAG);
        return false;
    }

    String stored = getStoredVersion("feeder_ver");
    LOG_INFO("[%s] Feeder: stored=%s  latest=%s", TAG, stored.c_str(), remoteTag.c_str());

    if (!isNewer(remoteTag, stored)) {
        LOG_INFO("[%s] Feeder firmware is up to date", TAG);
        return false;
    }

    LOG_INFO("[%s] Feeder update available! %s -> %s", TAG, stored.c_str(), remoteTag.c_str());

    if (downloadToSPIFFS(downloadUrl, getFeederFirmwarePath())) {
        setStoredVersion("feeder_ver", remoteTag);
        feederUpdateReady_ = true;
        LOG_INFO("[%s] Feeder firmware ready in SPIFFS, waiting for Serial2 window", TAG);
        return true;
    }

    return false;
}

// ============================================================================
// GITHUB RELEASES API
// ============================================================================

bool OTAManager::fetchReleaseInfo(const char* repo, String& outTag, String& outUrl,
                                   const char* assetName) {
    String apiUrl = String("https://api.github.com/repos/") + repo + "/releases/latest";

    WiFiClientSecure client;
    client.setInsecure();  // Skip cert verification — acceptable for firmware polling
    HTTPClient http;
    http.begin(client, apiUrl);
    http.addHeader("Accept", "application/vnd.github+json");
    http.addHeader("User-Agent", "ESP32-BitBite-OTA/1.0");
    if (strlen(OTA_GITHUB_TOKEN) > 0) {
        http.addHeader("Authorization", String("Bearer ") + OTA_GITHUB_TOKEN);
    }
    http.setTimeout(15000);

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        LOG_WARN("[%s] GitHub API returned HTTP %d for repo %s", TAG, code, repo);
        http.end();
        return false;
    }

    String body = http.getString();
    http.end();

    // Extract tag_name
    outTag = extractJsonValue(body, "tag_name");
    if (outTag.isEmpty()) {
        LOG_WARN("[%s] Could not parse tag_name from release JSON", TAG);
        return false;
    }

    // Find the named asset and extract its download URL
    // The JSON has "assets": [{ "name": "wifi-firmware.bin", "browser_download_url": "..." }]
    String searchName = String("\"") + assetName + "\"";
    int assetPos = body.indexOf(searchName);
    if (assetPos < 0) {
        LOG_WARN("[%s] Asset '%s' not found in release assets", TAG, assetName);
        return false;
    }

    outUrl = extractJsonValue(body.substring(assetPos), "browser_download_url");
    if (outUrl.isEmpty()) {
        LOG_WARN("[%s] Could not parse browser_download_url for %s", TAG, assetName);
        return false;
    }

    return true;
}

// ============================================================================
// WIFI SELF-FLASH
// ============================================================================

bool OTAManager::applyWiFiFirmware(const String& url) {
    LOG_INFO("[%s] Downloading WiFi firmware from GitHub...", TAG);

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.begin(client, url);
    http.addHeader("User-Agent", "ESP32-BitBite-OTA/1.0");
    if (strlen(OTA_GITHUB_TOKEN) > 0) {
        http.addHeader("Authorization", String("Bearer ") + OTA_GITHUB_TOKEN);
    }
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(60000);

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        LOG_ERROR("[%s] Firmware download failed: HTTP %d", TAG, code);
        http.end();
        return false;
    }

    int totalSize = http.getSize();
    LOG_INFO("[%s] Firmware size: %d bytes", TAG, totalSize);

    if (!Update.begin(totalSize > 0 ? totalSize : UPDATE_SIZE_UNKNOWN)) {
        LOG_ERROR("[%s] Update.begin() failed: %s", TAG, Update.errorString());
        http.end();
        return false;
    }

    // Write firmware in chunks, feeding the watchdog each iteration so
    // Core 1's loopTask keeps the WDT alive during the multi-second download.
    WiFiClient* stream = http.getStreamPtr();
    uint8_t buf[1024];
    size_t written = 0;
    size_t lastLogAt = 0;
    unsigned long lastDataMs = millis();

    while (written < (size_t)totalSize) {
        if (stream->available()) {
            size_t remaining = (size_t)totalSize - written;
            size_t toRead = min((size_t)stream->available(), min(sizeof(buf), remaining));
            size_t bytesRead = stream->readBytes(buf, toRead);
            if (bytesRead > 0) {
                Update.write(buf, bytesRead);
                written += bytesRead;
                lastDataMs = millis();
                Watchdog::feed();

                // Log progress every 128KB
                if (written - lastLogAt >= 131072) {
                    lastLogAt = written;
                    int pct = (int)(100 * written / totalSize);
                    LOG_INFO("[%s] WiFi OTA: %u / %d bytes (%d%%)", TAG, written, totalSize, pct);
                }
            }
        } else {
            if (!http.connected()) break;  // Connection closed — exit even if short
            if (millis() - lastDataMs > 15000) {
                LOG_ERROR("[%s] Download stalled — aborting", TAG);
                Update.abort();
                http.end();
                return false;
            }
            delay(1);
        }
    }

    if ((int)written != totalSize) {
        LOG_WARN("[%s] Wrote %u / %d bytes", TAG, written, totalSize);
    }

    if (!Update.end() || !Update.isFinished()) {
        LOG_ERROR("[%s] Update.end() failed: %s", TAG, Update.errorString());
        http.end();
        return false;
    }

    http.end();
    LOG_INFO("[%s] WiFi firmware applied successfully — rebooting now", TAG);
    delay(500);
    ESP.restart();
    return true;  // Never reached
}

// ============================================================================
// DOWNLOAD FEEDER FIRMWARE TO SPIFFS
// ============================================================================

bool OTAManager::downloadToSPIFFS(const String& url, const char* spiffsPath) {
    // Check available SPIFFS space
    size_t freeBytes = SPIFFS.totalBytes() - SPIFFS.usedBytes();
    LOG_INFO("[%s] SPIFFS free: %u bytes", TAG, freeBytes);

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.begin(client, url);
    http.addHeader("User-Agent", "ESP32-BitBite-OTA/1.0");
    if (strlen(OTA_GITHUB_TOKEN) > 0) {
        http.addHeader("Authorization", String("Bearer ") + OTA_GITHUB_TOKEN);
    }
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(60000);

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        LOG_ERROR("[%s] Feeder firmware download failed: HTTP %d", TAG, code);
        http.end();
        return false;
    }

    int totalSize = http.getSize();
    if (totalSize > 0 && (size_t)totalSize > freeBytes) {
        LOG_ERROR("[%s] Not enough SPIFFS space: need %d, have %u", TAG, totalSize, freeBytes);
        http.end();
        return false;
    }

    File f = SPIFFS.open(spiffsPath, "w");
    if (!f) {
        LOG_ERROR("[%s] Failed to open %s for writing", TAG, spiffsPath);
        http.end();
        return false;
    }

    WiFiClient* stream = http.getStreamPtr();
    uint8_t buf[512];
    int totalWritten = 0;
    int lastLogAt = 0;
    unsigned long lastDataMs = millis();

    while (totalSize <= 0 || totalWritten < totalSize) {
        if (stream->available()) {
            size_t remaining = totalSize > 0 ? (size_t)(totalSize - totalWritten) : sizeof(buf);
            size_t toRead = min((size_t)stream->available(), min(sizeof(buf), remaining));
            size_t bytesRead = stream->readBytes(buf, toRead);
            if (bytesRead > 0) {
                f.write(buf, bytesRead);
                totalWritten += bytesRead;
                lastDataMs = millis();
                Watchdog::feed();

                // Log progress every 128KB
                if (totalWritten - lastLogAt >= 131072) {
                    lastLogAt = totalWritten;
                    int pct = totalSize > 0 ? (int)(100 * totalWritten / totalSize) : 0;
                    LOG_INFO("[%s] Feeder OTA: %d / %d bytes (%d%%)", TAG, totalWritten, totalSize, pct);
                }
            }
        } else {
            if (!http.connected()) break;
            if (millis() - lastDataMs > 15000) {
                LOG_ERROR("[%s] Feeder download stalled — aborting", TAG);
                f.close();
                SPIFFS.remove(spiffsPath);
                http.end();
                return false;
            }
            delay(1);
        }
    }

    f.close();
    http.end();

    if (totalWritten == 0) {
        LOG_ERROR("[%s] Download produced 0 bytes", TAG);
        SPIFFS.remove(spiffsPath);
        return false;
    }

    LOG_INFO("[%s] Feeder firmware saved: %d bytes -> %s", TAG, totalWritten, spiffsPath);
    return true;
}

// ============================================================================
// VERSION COMPARISON
// ============================================================================

bool OTAManager::isNewer(const String& remoteTag, const String& currentTag) {
    // Local/unversioned builds always pull the latest
    if (currentTag.isEmpty() || currentTag == "v0.0.0-local" || currentTag == "v0.0.0") {
        return true;
    }
    if (remoteTag == currentTag) return false;

    auto parse = [](const String& tag, int& major, int& minor, int& patch) {
        String t = tag.startsWith("v") ? tag.substring(1) : tag;
        int d1 = t.indexOf('.');
        int d2 = t.lastIndexOf('.');
        if (d1 < 0) { major = t.toInt(); minor = 0; patch = 0; return; }
        major = t.substring(0, d1).toInt();
        if (d1 == d2) { minor = t.substring(d1 + 1).toInt(); patch = 0; return; }
        minor = t.substring(d1 + 1, d2).toInt();
        patch = t.substring(d2 + 1).toInt();
    };

    int rMaj, rMin, rPat, cMaj, cMin, cPat;
    parse(remoteTag, rMaj, rMin, rPat);
    parse(currentTag, cMaj, cMin, cPat);

    if (rMaj != cMaj) return rMaj > cMaj;
    if (rMin != cMin) return rMin > cMin;
    return rPat > cPat;
}

// ============================================================================
// NVS HELPERS
// ============================================================================

String OTAManager::getStoredVersion(const char* key) {
    return prefs_.getString(key, "v0.0.0");
}

void OTAManager::setStoredVersion(const char* key, const String& tag) {
    prefs_.putString(key, tag);
}

// ============================================================================
// JSON PARSING
// ============================================================================

String OTAManager::extractJsonValue(const String& body, const char* key) {
    // Finds the pattern  "key":"VALUE"  and returns VALUE
    String search = String("\"") + key + "\":\"";
    int idx = body.indexOf(search);
    if (idx < 0) return "";
    idx += search.length();
    int end = body.indexOf('"', idx);
    if (end < 0) return "";
    return body.substring(idx, end);
}
