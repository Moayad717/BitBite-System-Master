#pragma once

#include <Arduino.h>

// ============================================================================
// BACKOFF
// ============================================================================
// Shared timing helpers for periodic-retry logic (WiFi reconnect, Firebase
// reconnect, etc.) — header-only since every method is a one-line calculation
// with no state of its own (callers keep their own lastAttempt/failure count).

class Backoff {
public:
    // True once `intervalMs` has elapsed since `lastAttempt`. Used for plain
    // fixed-interval retry (no exponential growth).
    static bool ready(unsigned long now, unsigned long lastAttempt, unsigned long intervalMs) {
        return now - lastAttempt >= intervalMs;
    }

    // Exponential backoff interval: baseMs * 2^failures, capped at capMs.
    static unsigned long computeInterval(unsigned long baseMs, uint8_t failures, unsigned long capMs) {
        unsigned long interval = baseMs;
        for (uint8_t i = 0; i < failures && interval < capMs; i++) {
            interval *= 2;
        }
        if (interval > capMs) interval = capMs;
        return interval;
    }
};
