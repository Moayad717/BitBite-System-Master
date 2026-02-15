#include "SPIFFSHelper.h"

#ifndef DISABLE_FLASH

#include "../core/LogManager.h"

// ============================================================================
// STATIC MEMBERS
// ============================================================================

bool SPIFFSHelper::initialized_ = false;

// ============================================================================
// INITIALIZATION
// ============================================================================

bool SPIFFSHelper::begin() {
    if (initialized_) {
        return true;
    }

    if (!SPIFFS.begin(false)) {  // Try mounting without formatting first
        LOG_WARN("SPIFFS mount failed - formatting (queued offline data will be lost)");
        if (!SPIFFS.begin(true)) {  // Format as last resort
            LOG_ERROR("SPIFFS format failed");
            return false;
        }
    }

    initialized_ = true;
    LOG_INFO("SPIFFS initialized: %u/%u bytes used", getUsedBytes(), getTotalBytes());
    return true;
}

bool SPIFFSHelper::isReady() {
    return initialized_;
}

// ============================================================================
// FILE OPERATIONS
// ============================================================================

bool SPIFFSHelper::appendLine(const char* filename, const String& line) {
    if (!initialized_) return false;

    File file = SPIFFS.open(filename, FILE_APPEND);
    if (!file) {
        LOG_ERROR("Failed to open %s for append", filename);
        return false;
    }

    file.println(line);
    file.close();
    return true;
}

String SPIFFSHelper::readFirstLine(const char* filename) {
    if (!initialized_) return "";

    File file = SPIFFS.open(filename, FILE_READ);
    if (!file) {
        return "";
    }

    String line = file.readStringUntil('\n');
    line.trim();
    file.close();
    return line;
}

bool SPIFFSHelper::removeFirstLine(const char* filename) {
    if (!initialized_) return false;

    // Read all lines except the first
    File file = SPIFFS.open(filename, FILE_READ);
    if (!file) {
        return false;
    }

    // Skip first line
    file.readStringUntil('\n');

    // Read remaining content
    String remaining = file.readString();
    file.close();

    // Rewrite file without first line
    file = SPIFFS.open(filename, FILE_WRITE);
    if (!file) {
        LOG_ERROR("Failed to rewrite %s", filename);
        return false;
    }

    if (remaining.length() > 0) {
        file.print(remaining);
    }
    file.close();
    return true;
}

size_t SPIFFSHelper::getLineCount(const char* filename) {
    if (!initialized_) return 0;

    File file = SPIFFS.open(filename, FILE_READ);
    if (!file) {
        return 0;
    }

    size_t count = 0;
    while (file.available()) {
        file.readStringUntil('\n');
        count++;
    }
    file.close();
    return count;
}

size_t SPIFFSHelper::getFileSize(const char* filename) {
    if (!initialized_) return 0;

    File file = SPIFFS.open(filename, FILE_READ);
    if (!file) {
        return 0;
    }

    size_t size = file.size();
    file.close();
    return size;
}

bool SPIFFSHelper::deleteFile(const char* filename) {
    if (!initialized_) return false;

    if (!SPIFFS.exists(filename)) {
        return true;  // Already doesn't exist
    }

    return SPIFFS.remove(filename);
}

bool SPIFFSHelper::fileExists(const char* filename) {
    if (!initialized_) return false;
    return SPIFFS.exists(filename);
}

// ============================================================================
// STORAGE INFO
// ============================================================================

size_t SPIFFSHelper::getTotalBytes() {
    if (!initialized_) return 0;
    return SPIFFS.totalBytes();
}

size_t SPIFFSHelper::getUsedBytes() {
    if (!initialized_) return 0;
    return SPIFFS.usedBytes();
}

size_t SPIFFSHelper::getFreeBytes() {
    if (!initialized_) return 0;
    return SPIFFS.totalBytes() - SPIFFS.usedBytes();
}

#endif  // DISABLE_FLASH
