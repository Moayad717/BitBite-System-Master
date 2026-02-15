#include "TaskScheduler.h"
#include "../core/LogManager.h"

// ============================================================================
// CONSTRUCTOR
// ============================================================================

TaskScheduler::TaskScheduler()
    : taskCount_(0) {
    for (size_t i = 0; i < MAX_TASKS; i++) {
        tasks_[i] = nullptr;
    }
}

// ============================================================================
// TASK REGISTRATION
// ============================================================================

bool TaskScheduler::registerTask(Task* task) {
    if (task == nullptr) {
        return false;
    }

    if (taskCount_ >= MAX_TASKS) {
        LOG_ERROR("TaskScheduler: max tasks reached (%u)", MAX_TASKS);
        return false;
    }

    tasks_[taskCount_++] = task;
    LOG_DEBUG("Task registered: %s (interval: %lu ms)", task->getName(), task->getInterval());
    return true;
}

// ============================================================================
// TICK - EXECUTE DUE TASKS
// ============================================================================

void TaskScheduler::tick() {
    unsigned long now = millis();

    for (size_t i = 0; i < taskCount_; i++) {
        if (tasks_[i] && tasks_[i]->shouldRun(now)) {
            tasks_[i]->execute();
            tasks_[i]->markRun(now);
        }
    }
}

// ============================================================================
// TASK MANAGEMENT
// ============================================================================

Task* TaskScheduler::getTask(const char* name) {
    for (size_t i = 0; i < taskCount_; i++) {
        if (tasks_[i] && strcmp(tasks_[i]->getName(), name) == 0) {
            return tasks_[i];
        }
    }
    return nullptr;
}

void TaskScheduler::enableTask(const char* name) {
    Task* task = getTask(name);
    if (task) {
        task->enable();
        LOG_DEBUG("Task enabled: %s", name);
    }
}

void TaskScheduler::disableTask(const char* name) {
    Task* task = getTask(name);
    if (task) {
        task->disable();
        LOG_DEBUG("Task disabled: %s", name);
    }
}
