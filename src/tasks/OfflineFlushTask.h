#pragma once

#include "Task.h"

class OfflineQueueManager;
class FirebaseManager;

// ============================================================================
// OFFLINE FLUSH TASK
// ============================================================================
// Periodically attempts to flush queued offline entries to Firebase.

class OfflineFlushTask : public Task {
public:
    OfflineFlushTask(OfflineQueueManager* queue, FirebaseManager* fb, unsigned long intervalMs);
    void execute() override;

private:
    OfflineQueueManager* queueManager_;
    FirebaseManager* firebaseManager_;
};
