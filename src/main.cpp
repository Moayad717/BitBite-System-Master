#include <Arduino.h>

// Phase 1 Infrastructure
#include "core/LogManager.h"
#include "utils/MemoryMonitor.h"
#include "utils/Watchdog.h"
#include "config/Config.h"
#include "config/TimingConfig.h"

// Phase 2 Components
#include "core/DeviceManager.h"
#include "core/TimeManager.h"
#include "connectivity/WiFiConnectionManager.h"
#include "connectivity/FirebaseManager.h"
#include "messaging/StreamManager.h"
#include "messaging/CommandProcessor.h"
#include "messaging/SerialProtocol.h"
#include "display/OLEDDisplay.h"
#include "config/Credentials.h"

// Phase 3: Storage and Tasks
#include "storage/SPIFFSHelper.h"
#include "storage/OfflineQueueManager.h"
#include "tasks/TaskScheduler.h"
#include "tasks/TimeSyncTask.h"
#include "tasks/StatusUpdateTask.h"
#include "tasks/OfflineFlushTask.h"

// Phase 4: Dual-Core
#include "core/DualCoreManager.h"

// ============================================================================
// GLOBAL INSTANCES
// ============================================================================

DeviceManager deviceManager;
TimeManager timeManager;
WiFiConnectionManager wifiManager;
FirebaseManager firebaseManager;
StreamManager streamManager;
CommandProcessor commandProcessor;
SerialProtocol serialProtocol;
OLEDDisplay oledDisplay;
SensorStatus sensorStatus;

// Phase 3: Storage and Tasks
OfflineQueueManager offlineQueue;
TaskScheduler taskScheduler;

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

void initializeDevice();
void sendStatusToFirebase();

// ============================================================================
// SETUP
// ============================================================================

