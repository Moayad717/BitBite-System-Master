#include "SerialOTAForwarder.h"
#include "../core/LogManager.h"
#include "../utils/Watchdog.h"
#include <SPIFFS.h>

static const char* TAG = "SerialOTA";

// ============================================================================
// CONSTRUCTOR
// ============================================================================

SerialOTAForwarder::SerialOTAForwarder() : forwarding_(false) {}

// ============================================================================
// FORWARD — main entry point (blocking)
// ============================================================================

bool SerialOTAForwarder::forward(const char* spiffsPath) {
    if (forwarding_) return false;

    File f = SPIFFS.open(spiffsPath, "r");
    if (!f) {
        LOG_ERROR("[%s] Cannot open %s", TAG, spiffsPath);
        return false;
    }

    size_t totalSize = f.size();
    if (totalSize == 0) {
        LOG_ERROR("[%s] Firmware file is empty: %s", TAG, spiffsPath);
        f.close();
        return false;
    }

    // Calculate whole-file CRC32 for the Feeder's final integrity check
    uint32_t crc = calculateFileCRC32(spiffsPath);
    f.seek(0);

    // Sanity-check: the file must contain exactly what it should before we start.
    // This catches silent SPIFFS write failures where size() diverges from actual data.
    if (totalSize == 0) {
        LOG_ERROR("[%s] Firmware file is empty (size=0): %s", TAG, spiffsPath);
        f.close();
        return false;
    }

    LOG_INFO("[%s] Starting feeder OTA: %u bytes, CRC32=0x%08X", TAG, totalSize, crc);
    forwarding_ = true;

    // Flush any leftover data from normal SerialProtocol operation
    while (Serial2.available()) Serial2.read();

    // ---- Handshake ----
    char startMsg[64];
    snprintf(startMsg, sizeof(startMsg), "OTA_START:%u:%u", totalSize, crc);
    Serial2.println(startMsg);

    String response;
    if (!waitForLine(response, READY_TIMEOUT_MS) || response != "OTA_READY") {
        LOG_ERROR("[%s] No OTA_READY from feeder (got: '%s')", TAG, response.c_str());
        sendAbort();
        f.close();
        forwarding_ = false;
        return false;
    }

    LOG_INFO("[%s] Feeder ready — sending %u chunks", TAG, (totalSize + CHUNK_SIZE - 1) / CHUNK_SIZE);

    // ---- Send chunks ----
    uint8_t buf[CHUNK_SIZE];
    size_t bytesSent = 0;
    size_t totalChunks = (totalSize + CHUNK_SIZE - 1) / CHUNK_SIZE;

    // Iterate by explicit count, not f.available(), so a premature SPIFFS EOF is a
    // hard error with a clear log rather than a silent early OTA_END.
    for (size_t seq = 0; seq < totalChunks; seq++) {
        size_t expected = min((size_t)CHUNK_SIZE, totalSize - bytesSent);
        size_t len = f.read(buf, expected);
        if (len != expected) {
            LOG_ERROR("[%s] SPIFFS read failed at chunk %u: got %u/%u bytes (file truncated?)",
                      TAG, seq, len, expected);
            sendAbort();
            f.close();
            forwarding_ = false;
            return false;
        }

        bool ok = false;
        for (int retry = 0; retry < MAX_RETRIES && !ok; retry++) {
            if (retry > 0) {
                LOG_WARN("[%s] Chunk %u: retry %d", TAG, seq, retry);
            }
            ok = sendChunkAndWaitAck((int)seq, buf, len);
        }

        if (!ok) {
            LOG_ERROR("[%s] Chunk %u failed after %d retries — aborting OTA", TAG, seq, MAX_RETRIES);
            sendAbort();
            f.close();
            forwarding_ = false;
            return false;
        }

        bytesSent += len;

        // Feed watchdog — chunk loop can take minutes at low baud rates
        Watchdog::feed();

        // Progress log at 10% intervals
        if (totalChunks > 0 && (seq + 1) % max((size_t)1, totalChunks / 10) == 0) {
            LOG_INFO("[%s] Progress: %u/%u bytes (%.0f%%)",
                     TAG, bytesSent, totalSize, 100.0f * bytesSent / totalSize);
        }
    }

    f.close();

    // ---- Signal completion ----
    Serial2.println("OTA_END");

    if (!waitForLine(response, DONE_TIMEOUT_MS)) {
        LOG_ERROR("[%s] Timeout waiting for feeder OTA result", TAG);
        sendAbort();
        forwarding_ = false;
        return false;
    }

    if (response == "OTA_OK") {
        LOG_INFO("[%s] Feeder OTA complete! Sent %u chunks (%u bytes)", TAG, totalChunks, bytesSent);
        SPIFFS.remove(spiffsPath);  // Clean up downloaded binary
        forwarding_ = false;
        return true;
    } else {
        LOG_ERROR("[%s] Feeder OTA failed: '%s'", TAG, response.c_str());
        forwarding_ = false;
        return false;
    }
}

