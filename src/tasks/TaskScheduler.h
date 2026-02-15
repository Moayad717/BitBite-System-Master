#pragma once

#include <Arduino.h>
#include "Task.h"

// ============================================================================
// TASK SCHEDULER
// ============================================================================
// Simple cooperative task scheduler that manages periodic tasks.
// Call tick() from the main loop to execute due tasks.

class TaskScheduler {
public:
    TaskScheduler();

    // Register a task (returns false if max tasks reached)
    bool registerTask(Task* task);

    // Execute due tasks - call from main loop
    void tick();

    // Task management
    Task* getTask(const char* name);
    void enableTask(const char* name);
    void disableTask(const char* name);

    // Statistics
    size_t getTaskCount() const { return taskCount_; }

private:
    static const size_t MAX_TASKS = 10;
    Task* tasks_[MAX_TASKS];
    size_t taskCount_;
};
