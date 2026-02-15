#include "OfflineFlushTask.h"
#include "../storage/OfflineQueueManager.h"
#include "../connectivity/FirebaseManager.h"
#include "../core/LogManager.h"

// ============================================================================
// CONSTRUCTOR
// ============================================================================

OfflineFlushTask::OfflineFlushTask(OfflineQueueManager* queue, FirebaseManager* fb, unsigned long intervalMs)
    : Task("OfflineFlush", intervalMs),
      queueManager_(queue),
      firebaseManager_(fb) {
}

// ============================================================================
// EXECUTE
// ============================================================================

void OfflineFlushTask::execute() {
    if (!queueManager_ || !firebaseManager_) {
        return;
    }

    // Only flush if Firebase is ready
    if (!firebaseManager_->isReady()) {
        return;
    }

    // Only flush if there are entries
    if (!queueManager_->hasEntries()) {
        return;
    }

    // Flush one entry per tick (rate limiting)
    queueManager_->flushOne();
}