// ============================================================================
// CHUNK SEND + ACK
// ============================================================================

bool SerialOTAForwarder::sendChunkAndWaitAck(int seq, const uint8_t* data, size_t len) {
    // Build and send:  OTA_CHUNK:<seq>:<len>:<hexdata>
    String hex;
    bytesToHex(data, len, hex);

    char header[32];
    snprintf(header, sizeof(header), "OTA_CHUNK:%d:%u:", seq, (unsigned)len);

    Serial2.print(header);
    Serial2.print(hex);
    Serial2.println();  // Terminate with \r\n

    // Wait for OTA_ACK:<seq> or OTA_NACK:<seq>
    String response;
    if (!waitForLine(response, CHUNK_TIMEOUT_MS)) {
        LOG_WARN("[%s] Chunk %d: timeout waiting for ACK", TAG, seq);
        return false;
    }

    char expectedAck[24];
    snprintf(expectedAck, sizeof(expectedAck), "OTA_ACK:%d", seq);
    if (response == expectedAck) return true;

    // Could be OTA_NACK:<seq> or garbage
    LOG_WARN("[%s] Chunk %d: unexpected response '%s'", TAG, seq, response.c_str());
    return false;
}

// ============================================================================
// SERIAL READ HELPERS
// ============================================================================

bool SerialOTAForwarder::waitForLine(String& out, uint32_t timeoutMs) {
    out = "";
    unsigned long deadline = millis() + timeoutMs;

    while (millis() < deadline) {
        if (Serial2.available()) {
            char c = (char)Serial2.read();
            if (c == '\n') {
                out.trim();
                return !out.isEmpty();
            }
            if (c != '\r') out += c;
        } else {
            // DONE_TIMEOUT_MS equals the hardware watchdog timeout, and this
            // loop is the only code running on Core 1 while it spins — feed
            // here or a slow-but-successful final ACK can still cost us a
            // watchdog reset right as the transfer was about to finish.
            Watchdog::feed();
            yield();  // Let FreeRTOS scheduler breathe
        }
    }

    return false;
}

// ============================================================================
// ABORT
// ============================================================================

void SerialOTAForwarder::sendAbort() {
    Serial2.println("OTA_ABORT");
}

// ============================================================================
// UTILITIES
// ============================================================================

void SerialOTAForwarder::bytesToHex(const uint8_t* data, size_t len, String& out) {
    static const char HEX_CHARS[] = "0123456789ABCDEF";
    out = "";
    out.reserve(len * 2);
    for (size_t i = 0; i < len; i++) {
        out += HEX_CHARS[data[i] >> 4];
        out += HEX_CHARS[data[i] & 0x0F];
    }
}

uint32_t SerialOTAForwarder::calculateFileCRC32(const char* path) {
    File f = SPIFFS.open(path, "r");
    if (!f) return 0;

    uint32_t crc = 0xFFFFFFFF;
    uint8_t buf[256];

    while (f.available()) {
        size_t len = f.readBytes((char*)buf, sizeof(buf));
        for (size_t i = 0; i < len; i++) {
            crc ^= buf[i];
            for (int j = 0; j < 8; j++) {
                crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
            }
        }
    }

    f.close();
    return ~crc;
}