void setup() {
    // Initialize Serial
    Serial.begin(115200);
    delay(1000);  // Wait for serial to stabilize

    Serial.println("\n\n");
    Serial.println("========================================");
    Serial.println("   ESP32 Horse Feeder - WiFi Module");
    Serial.println("   Phase 2: Modular Refactor");
    Serial.println("========================================");

    // Initialize Phase 1 components
    LogManager::getInstance().begin();
    MemoryMonitor::begin();

    // Initialize SPIFFS for offline storage
    MemoryMonitor::checkpoint("Before SPIFFS");
    if (SPIFFSHelper::begin()) {
        LOG_INFO("SPIFFS ready: %u/%u bytes free",
                 SPIFFSHelper::getFreeBytes(), SPIFFSHelper::getTotalBytes());
    } else {
        LOG_WARN("SPIFFS init failed - offline storage disabled");
    }

    // Initialize Serial2 for Feeding ESP communication
    Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);
    LOG_INFO("Serial2 initialized for Feeding ESP (RX: %d, TX: %d)", RXD2, TXD2);

    // NOTE: Watchdog started AFTER time sync to prevent timeout during NTP

    LOG_INFO("System initialization complete");
    LOG_INFO("Build: " __DATE__ " " __TIME__);

    #ifdef DEV_BUILD
    LOG_INFO("Build type: DEVELOPMENT");
    #elif defined(PROD_BUILD)
    LOG_INFO("Build type: PRODUCTION");
    #else
    LOG_INFO("Build type: DEFAULT");
    #endif

    // Initialize Phase 2 components
    MemoryMonitor::checkpoint("Before DeviceManager");
    if (!deviceManager.begin()) {
        LOG_CRITICAL("Failed to initialize DeviceManager");
        delay(5000);
        ESP.restart();
    }

    // Initialize OLED display
    if (oledDisplay.begin()) {
        oledDisplay.showStartup("Initializing...");
    }

    MemoryMonitor::checkpoint("Before WiFiManager");
    if (!wifiManager.begin()) {
        LOG_WARN("WiFi connection failed - continuing offline");
    }

    MemoryMonitor::checkpoint("Before TimeManager");
    if (wifiManager.isConnected()) {
        if (!timeManager.begin()) {
            LOG_WARN("NTP sync failed - continuing without time");
        }
    } else {
        LOG_WARN("Skipping NTP sync (no WiFi connection)");
    }

    // Initialize Firebase (only if WiFi connected)
    MemoryMonitor::checkpoint("Before FirebaseManager");
    if (wifiManager.isConnected()) {
        if (!firebaseManager.begin(API_KEY, DATABASE_URL)) {
            LOG_WARN("Firebase initialization failed - continuing offline");
        } else {
            // Initialize device in Firebase
            initializeDevice();

            // Initialize offline queue
            offlineQueue.begin(&firebaseManager, &deviceManager);

            // Initialize SerialProtocol with status update callback
            serialProtocol.begin(&firebaseManager, &deviceManager, &timeManager, &offlineQueue);
            serialProtocol.setStatusUpdateCallback([](const String& statusJson) {
                // Parse status and update sensorStatus
                FirebaseJson json;
                json.setJsonData(statusJson);
                FirebaseJsonData data;

                if (json.get(data, "temperature")) sensorStatus.temperature = data.floatValue;
                if (json.get(data, "humidity")) sensorStatus.humidity = data.floatValue;
                if (json.get(data, "waterFlow")) sensorStatus.waterFlow = data.floatValue;
                if (json.get(data, "isFeeding")) sensorStatus.isFeeding = data.boolValue;
                if (json.get(data, "activeFaults")) sensorStatus.activeFaults = data.intValue;

                json.clear();
            });

            // Connect CommandProcessor with SerialProtocol
            commandProcessor.setSerialProtocol(&serialProtocol);

            // Initialize StreamManager
            streamManager.begin(&firebaseManager, &deviceManager);
            streamManager.setCommandCallback([](const String& type, const String& id) {
                commandProcessor.processCommand(type, id);
            });

            // Start command stream
            if (streamManager.startStream()) {
                LOG_INFO("Command stream active");
            } else {
                LOG_WARN("Failed to start command stream");
            }

            // Send initial time and name to Feeding ESP
            serialProtocol.sendTime();
            serialProtocol.sendDisplayName();
        }
    } else {
        LOG_WARN("Skipping Firebase init (no WiFi connection)");
        // Still initialize offline queue for when we come back online
        offlineQueue.begin(&firebaseManager, &deviceManager);
        serialProtocol.begin(&firebaseManager, &deviceManager, &timeManager, &offlineQueue);
    }

    // Register periodic tasks with scheduler
    static TimeSyncTask timeSyncTask(&wifiManager, &timeManager, TIME_SYNC_INTERVAL);
    taskScheduler.registerTask(&timeSyncTask);

    static StatusUpdateTask statusUpdateTask(&firebaseManager, &deviceManager, &wifiManager, STATUS_UPDATE_INTERVAL);
    taskScheduler.registerTask(&statusUpdateTask);

    static OfflineFlushTask offlineFlushTask(&offlineQueue, &firebaseManager, OFFLINE_LOG_FLUSH_INTERVAL);
    taskScheduler.registerTask(&offlineFlushTask);

    LOG_INFO("TaskScheduler: %u tasks registered", taskScheduler.getTaskCount());

    // Start watchdog AFTER Firebase init (can take time)
    Watchdog::begin(30000);  // 30 second watchdog
    LOG_INFO("Watchdog started");

    // Start network task on Core 0 (WiFi, Firebase, tasks run there)
    startNetworkTask();

    // Create memory checkpoint
    MemoryMonitor::checkpoint("After setup()");

    LOG_INFO("Entering main loop on Core %d...", xPortGetCoreID());

    // Print memory usage summary
    MemoryMonitor::printCheckpoints();
}

// ============================================================================
// LOOP
// ============================================================================

void loop() {
    // ---- Core 1: Time-sensitive operations only ----

    // Feed watchdog to prevent reset
    Watchdog::feed();

    // Monitor memory
    MemoryMonitor::tick();

    // Handle serial communication with Feeding ESP (latency-sensitive)
    serialProtocol.tick();

    // Update OLED display every 2 seconds
    static unsigned long lastOledUpdate = 0;
    unsigned long now = millis();
    if (now - lastOledUpdate >= 2000) {
        lastOledUpdate = now;
        oledDisplay.update(sensorStatus, wifiManager.getRSSI(), wifiManager.isConnected());
    }

    // Status logging every 10 seconds
    static unsigned long lastDemo = 0;
    if (now - lastDemo >= 10000) {
        lastDemo = now;

        if (wifiManager.isConnected()) {
            LOG_INFO("System online - WiFi: %s (RSSI: %d dBm) - Firebase: %s - Uptime: %lu ms",
                     wifiManager.getSSID().c_str(),
                     wifiManager.getRSSI(),
                     firebaseManager.isReady() ? "Connected" : "Offline",
                     now);
        } else {
            LOG_INFO("System offline - Uptime: %lu ms", now);
        }

        // Log offline queue status if entries pending
        if (offlineQueue.hasEntries()) {
            LOG_INFO("Offline queue: %u entries pending", offlineQueue.getEntryCount());
        }

        LOG_INFO("Free heap: %u bytes", MemoryMonitor::getFreeHeap());
    }

    // Small delay — Core 1 loop is now lightweight, no network blocking
    delay(50);
}
