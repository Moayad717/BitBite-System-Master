#pragma once

#include <Arduino.h>

// ============================================================================
// SERIAL OTA FORWARDER
// ============================================================================
// Runs on Core 1 (main loop). Forwards a firmware binary stored in SPIFFS
// to the Feeder ESP over Serial2.
//
// Protocol (text-based, newline-terminated — consistent with SerialProtocol):
//
//   WiFi ESP  →  Feeder ESP
//   ─────────────────────────────────────────────────────────────
//   OTA_START:<total_bytes>:<crc32>        Begin OTA session
//   OTA_CHUNK:<seq>:<len>:<hexdata>        Send a firmware chunk
//   OTA_END                                All chunks sent
//
//   Feeder ESP  →  WiFi ESP
//   ─────────────────────────────────────────────────────────────
//   OTA_READY                              Ready to receive chunks
//   OTA_ACK:<seq>                          Chunk accepted (or NACK to retry)
//   OTA_NACK:<seq>                         Chunk failed, please resend
//   OTA_OK                                 Firmware verified and applied
//   OTA_ERROR:<reason>                     OTA failed
//
// Notes:
//   - Chunk size: 256 bytes -> 512 hex chars per chunk
//   - Max retries per chunk: 3
//   - Watchdog is fed after every chunk ACK
//   - Normal SerialProtocol operation must be paused while forwarding

class SerialOTAForwarder {
public:
    SerialOTAForwarder();

    // Blocking call — reads from SPIFFS and sends to Feeder ESP over Serial2.
    // Returns true on success, false on error. Deletes the SPIFFS file on success.
    // DO NOT call serialProtocol.tick() while this is running.
    bool forward(const char* spiffsPath);

    bool isForwarding() const { return forwarding_; }

private:
    bool forwarding_;

    static const size_t  CHUNK_SIZE       = 256;   // Bytes per chunk (512 hex chars)
    static const int     MAX_RETRIES      = 3;
    static const uint32_t CHUNK_TIMEOUT_MS = 5000;  // Wait up to 5s for ACK per chunk
    static const uint32_t READY_TIMEOUT_MS = 10000; // Wait up to 10s for OTA_READY
    static const uint32_t DONE_TIMEOUT_MS  = 30000; // Wait up to 30s for OTA_OK/OTA_ERROR

    // Sends one chunk and waits for ACK/NACK. Returns true on ACK.
    bool sendChunkAndWaitAck(int seq, const uint8_t* data, size_t len);

    // Reads a newline-terminated response from Serial2.
    bool waitForLine(String& out, uint32_t timeoutMs);

    // Converts bytes to uppercase hex string (no separator)
    void bytesToHex(const uint8_t* data, size_t len, String& out);

    // CRC32 of entire SPIFFS file (for end-to-end integrity check by Feeder)
    uint32_t calculateFileCRC32(const char* path);
};
