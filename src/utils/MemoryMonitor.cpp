#include "MemoryMonitor.h"
#include "../core/LogManager.h"
#include "../config/TimingConfig.h"

// ============================================================================
// STATIC MEMBERS
// ============================================================================

size_t MemoryMonitor::warningThreshold_ = 60000;  // 60KB
size_t MemoryMonitor::criticalThreshold_ = MEMORY_CHECK_THRESHOLD;  // 50KB
MemoryCheckpoint MemoryMonitor::checkpoints_[MAX_CHECKPOINTS];
size_t MemoryMonitor::checkpointCount_ = 0;
unsigned long MemoryMonitor::lastCheckTime_ = 0;

// ============================================================================
// INITIALIZATION
// ============================================================================

void MemoryMonitor::begin() {
    LOG_INFO("MemoryMonitor initialized");
    LOG_INFO("  Total heap: %u bytes", ESP.getHeapSize());
    LOG_INFO("  Free heap: %u bytes", ESP.getFreeHeap());
    LOG_INFO("  Min free heap: %u bytes", ESP.getMinFreeHeap());
    LOG_INFO("  Largest block: %u bytes", ESP.getMaxAllocHeap());
    LOG_INFO("  Warning threshold: %u bytes", warningThreshold_);
    LOG_INFO("  Critical threshold: %u bytes", criticalThreshold_);

    // Create initial checkpoint
    checkpoint("Initialization");
}

// ============================================================================
// PERIODIC CHECK
// ============================================================================

void MemoryMonitor::tick() {
    unsigned long now = millis();

    if (now - lastCheckTime_ >= MEMORY_CHECK_INTERVAL) {
        lastCheckTime_ = now;
        check();
    }
}

void MemoryMonitor::check(const char* label) {
    size_t freeHeap = getFreeHeap();
    size_t minFreeHeap = getMinFreeHeap();

    // Check for critical memory
    if (freeHeap < criticalThreshold_) {
        LOG_CRITICAL("Memory CRITICAL: %u bytes free (min: %u)", freeHeap, minFreeHeap);
        LOG_CRITICAL("System may become unstable - consider restart");

        // Print recent checkpoints to help identify leak
        printCheckpoints();

        delay(5000);
        ESP.restart();
    }
    // Check for low memory
    else if (freeHeap < warningThreshold_) {
        LOG_WARN("Memory LOW: %u bytes free (min: %u)", freeHeap, minFreeHeap);
    }
    // Log debug info periodically
    else if (label != nullptr) {
        LOG_DEBUG("Memory check '%s': %u bytes free", label, freeHeap);
    }
}

// ============================================================================
// MEMORY STATS
// ============================================================================

size_t MemoryMonitor::getFreeHeap() {
    return ESP.getFreeHeap();
}

size_t MemoryMonitor::getMinFreeHeap() {
    return ESP.getMinFreeHeap();
}

size_t MemoryMonitor::getLargestFreeBlock() {
    return ESP.getMaxAllocHeap();
}

// ============================================================================
// CHECKPOINTS (for leak detection)
// ============================================================================

void MemoryMonitor::checkpoint(const char* label) {
    if (checkpointCount_ >= MAX_CHECKPOINTS) {
        LOG_WARN("Checkpoint limit reached (%d), clearing old checkpoints", MAX_CHECKPOINTS);
        // Shift array left (remove oldest)
        for (size_t i = 0; i < MAX_CHECKPOINTS - 1; i++) {
            checkpoints_[i] = checkpoints_[i + 1];
        }
        checkpointCount_ = MAX_CHECKPOINTS - 1;
    }

    MemoryCheckpoint& cp = checkpoints_[checkpointCount_];
    cp.label = label;
    cp.freeHeap = getFreeHeap();
    cp.minFreeHeap = getMinFreeHeap();
    cp.largestBlock = getLargestFreeBlock();
    cp.timestamp = millis();

    checkpointCount_++;

    LOG_DEBUG("Memory checkpoint '%s': %u bytes free", label, cp.freeHeap);
}

void MemoryMonitor::printCheckpoints() {
    if (checkpointCount_ == 0) {
        LOG_INFO("No memory checkpoints recorded");
        return;
    }

    Serial.println("\n=== Memory Checkpoints ===");

    for (size_t i = 0; i < checkpointCount_; i++) {
        const MemoryCheckpoint& cp = checkpoints_[i];

        Serial.printf("[%2d] [%10lu] %-20s: Free: %6u, Min: %6u, Largest: %6u\n",
                     i + 1,
                     cp.timestamp,
                     cp.label,
                     cp.freeHeap,
                     cp.minFreeHeap,
                     cp.largestBlock);

        // Calculate delta from previous checkpoint
        if (i > 0) {
            const MemoryCheckpoint& prev = checkpoints_[i - 1];
            int delta = (int)cp.freeHeap - (int)prev.freeHeap;

            if (delta < 0) {
                Serial.printf("     └─> Lost %d bytes since '%s'\n", -delta, prev.label);
            } else if (delta > 0) {
                Serial.printf("     └─> Gained %d bytes since '%s'\n", delta, prev.label);
            }
        }
    }

    Serial.println("==========================\n");
}

void MemoryMonitor::clearCheckpoints() {
    checkpointCount_ = 0;
    LOG_INFO("Memory checkpoints cleared");
}

// ============================================================================
// MEMORY STATUS
// ============================================================================

bool MemoryMonitor::isMemoryLow() {
    return getFreeHeap() < warningThreshold_;
}

bool MemoryMonitor::isMemoryCritical() {
    return getFreeHeap() < criticalThreshold_;
}

// ============================================================================
// CONFIGURATION
// ============================================================================

void MemoryMonitor::setWarningThreshold(size_t bytes) {
    warningThreshold_ = bytes;
    LOG_INFO("Memory warning threshold set to %u bytes", bytes);
}

void MemoryMonitor::setCriticalThreshold(size_t bytes) {
    criticalThreshold_ = bytes;
    LOG_INFO("Memory critical threshold set to %u bytes", bytes);
}
