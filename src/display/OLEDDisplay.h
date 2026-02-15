#pragma once

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ============================================================================
// OLED DISPLAY MANAGER
// ============================================================================
// Manages SSD1306 OLED display (128x64)
// Shows: Temperature, Humidity, Water Flow, WiFi status, Feeding indicator, Faults

// Sensor status data
struct SensorStatus {
    float temperature;
    float humidity;
    float waterFlow;
    bool isFeeding;
    uint8_t activeFaults;

    SensorStatus() :
        temperature(-999),
        humidity(-999),
        waterFlow(0),
        isFeeding(false),
        activeFaults(0) {}
};

class OLEDDisplay {
public:
    OLEDDisplay();

    // Initialize display
    bool begin();

    // Update display with current status
    void update(const SensorStatus& status, int wifiRSSI, bool wifiConnected);

    // Show startup message
    void showStartup(const char* message);

    // Clear display
    void clear();

private:
    Adafruit_SSD1306 display_;

    // Drawing helpers
    void drawWiFiIcon(int x, int y, int rssi, bool connected);
    void drawTemperature(float temp);
    void drawHumidity(float humidity);
    void drawWaterFlow(float flow);
    void drawFeedingIndicator(bool isFeeding);
    void drawFaultIndicator(uint8_t faults);
    void drawDividers();
};
