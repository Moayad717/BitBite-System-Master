#pragma once

#include <Arduino.h>

// ============================================================================
// SPIFFS HELPER
// ============================================================================
// Low-level SPIFFS abstraction with conditional compilation.
// Compiles to stubs when DISABLE_FLASH is defined.

#ifndef DISABLE_FLASH

#include <SPIFFS.h>

class SPIFFSHelper {
public:
    // Initialize SPIFFS filesystem
    static bool begin();

    // Check if SPIFFS is ready
    static bool isReady();

    // File operations
    static bool appendLine(const char* filename, const String& line);
    static String readFirstLine(const char* filename);
    static bool removeFirstLine(const char* filename);
    static size_t getLineCount(const char* filename);
    static size_t getFileSize(const char* filename);
    static bool deleteFile(const char* filename);
    static bool fileExists(const char* filename);

    // Storage info
    static size_t getTotalBytes();
    static size_t getUsedBytes();
    static size_t getFreeBytes();

private:
    static bool initialized_;
};

#else

// ============================================================================
// STUB IMPLEMENTATION (when SPIFFS disabled)
// ============================================================================

class SPIFFSHelper {
public:
    static bool begin() { return false; }
    static bool isReady() { return false; }
    static bool appendLine(const char*, const String&) { return false; }
    static String readFirstLine(const char*) { return ""; }
    static bool removeFirstLine(const char*) { return false; }
    static size_t getLineCount(const char*) { return 0; }
    static size_t getFileSize(const char*) { return 0; }
    static bool deleteFile(const char*) { return false; }
    static bool fileExists(const char*) { return false; }
    static size_t getTotalBytes() { return 0; }
    static size_t getUsedBytes() { return 0; }
    static size_t getFreeBytes() { return 0; }
};

#endif
