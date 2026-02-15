#include "OLEDDisplay.h"
#include "../config/Config.h"
#include "../core/LogManager.h"
#include <SPI.h>

// ============================================================================
// CONSTRUCTOR
// ============================================================================

OLEDDisplay::OLEDDisplay()
    : display_(SCREEN_WIDTH, SCREEN_HEIGHT, &SPI, OLED_DC, OLED_RESET, OLED_CS) {
    // Using hardware SPI - faster than software SPI
}

// ============================================================================
// INITIALIZATION
// ============================================================================

bool OLEDDisplay::begin() {
    LOG_INFO("Initializing OLED display...");

    // Initialize hardware SPI with custom pins (MOSI=23, CLK=18)
    SPI.begin(OLED_CLK, -1, OLED_MOSI, -1);  // SCK, MISO (unused), MOSI, SS (unused)

    if (!display_.begin(SSD1306_SWITCHCAPVCC)) {
        LOG_ERROR("OLED initialization failed");
        return false;
    }

    LOG_INFO("OLED display initialized");

    display_.clearDisplay();
    display_.setTextColor(SSD1306_WHITE);
    display_.display();

    return true;
}

// ============================================================================
// DISPLAY UPDATES
// ============================================================================

void OLEDDisplay::update(const SensorStatus& status, int wifiRSSI, bool wifiConnected) {
    display_.clearDisplay();

    // WiFi icon in top right
    drawWiFiIcon(110, 0, wifiRSSI, wifiConnected);

    // Sensor data
    drawTemperature(status.temperature);
    drawHumidity(status.humidity);
    drawWaterFlow(status.waterFlow);

    // Status indicators
    drawFeedingIndicator(status.isFeeding);
    drawFaultIndicator(status.activeFaults);

    // Divider lines
    drawDividers();

    display_.display();
}

void OLEDDisplay::showStartup(const char* message) {
    display_.clearDisplay();
    display_.setTextSize(1);
    display_.setCursor(0, 0);
    display_.println("BitBite");
    display_.println(message);
    display_.display();
}

void OLEDDisplay::clear() {
    display_.clearDisplay();
    display_.display();
}

// ============================================================================
// DRAWING HELPERS
// ============================================================================

void OLEDDisplay::drawTemperature(float temp) {
    display_.setTextSize(1);
    display_.setCursor(0, 0);
    display_.print("Temp:");

    if (temp == -999) {
        display_.setTextSize(2);
        display_.setCursor(45, 0);
        display_.print("--");
    } else {
        display_.setTextSize(2);
        display_.setCursor(45, 0);
        display_.print(temp, 1);
        display_.setTextSize(1);
        display_.setCursor(95, 2);
        display_.print("C");
        display_.drawCircle(90, 2, 2, SSD1306_WHITE);
    }
}

void OLEDDisplay::drawHumidity(float humidity) {
    display_.setTextSize(1);
    display_.setCursor(0, 20);
    display_.print("Humid:");

    if (humidity == -999) {
        display_.setTextSize(2);
        display_.setCursor(45, 18);
        display_.print("--");
    } else {
        display_.setTextSize(2);
        display_.setCursor(45, 18);
        display_.print(humidity, 1);
        display_.setTextSize(1);
        display_.setCursor(95, 22);
        display_.print("%");
    }
}

void OLEDDisplay::drawWaterFlow(float flow) {
    display_.setTextSize(1);
    display_.setCursor(0, 38);
    display_.print("Water:");
    display_.setTextSize(2);
    display_.setCursor(35, 38);
    display_.print(flow, 2);
    display_.print("L");
}

void OLEDDisplay::drawFeedingIndicator(bool isFeeding) {
    if (isFeeding) {
        display_.fillCircle(120, 56, 3, SSD1306_WHITE);
    }
}

void OLEDDisplay::drawFaultIndicator(uint8_t faults) {
    if (faults != 0) {
        display_.setCursor(100, 38);
        display_.print("!");
        display_.drawCircle(104, 42, 6, SSD1306_WHITE);
    }
}

void OLEDDisplay::drawDividers() {
    display_.drawLine(0, 16, 128, 16, SSD1306_WHITE);
    display_.drawLine(0, 36, 128, 36, SSD1306_WHITE);
}

void OLEDDisplay::drawWiFiIcon(int x, int y, int rssi, bool connected) {
    // Calculate signal strength bars (0-4)
    int bars = 0;
    if (rssi >= -50) bars = 4;
    else if (rssi >= -60) bars = 3;
    else if (rssi >= -70) bars = 2;
    else if (rssi >= -80) bars = 1;

    // Draw WiFi bars
    for (int i = 0; i < 4; i++) {
        int barHeight = (i + 1) * 3;
        int barX = x + (i * 4);
        int barY = y + 12 - barHeight;

        if (i < bars && connected) {
            display_.fillRect(barX, barY, 3, barHeight, SSD1306_WHITE);
        } else {
            display_.drawRect(barX, barY, 3, barHeight, SSD1306_WHITE);
        }
    }

    // Draw X if disconnected
    if (!connected) {
        display_.drawLine(x, y, x + 14, y + 12, SSD1306_WHITE);
        display_.drawLine(x + 14, y, x, y + 12, SSD1306_WHITE);
    }
}
