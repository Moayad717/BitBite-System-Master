#pragma once

#include <Arduino.h>

// Forward declarations
class SerialProtocol;

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

    // Process a command
    void processCommand(const String& commandType, const String& commandId);

private:
    SerialProtocol* serialProtocol_;

    // Command handlers
    void handleSyncSchedules();
    void handleSyncName();
    void handleClearFaults();
    void handleGetScheduleStatus();
    void handleGenericCommand(const String& commandType);
};
