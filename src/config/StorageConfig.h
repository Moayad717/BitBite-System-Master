#pragma once

// ============================================================================
// OFFLINE STORAGE CONFIGURATION
// ============================================================================

// Queue file path
#define OFFLINE_QUEUE_FILE "/offline_queue.jsonl"

// Queue limits
#define MAX_OFFLINE_ENTRIES 50          // Maximum entries before dropping oldest
#define MAX_ENTRY_SIZE 512              // Maximum JSON size per entry

// SPIFFS thresholds
#define SPIFFS_MIN_FREE_BYTES 10240     // 10KB minimum free space

// Flush behavior
#define OFFLINE_FLUSH_BATCH_SIZE 1      // Entries to flush per tick (rate limiting)
