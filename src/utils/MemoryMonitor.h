#pragma once

#include <Arduino.h>

// ============================================================================
// MEMORY CHECKPOINT
// ============================================================================

struct MemoryCheckpoint {
    const char* label;
    size_t freeHeap;
    size_t minFreeHeap;
    size_t largestBlock;
    unsigned long timestamp;
};

// ============================================================================
// MEMORY MONITOR
// ============================================================================

class MemoryMonitor {
public:
    // Initialize memory monitoring
    static void begin();

    // Periodic check (call from loop or task)
    static void tick();

    // Manual memory check
    static void check(const char* label = nullptr);

    // Get current memory stats
    static size_t getFreeHeap();
    static size_t getMinFreeHeap();
    static size_t getLargestFreeBlock();

    // Checkpoints for leak detection
    static void checkpoint(const char* label);
    static void printCheckpoints();
    static void clearCheckpoints();

    // Memory status
    static bool isMemoryLow();
    static bool isMemoryCritical();

    // Configuration
    static void setWarningThreshold(size_t bytes);
    static void setCriticalThreshold(size_t bytes);

private:
    // Thresholds
    static size_t warningThreshold_;
    static size_t criticalThreshold_;

    // Checkpoints (up to 10)
    static const size_t MAX_CHECKPOINTS = 10;
    static MemoryCheckpoint checkpoints_[MAX_CHECKPOINTS];
    static size_t checkpointCount_;

    // Last check time
    static unsigned long lastCheckTime_;
};
