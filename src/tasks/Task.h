#pragma once

#include <Arduino.h>

// ============================================================================
// TASK BASE CLASS
// ============================================================================
// Base class for periodic tasks in the cooperative scheduler.
// Derived classes implement execute() with their specific logic.

class Task {
public:
    Task(const char* name, unsigned long intervalMs)
        : name_(name),
          interval_(intervalMs),
          lastRun_(0),
          enabled_(true) {
    }

    virtual ~Task() = default;

    // Called by scheduler to check if task should run
    bool shouldRun(unsigned long currentMillis) const {
        if (!enabled_) return false;
        return (currentMillis - lastRun_) >= interval_;
    }

    // Called by scheduler after execute() completes
    void markRun(unsigned long currentMillis) {
        lastRun_ = currentMillis;
    }

    // Override in derived classes - contains the actual task logic
    virtual void execute() = 0;

    // Task control
    void enable() { enabled_ = true; }
    void disable() { enabled_ = false; }
    bool isEnabled() const { return enabled_; }

    // Configuration
    void setInterval(unsigned long ms) { interval_ = ms; }
    unsigned long getInterval() const { return interval_; }
    const char* getName() const { return name_; }
    unsigned long getLastRun() const { return lastRun_; }

protected:
    const char* name_;
    unsigned long interval_;
    unsigned long lastRun_;
    bool enabled_;
};
