#pragma once

// ============================================================================
// HARDWARE PIN CONFIGURATION
// ============================================================================

// Serial2 Communication with Feeding ESP
#define RXD2 16
#define TXD2 17

// Portal Button (for WiFiManager)
#define PORTAL_BUTTON_PIN 33

// OLED Display Pins (SPI)
#define OLED_MOSI  23
#define OLED_CLK   18
#define OLED_DC    2
#define OLED_RESET 4   // Hardware reset pin
#define OLED_CS    5   // Dummy CS pin (not physically connected, but avoids GPIO errors)

// Screen Dimensions
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
