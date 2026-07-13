#pragma once

#include <Arduino.h>

// Forward declarations
class SerialProtocol;
#ifdef DEV_BUILD
class FirebaseManager;
class StreamManager;
#endif

// ============================================================================
// COMMAND PROCESSOR
// ============================================================================
// Processes commands received from Firebase stream
// Routes commands to appropriate handlers (Serial2, local processing, etc.)

class CommandProcessor {
public:
    CommandProcessor();

    // Set SerialProtocol dependency
    void setSerialProtocol(SerialProtocol* serialProtocol);

#ifdef DEV_BUILD
    // Dependencies for the two dev-only simulate commands below
    void setFirebaseManager(FirebaseManager* firebaseManager);
    void setStreamManager(StreamManager* streamManager);
#endif

    // Process a command
    void processCommand(const String& commandType, const String& commandId);

private:
    SerialProtocol* serialProtocol_;
#ifdef DEV_BUILD
    FirebaseManager* firebaseManager_;
    StreamManager* streamManager_;
#endif

    // Command handlers
    void handleSyncSchedules();
    void handleSyncName();
    void handleClearFaults();
    void handleGetScheduleStatus();
    void handleGenericCommand(const String& commandType);
#ifdef DEV_BUILD
    void handleSimulateFirebaseDown();
    void handleSimulateStreamRestart();
#endif
};
