#pragma once

// ============================================================================
// TIMING CONFIGURATION
// ============================================================================

// Reconnection Intervals
#define FIREBASE_RECONNECT_INTERVAL 30000   // 30 seconds
#define WIFI_RECONNECT_INTERVAL 60000       // 60 seconds
#define STREAM_RESTART_INTERVAL 15000       // 15 seconds between stream restart attempts

// Sync Intervals
#define TIME_SYNC_INTERVAL 43200000         // 12 hours

// Update Intervals
#define OLED_UPDATE_INTERVAL 2000           // 2 seconds
#define STATUS_UPDATE_INTERVAL 300000       // 5 minutes

// Memory Management
#define MEMORY_CHECK_THRESHOLD 50000        // 50KB minimum free heap
#define MEMORY_CHECK_INTERVAL 5000          // Check every 5 seconds

// Offline Log Queue
#define MAX_OFFLINE_LOGS 10                 // Maximum logs to store when offline
#define OFFLINE_LOG_FLUSH_INTERVAL 5000     // Try to flush logs every 5s

// Schedule Sync
#define SCHEDULE_SYNC_TIMEOUT 5000          // 5 seconds timeout for confirmation
#define SCHEDULE_STATUS_TIMEOUT 10000       // 10 seconds timeout for multi-line status collection

// Feeder OTA forwarding
#define FEEDER_OTA_RETRY_MS  30000          // Minimum gap between Serial2 transfer attempts

// OTA HTTP
#define OTA_API_TIMEOUT_MS   15000          // GitHub API request timeout
#define OTA_HTTP_TIMEOUT_MS  60000          // Firmware binary download timeout
#define OTA_STALL_TIMEOUT_MS 15000          // Abort download if no data for this long

// Watchdog
#define WATCHDOG_TIMEOUT_MS  30000          // Hardware watchdog

// Core 1 loop
#define LOOP_DELAY_MS        50             // Keeps Core 1 from busy-spinning
#define STATUS_LOG_INTERVAL_MS 300000       // "System online" console log (5 minutes)

// NTP Configuration
#define NTP_SERVER_1 "time.google.com"
#define NTP_SERVER_2 "time.cloudflare.com"
#define NTP_SERVER_3 "lb.pool.ntp.org"
#define TIMEZONE "EET-2EEST,M3.5.0/0,M10.5.0/0"  // Eastern European Time
