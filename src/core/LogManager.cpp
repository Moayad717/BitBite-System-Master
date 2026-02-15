#include "LogManager.h"
#include "DualCoreManager.h"

// ============================================================================
// SINGLETON INSTANCE
// ============================================================================

LogManager& LogManager::getInstance() {
    static LogManager instance;
    return instance;
}

// ============================================================================
// CONSTRUCTOR
// ============================================================================

LogManager::LogManager()
    : minLevel_(LogLevel::INFO),
      serialLogging_(true),
      remoteLogging_(false),
      ringBufferIndex_(0) {
    // Initialize ring buffer
    for (size_t i = 0; i < RING_BUFFER_SIZE; i++) {
        ringBuffer_[i].level = LogLevel::INFO;
        ringBuffer_[i].message[0] = '\0';
        ringBuffer_[i].timestamp = 0;
        ringBuffer_[i].file = nullptr;
        ringBuffer_[i].line = 0;
    }
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void LogManager::begin() {
    // Set log level based on build flags
    #ifdef LOG_LEVEL_DEBUG
        minLevel_ = LogLevel::DEBUG;
    #elif defined(LOG_LEVEL_INFO)
        minLevel_ = LogLevel::INFO;
    #elif defined(LOG_LEVEL_WARN)
        minLevel_ = LogLevel::WARN;
    #elif defined(LOG_LEVEL_ERROR)
        minLevel_ = LogLevel::ERROR;
    #else
        minLevel_ = LogLevel::INFO;  // Default
    #endif

    serialLogging_ = true;
    remoteLogging_ = false;

    Serial.println("=== LogManager Initialized ===");
    Serial.printf("Log Level: %s\n", levelToString(minLevel_));
    Serial.printf("Serial Logging: %s\n", serialLogging_ ? "Enabled" : "Disabled");
    Serial.printf("Remote Logging: %s\n", remoteLogging_ ? "Enabled" : "Disabled");
    Serial.println("===============================");
}

// ============================================================================
// LOGGING METHODS
// ============================================================================

void LogManager::log(LogLevel level, const char* file, int line, const char* format, ...) {
    // Check if this log level should be processed
    if (level < minLevel_) {
        return;
    }

    va_list args;
    va_start(args, format);
    vlog(level, file, line, format, args);
    va_end(args);
}

void LogManager::vlog(LogLevel level, const char* file, int line, const char* format, va_list args) {
    // Check if this log level should be processed
    if (level < minLevel_) {
        return;
    }

    // Create log entry
    LogEntry entry;
    entry.level = level;
    entry.timestamp = millis();
    entry.file = file;
    entry.line = line;

    // Format message
    vsnprintf(entry.message, sizeof(entry.message), format, args);

    // Thread-safe: protect ring buffer and serial output from both cores
    bool haveMutex = false;
    if (logMutex) {
        haveMutex = (xSemaphoreTake(logMutex, pdMS_TO_TICKS(50)) == pdTRUE);
    }

    // Add to ring buffer (always, for debugging)
    addToRingBuffer(entry);

    // Log to serial
    if (serialLogging_) {
        logToSerial(entry);
    }

    if (haveMutex) {
        xSemaphoreGive(logMutex);
    }

    // TODO: Remote logging to Firebase (Phase 2)
    if (remoteLogging_) {
        // Will implement in Phase 2 with Firebase integration
    }
}

// Convenience methods
void LogManager::debug(const char* file, int line, const char* format, ...) {
    va_list args;
    va_start(args, format);
    vlog(LogLevel::DEBUG, file, line, format, args);
    va_end(args);
}

void LogManager::info(const char* file, int line, const char* format, ...) {
    va_list args;
    va_start(args, format);
    vlog(LogLevel::INFO, file, line, format, args);
    va_end(args);
}

void LogManager::warn(const char* file, int line, const char* format, ...) {
    va_list args;
    va_start(args, format);
    vlog(LogLevel::WARN, file, line, format, args);
    va_end(args);
}

void LogManager::error(const char* file, int line, const char* format, ...) {
    va_list args;
    va_start(args, format);
    vlog(LogLevel::ERROR, file, line, format, args);
    va_end(args);
}

void LogManager::critical(const char* file, int line, const char* format, ...) {
    va_list args;
    va_start(args, format);
    vlog(LogLevel::CRITICAL, file, line, format, args);
    va_end(args);
}

// ============================================================================
// CONFIGURATION
// ============================================================================

void LogManager::setLogLevel(LogLevel minLevel) {
    minLevel_ = minLevel;
    LOG_INFO("Log level changed to: %s", levelToString(minLevel));
}

void LogManager::enableSerialLogging(bool enable) {
    serialLogging_ = enable;
}

void LogManager::enableRemoteLogging(bool enable) {
    remoteLogging_ = enable;
}

// ============================================================================
// RING BUFFER
// ============================================================================

void LogManager::addToRingBuffer(const LogEntry& entry) {
    ringBuffer_[ringBufferIndex_] = entry;
    ringBufferIndex_ = (ringBufferIndex_ + 1) % RING_BUFFER_SIZE;
}

void LogManager::dumpRecentLogs() {
    Serial.println("\n=== Recent Logs (Ring Buffer) ===");

    size_t index = ringBufferIndex_;
    for (size_t i = 0; i < RING_BUFFER_SIZE; i++) {
        const LogEntry& entry = ringBuffer_[index];

        // Skip empty entries
        if (entry.message[0] == '\0') {
            index = (index + 1) % RING_BUFFER_SIZE;
            continue;
        }

        // Print log entry
        Serial.printf("[%10lu] [%s] %s\n",
                     entry.timestamp,
                     levelToString(entry.level),
                     entry.message);

        index = (index + 1) % RING_BUFFER_SIZE;
    }

    Serial.println("==================================\n");
}

const LogEntry* LogManager::getRecentLogs(size_t& count) {
    count = RING_BUFFER_SIZE;
    return ringBuffer_;
}

// ============================================================================
// OUTPUT FORMATTING
// ============================================================================

void LogManager::logToSerial(const LogEntry& entry) {
    // Extract filename from full path
    const char* filename = strrchr(entry.file, '/');
    if (filename == nullptr) {
        filename = strrchr(entry.file, '\\');
    }
    filename = (filename != nullptr) ? filename + 1 : entry.file;

    // Format: [timestamp] [LEVEL] [file:line] message
    Serial.printf("%s[%10lu] [%-5s] [%s:%d] %s\033[0m\n",
                 levelToColorCode(entry.level),
                 entry.timestamp,
                 levelToString(entry.level),
                 filename,
                 entry.line,
                 entry.message);
}

const char* LogManager::levelToString(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG:    return "DEBUG";
        case LogLevel::INFO:     return "INFO";
        case LogLevel::WARN:     return "WARN";
        case LogLevel::ERROR:    return "ERROR";
        case LogLevel::CRITICAL: return "CRIT";
        default:                 return "UNKNOWN";
    }
}

const char* LogManager::levelToColorCode(LogLevel level) {
    // ANSI color codes for terminal
    switch (level) {
        case LogLevel::DEBUG:    return "\033[36m";  // Cyan
        case LogLevel::INFO:     return "\033[32m";  // Green
        case LogLevel::WARN:     return "\033[33m";  // Yellow
        case LogLevel::ERROR:    return "\033[31m";  // Red
        case LogLevel::CRITICAL: return "\033[35m";  // Magenta
        default:                 return "\033[0m";   // Reset
    }
}
