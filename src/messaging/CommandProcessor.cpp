#include "CommandProcessor.h"
#include "SerialProtocol.h"
#include "../core/LogManager.h"

// ============================================================================
// CONSTRUCTOR
// ============================================================================

CommandProcessor::CommandProcessor()
    : serialProtocol_(nullptr) {
}

// ============================================================================
// CONFIGURATION
// ============================================================================

void CommandProcessor::setSerialProtocol(SerialProtocol* serialProtocol) {
    serialProtocol_ = serialProtocol;
}

// ============================================================================
// COMMAND PROCESSING
// ============================================================================

void CommandProcessor::processCommand(const String& commandType, const String& commandId) {
    LOG_INFO("Processing command: %s", commandType.c_str());

    // Route to specific handlers
    if (commandType == "SYNC_SCHEDULES") {
        handleSyncSchedules();
    } else if (commandType == "SYNC_NAME") {
        handleSyncName();
    } else if (commandType == "CLEAR_FAULTS") {
        handleClearFaults();
    } else if (commandType == "GET_SCHEDULE_STATUS") {
        handleGetScheduleStatus();
    } else {
        // Generic command - forward to external handler
        handleGenericCommand(commandType);
    }
}

// ============================================================================
// COMMAND HANDLERS
// ============================================================================

void CommandProcessor::handleSyncSchedules() {
    LOG_INFO("Command: Sync schedules to Feeding ESP");

    if (serialProtocol_) {
        serialProtocol_->syncSchedules();
    } else {
        LOG_ERROR("SerialProtocol not initialized");
    }
}

void CommandProcessor::handleSyncName() {
    LOG_INFO("Command: Sync display name to Feeding ESP");

    if (serialProtocol_) {
        serialProtocol_->sendDisplayName();
    } else {
        LOG_ERROR("SerialProtocol not initialized");
    }
}

void CommandProcessor::handleClearFaults() {
    LOG_INFO("Command: Clear faults on Feeding ESP");

    // Forward to Feeding ESP via Serial2
    Serial2.println("CLEAR_FAULTS");
}

void CommandProcessor::handleGetScheduleStatus() {
    LOG_INFO("Command: Get schedule status from Feeding ESP");

    // Forward to Feeding ESP via Serial2
    Serial2.println("GET_SCHEDULE_STATUS");
}

void CommandProcessor::handleGenericCommand(const String& commandType) {
    LOG_INFO("Forwarding command to Feeding ESP: %s", commandType.c_str());

    // Forward to Feeding ESP via Serial2
    Serial2.println(commandType);
}
