#pragma once

#include <Arduino.h>
#include <stdarg.h>

// ============================================================================
// LOG LEVELS
// ============================================================================

enum class LogLevel {
    DEBUG = 0,      // Verbose diagnostics (disabled in production)
    INFO = 1,       // Informational messages
    WARN = 2,       // Warnings (recoverable issues)
    ERROR = 3,      // Errors (operation failed but system continues)
    CRITICAL = 4    // Critical (system integrity compromised)
};

// ============================================================================
// LOG ENTRY
// ============================================================================

struct LogEntry {
    LogLevel level;
    char message[256];
    unsigned long timestamp;
    const char* file;
    int line;
};

// ============================================================================
// LOG MANAGER (Singleton)
// ============================================================================

class LogManager {
public:
    // Singleton instance
    static LogManager& getInstance();

    // Initialize logger
    void begin();

    // Logging methods
    void log(LogLevel level, const char* file, int line, const char* format, ...);
    void vlog(LogLevel level, const char* file, int line, const char* format, va_list args);

    // Convenience methods
    void debug(const char* file, int line, const char* format, ...);
    void info(const char* file, int line, const char* format, ...);
    void warn(const char* file, int line, const char* format, ...);
    void error(const char* file, int line, const char* format, ...);
    void critical(const char* file, int line, const char* format, ...);

    // Configuration
    void setLogLevel(LogLevel minLevel);
    void enableSerialLogging(bool enable);
    void enableRemoteLogging(bool enable);

    // Ring buffer for debugging
    void dumpRecentLogs();
    const LogEntry* getRecentLogs(size_t& count);

private:
    // Singleton - private constructor
    LogManager();
    ~LogManager() = default;
    LogManager(const LogManager&) = delete;
    LogManager& operator=(const LogManager&) = delete;

    // Configuration
    LogLevel minLevel_;
    bool serialLogging_;
    bool remoteLogging_;

    // Ring buffer for recent logs (last 50)
    static const size_t RING_BUFFER_SIZE = 50;
    LogEntry ringBuffer_[RING_BUFFER_SIZE];
    size_t ringBufferIndex_;

    // Helper methods
    void addToRingBuffer(const LogEntry& entry);
    void logToSerial(const LogEntry& entry);
    const char* levelToString(LogLevel level);
    const char* levelToColorCode(LogLevel level);
};

// ============================================================================
// LOGGING MACROS (include file and line info)
// ============================================================================

#define LOG_DEBUG(...) LogManager::getInstance().debug(__FILE__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...) LogManager::getInstance().info(__FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARN(...) LogManager::getInstance().warn(__FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...) LogManager::getInstance().error(__FILE__, __LINE__, __VA_ARGS__)
#define LOG_CRITICAL(...) LogManager::getInstance().critical(__FILE__, __LINE__, __VA_ARGS__)

// Generic log with custom level
#define LOG(level, ...) LogManager::getInstance().log(level, __FILE__, __LINE__, __VA_ARGS__)
