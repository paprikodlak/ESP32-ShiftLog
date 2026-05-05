#pragma once

// Waveshare ESP32-S2-LCD-0.96 board profile
// Display: ST7735S 160x80 TFT (rotated 3)
// Button: GPIO 0 (BOOT, active-low with pullup)
// Serial: UART0 on GPIO43/44 at 115200

namespace shiftlog::board {

// Display pins
constexpr uint8_t DISPLAY_RST = 21;
constexpr uint8_t DISPLAY_BL = 45;
constexpr uint8_t DISPLAY_DC = 18;
constexpr uint8_t DISPLAY_CS = 9;
constexpr uint8_t DISPLAY_MOSI = 11;
constexpr uint8_t DISPLAY_MISO = 12;
constexpr uint8_t DISPLAY_SCK = 10;

// Display configuration
constexpr uint16_t DISPLAY_WIDTH = 160;
constexpr uint16_t DISPLAY_HEIGHT = 80;
constexpr uint8_t DISPLAY_ROTATION = 3;
constexpr const char* DISPLAY_TYPE = "ST7735S 160x80";

// Button pins
constexpr uint8_t BUTTON_PIN = 0;
constexpr bool BUTTON_ACTIVE_LOW = true;
constexpr bool BUTTON_USE_PULLUP = true;

// Serial configuration
constexpr uint32_t SERIAL_BAUD = 115200;

// Features
constexpr bool HAS_USB_CDC = true;
constexpr bool HAS_WIFI = false;  // No antenna, requires external

}  // namespace shiftlog::board
