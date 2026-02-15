# ESP32 Horse Feeder - WiFi Module (PlatformIO)

Production-ready refactored version of the WiFi ESP coordinator module.

**From**: 2 monolithic .ino files (2,951 lines)
**To**: 16 modular classes across 32+ files

---

## 🚀 Quick Start

### 1. Install PlatformIO

1. Install [VSCode](https://code.visualstudio.com/)
2. Install PlatformIO extension in VSCode
3. Restart VSCode

### 2. Open Project

```bash
cd esp-wifi-platformio
code .
```

### 3. Build & Upload

**Development Build (verbose logging):**
```bash
pio run -e esp32dev -t upload
```

**Production Build (minimal logging):**
```bash
pio run -e esp32prod -t upload
```

### 4. Monitor Serial Output

```bash
pio device monitor
```

---

## 📁 Project Structure

```
esp-wifi-platformio/
├── platformio.ini              # Build configuration (3 environments)
├── README.md                   # This file
│
├── src/
│   ├── main.cpp                # Application entry point
│   ├── main_helpers.cpp        # Device initialization & status updates
│   │
│   ├── config/                 # ⚙️ Configuration
│   │   ├── Config.h            # Hardware pin definitions
│   │   ├── Credentials.h       # Firebase API credentials (GITIGNORED!)
│   │   ├── TimingConfig.h      # Timing intervals (WiFi, Firebase, NTP)
│   │   └── BufferConfig.h      # Buffer sizes for streams & responses
│   │
│   ├── core/                   # 🔧 Core Infrastructure
│   │   ├── LogManager.h/cpp    # 5-level logging (DEBUG/INFO/WARN/ERROR/CRITICAL)
│   │   ├── DeviceManager.h/cpp # Device ID generation & Firebase path management
│   │   └── TimeManager.h/cpp   # NTP time synchronization
│   │
│   ├── utils/                  # 🛠️ Utilities
│   │   ├── MemoryMonitor.h/cpp # Heap tracking with leak detection
│   │   └── Watchdog.h/cpp      # Hardware watchdog timer (30s timeout)
│   │
│   ├── connectivity/           # 🌐 Network & Cloud
│   │   ├── WiFiConnectionManager.h/cpp  # WiFi with captive portal & auto-reconnect
│   │   └── FirebaseManager.h/cpp        # Firebase RTDB operations
│   │
│   ├── messaging/              # 📡 Communication
│   │   ├── StreamManager.h/cpp          # Real-time Firebase command stream
│   │   ├── CommandProcessor.h/cpp       # Command routing & execution
│   │   └── SerialProtocol.h/cpp         # Serial2 ↔ Feeding ESP protocol
│   │
│   └── display/                # 📺 User Interface
│       └── OLEDDisplay.h/cpp   # SSD1306 OLED (128x64) rendering
│
└── test/                       # 🧪 Unit Tests (Future: Phase 3)
```

---

## 🏗️ Architecture Overview

### Layered Architecture

```
┌─────────────────────────────────────────────────┐
│              Application Layer                  │
│                   main.cpp                      │
└─────────────────────────────────────────────────┘
                       ↓
┌─────────────────────────────────────────────────┐
│             Component Layer                     │
│  ┌──────────────┐  ┌──────────────┐            │
│  │ Messaging    │  │ Display      │            │
│  │ - Stream     │  │ - OLED       │            │
│  │ - Commands   │  └──────────────┘            │
│  │ - Serial     │                               │
│  └──────────────┘                               │
└─────────────────────────────────────────────────┘
                       ↓
┌─────────────────────────────────────────────────┐
│           Connectivity Layer                    │
│  ┌──────────────┐  ┌──────────────┐            │
│  │ WiFi Manager │  │ Firebase Mgr │            │
│  └──────────────┘  └──────────────┘            │
└─────────────────────────────────────────────────┘
                       ↓
┌─────────────────────────────────────────────────┐
│              Core Layer                         │
│  ┌─────────┐  ┌─────────┐  ┌─────────┐        │
│  │ Device  │  │  Time   │  │ Logging │        │
│  │ Manager │  │ Manager │  │ Manager │        │
│  └─────────┘  └─────────┘  └─────────┘        │
└─────────────────────────────────────────────────┘
                       ↓
┌─────────────────────────────────────────────────┐
│             Utilities Layer                     │
│  ┌──────────────┐  ┌──────────────┐            │
│  │ Memory       │  │ Watchdog     │            │
│  │ Monitor      │  │ Timer        │            │
│  └──────────────┘  └──────────────┘            │
└─────────────────────────────────────────────────┘
```

---

## 📦 Component Details

### 🔧 Core Components

#### **LogManager** (`core/LogManager.h/cpp`)
- **Purpose**: Centralized logging system with multiple severity levels
- **Features**:
  - 5 log levels: DEBUG, INFO, WARN, ERROR, CRITICAL
  - ANSI color-coded output (green/blue/yellow/red/magenta)
  - Ring buffer (50 entries) for debugging
  - File/line number tracking
  - Configurable per-build (DEV shows DEBUG, PROD shows WARN+)
- **Usage**:
  ```cpp
  LOG_DEBUG("Detailed info");
  LOG_INFO("Normal operation");
  LOG_WARN("Something unusual");
  LOG_ERROR("Operation failed");
  LOG_CRITICAL("System failure");
  ```

#### **DeviceManager** (`core/DeviceManager.h/cpp`)
- **Purpose**: Manages device identity and Firebase path structure
- **Features**:
  - Generates unique device ID from MAC address
  - Creates Firebase paths: `/devices/{ID}/status`, `/commands`, etc.
  - Provides timestamp formatting
- **Paths Managed**:
  - `devicePath`: `/devices/ESP_AABBCCDDEEFF_abc123`
  - `statusPath`: `{devicePath}/status`
  - `commandsPath`: `{devicePath}/commands`
  - `schedulePath`: `{devicePath}/schedules`
  - `displayNamePath`: `{devicePath}/info/displayName`

#### **TimeManager** (`core/TimeManager.h/cpp`)
- **Purpose**: NTP time synchronization
- **Features**:
  - Multiple NTP servers (Google, Cloudflare, NTP Pool)
  - Timezone configuration (GMT+0)
  - Hourly resync capability
  - Tracks last sync time
- **Servers**: `time.google.com`, `time.cloudflare.com`, `pool.ntp.org`

---

### 🛠️ Utilities

#### **MemoryMonitor** (`utils/MemoryMonitor.h/cpp`)
- **Purpose**: Track heap memory usage and detect leaks
- **Features**:
  - Checkpoint system (before/after operations)
  - Delta calculation between checkpoints
  - Warning/critical thresholds
  - Free/min/largest block tracking
- **Usage**:
  ```cpp
  MemoryMonitor::checkpoint("Before WiFi");
  // ... WiFi init ...
  MemoryMonitor::checkpoint("After WiFi");
  MemoryMonitor::printCheckpoints();  // Shows delta
  ```

#### **Watchdog** (`utils/Watchdog.h/cpp`)
- **Purpose**: Prevent system hangs
- **Features**:
  - Hardware watchdog timer (30s timeout)
  - Must be fed regularly from main loop
  - Auto-reset on timeout
- **Usage**:
  ```cpp
  Watchdog::begin(30000);  // 30 second timeout
  // In loop:
  Watchdog::feed();
  ```

---

### 🌐 Connectivity

#### **WiFiConnectionManager** (`connectivity/WiFiConnectionManager.h/cpp`)
- **Purpose**: Manage WiFi connection lifecycle
- **Features**:
  - WiFiManager integration (captive portal)
  - Auto-reconnection (60s interval)
  - Signal strength monitoring (RSSI)
  - AP name: `BitBite-{device_id}`
- **States**: Disconnected → Connecting → Connected → Auto-reconnect

#### **FirebaseManager** (`connectivity/FirebaseManager.h/cpp`)
- **Purpose**: Firebase Realtime Database operations
- **Features**:
  - Authentication with API key
  - RTDB read/write operations
  - Stream management for real-time updates
  - Auto-reconnection (30s interval)
  - Error tracking
- **Methods**:
  ```cpp
  setJSON(path, json)      // Write JSON data
  getJSON(path, json)      // Read JSON data
  getString(path, str)     // Read string
  deleteNode(path)         // Delete node
  beginStream(path, cb)    // Start real-time stream
  ```

---

### 📡 Messaging

#### **StreamManager** (`messaging/StreamManager.h/cpp`)
- **Purpose**: Listen for real-time Firebase commands
- **Features**:
  - Firebase stream callback handling
  - Single & bulk command processing
  - Duplicate command prevention (delete queue)
  - Command callback system
  - Automatic processed command cleanup
- **Flow**:
  1. Firebase command added → Stream event
  2. Parse command type/data
  3. Execute via callback
  4. Queue deletion
  5. Delete after processing

#### **CommandProcessor** (`messaging/CommandProcessor.h/cpp`)
- **Purpose**: Route and execute commands from Firebase
- **Commands Handled**:
  - `SYNC_SCHEDULES` → Sync feeding schedules to Feeding ESP
  - `SYNC_NAME` → Sync display name to Feeding ESP
  - `CLEAR_FAULTS` → Clear active faults
  - `FEED_NOW`, `TARE`, etc. → Forward to Feeding ESP
- **Flow**: Firebase → StreamManager → **CommandProcessor** → SerialProtocol/Actions

#### **SerialProtocol** (`messaging/SerialProtocol.h/cpp`)
- **Purpose**: Bidirectional Serial2 communication with Feeding ESP
- **Protocol**:
  - **TX (WiFi → Feeding)**:
    - `TIME:YYYY-MM-DD HH:MM:SS` - Time sync
    - `NAME:Display Name` - Name sync
    - `SCHEDULES:{json}` - Schedule data with hash verification
    - `FEED_NOW`, `TARE`, `CLEAR_FAULTS` - Commands
  - **RX (Feeding → WiFi)**:
    - `{json}` - Status update (temp, humidity, water, etc.)
    - `LOG:{json}` - Log entry → Firebase logs
    - `FAULT:{json}` - Fault event → Firebase faults
    - `SCHEDULE_HASH:12345` - Sync confirmation
- **Features**:
  - JSON sanitization (NaN/Inf → -999)
  - Hash-based schedule verification
  - Status callback for OLED update
  - Automatic Firebase forwarding

---

### 📺 Display

#### **OLEDDisplay** (`display/OLEDDisplay.h/cpp`)
- **Purpose**: Render sensor data on 128x64 OLED screen
- **Display Elements**:
  - **Top Right**: WiFi icon (4 bars + X if disconnected)
  - **Line 1**: Temperature (°C with circle symbol)
  - **Line 2**: Humidity (%)
  - **Line 3**: Water flow (L)
  - **Bottom Right**: Feeding indicator (filled circle)
  - **Fault Indicator**: "!" in circle if faults > 0
  - **Divider Lines**: Between sections
- **Features**:
  - Handles sensor errors (-999 displayed as "--")
  - Updates every 2 seconds
  - Startup message support

---

## 🔄 Data Flow

### Startup Sequence

```
1. Serial initialization (115200 baud)
2. LogManager initialization
3. MemoryMonitor initialization
4. Serial2 initialization (9600 baud for Feeding ESP)
5. OLED display "Initializing..."
6. DeviceManager → Generate device ID
7. WiFiConnectionManager → Connect or captive portal
8. TimeManager → NTP sync
9. FirebaseManager → Authenticate
10. Device initialization in Firebase
11. SerialProtocol setup with status callback
12. CommandProcessor → Link to SerialProtocol
13. StreamManager → Start command stream
14. Send initial TIME + NAME to Feeding ESP
15. Watchdog timer start (30s)
16. Enter main loop
```

### Main Loop Flow

```
Every iteration (~100ms):
├─ Feed watchdog
├─ Memory monitor tick
├─ WiFi auto-reconnect (if needed)
├─ Firebase auto-reconnect (if needed)
├─ StreamManager tick (process delete queue)
├─ SerialProtocol tick (read Feeding ESP data)
├─ Every 1 hour: NTP resync
├─ Every 30 seconds: Send status to Firebase
├─ Every 2 seconds: Update OLED display
└─ Every 10 seconds: Log system status
```

### Command Processing Flow

```
Firebase (user adds command)
    ↓
StreamManager (receives event)
    ↓
CommandProcessor (routes command)
    ↓
SerialProtocol (sends to Feeding ESP)
    ↓
Feeding ESP (executes command)
    ↓
SerialProtocol (receives status/confirmation)
    ↓
Firebase (updates status)
    ↓
OLED Display (updates UI)
```

---

## 🎯 Phase Status

### ✅ Phase 1: Foundation (COMPLETE)
- LogManager with 5 log levels
- MemoryMonitor with leak detection
- Watchdog timer
- Modular configuration

### ✅ Phase 2: WiFi ESP Refactor (COMPLETE)
- DeviceManager
- TimeManager
- WiFiConnectionManager
- FirebaseManager
- StreamManager
- CommandProcessor
- SerialProtocol
- OLEDDisplay

### 🔮 Future Phases
- **Phase 3**: FreeRTOS task architecture
- **Phase 4-5**: Feeding ESP refactor
- **Phase 6**: Production hardening
- **Phase 7**: Documentation & handoff

---

## 📊 Build Environments

### `esp32dev` (Development)
```ini
build_flags = -DDEV_BUILD
```
- **Logging**: Full DEBUG level
- **Optimizations**: Minimal (for debugging)
- **Use Case**: Development & testing

### `esp32prod` (Production)
```ini
build_flags = -DPROD_BUILD
```
- **Logging**: WARN level only
- **Optimizations**: Maximum (-Os)
- **Use Case**: Production deployment

### `native` (Unit Testing)
- Runs tests on local machine (no ESP32 required)
- Fast iteration for logic testing

---

## 🔧 Configuration Files

### `config/Config.h` - Hardware Pins
```cpp
// Serial2 pins (Feeding ESP communication)
#define RXD2 16
#define TXD2 17

// OLED Display (SPI)
#define OLED_MOSI 23
#define OLED_CLK 18
#define OLED_DC 2
#define OLED_RESET 4

// WiFiManager button
#define PORTAL_BUTTON_PIN 33
```

### `config/Credentials.h` - Firebase API
```cpp
#define API_KEY "AIzaSy..."
#define DATABASE_URL "https://your-project.firebasedatabase.app"
```
**⚠️ GITIGNORED - Never commit this file!**

### `config/TimingConfig.h` - Intervals
```cpp
#define FIREBASE_RECONNECT_INTERVAL 30000   // 30s
#define WIFI_RECONNECT_INTERVAL 60000       // 60s
#define TIME_SYNC_INTERVAL 3600000          // 1 hour
```

### `config/BufferConfig.h` - Buffer Sizes
```cpp
#define STREAM_SSL_BUFFER_SIZE 2048
#define STREAM_SSL_RECEIVE_BUFFER 512
#define STREAM_RESPONSE_SIZE 2048
```

---

## 📈 Memory Usage

| Component | RAM Usage |
|-----------|-----------|
| WiFi Stack | ~50 KB |
| Firebase SSL | ~60 KB |
| Application | ~10 KB |
| **Free Heap** | **~200 KB** |
| **Total** | **320 KB** |

**Flash**: 96.4% used (1,264 KB / 1,310 KB)

---

## 📝 Logging Examples

```cpp
// Different log levels
LOG_DEBUG("WiFi signal strength: %d dBm", rssi);
LOG_INFO("Device initialized: %s", deviceId);
LOG_WARN("Memory low: %u bytes free", freeHeap);
LOG_ERROR("Firebase connection failed: %s", error);
LOG_CRITICAL("Watchdog timeout - system reset imminent");
```

**Output**:
```
[      1234] [DEBUG] [main.cpp:45] WiFi signal strength: -52 dBm
[      2345] [INFO ] [DeviceManager.cpp:35] Device initialized: ESP_10061C685218_6a8cc8
[      3456] [WARN ] [MemoryMonitor.cpp:89] Memory low: 45000 bytes free
[      4567] [ERROR] [FirebaseManager.cpp:156] Firebase connection failed: Timeout
[      5678] [CRITICAL] [Watchdog.cpp:67] Watchdog timeout - system reset imminent
```

---

## 🐛 Debugging Tips

### Memory Leak Detection
```cpp
MemoryMonitor::checkpoint("Start");
// ... operation ...
MemoryMonitor::checkpoint("End");
MemoryMonitor::printCheckpoints();  // Shows delta
```

### View Recent Logs
```cpp
LogManager::getInstance().dumpRecentLogs();  // Last 50 logs
```

### Firebase Stream Issues
```cpp
// Enable verbose logging
streamManager.setVerbose(true);
```

### Serial Protocol Issues
```cpp
// Monitor Serial2 traffic
LOG_DEBUG("TX: %s", message);  // Outgoing
LOG_DEBUG("RX: %s", message);  // Incoming
```

---

## 🧪 Testing (Future)

```bash
# Run all tests
pio test

# Run specific test
pio test -e native -f test_device_manager
```

---

## 🤝 Contributing

1. Create feature branch from `dev`
2. Make changes
3. Test with `esp32dev` build
4. Verify memory usage hasn't increased significantly
5. Submit PR with clear description

---

## 📄 License

Proprietary - BitBite Horse Feeder System

---

## 🆘 Troubleshooting

### "Firebase not ready"
- Check WiFi connection
- Verify `Credentials.h` has correct API_KEY and DATABASE_URL
- Check serial monitor for authentication errors

### "Watchdog timeout"
- Operation taking too long (>30s)
- Add `Watchdog::feed()` inside long loops
- Consider moving to background task (Phase 3)

### OLED display blank
- Check SPI pin connections
- Verify `oledDisplay.begin()` returns true
- Check I2C address (0x3C default for SSD1306)

### Serial2 not receiving data
- Check RX/TX pin connections (RX2=16, TX2=17)
- Verify baud rate matches Feeding ESP (9600)
- Test with loopback (connect RX2 to TX2)

### High memory usage
- Check for memory leaks with MemoryMonitor
- Reduce buffer sizes in `BufferConfig.h`
- Clear Firebase objects after use: `json.clear()`, `fbdo.clear()`

---

**Current Build**: Phase 2 Complete - Production Ready
**Last Updated**: 2026-01-17
**Flash Usage**: 96.4% (1,264 KB / 1,310 KB)
**RAM Usage**: 21.0% (68 KB / 320 KB)
