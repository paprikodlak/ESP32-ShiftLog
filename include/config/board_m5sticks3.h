#pragma once

// M5StickS3 board profile (ESP32-S3)
// Pin map source: project pin assignment table
// Serial: UART0 on GPIO43/44 at 115200

namespace shiftlog::board {

// LCD (ST7789P3)
constexpr uint8_t DISPLAY_MOSI = 39;
constexpr uint8_t DISPLAY_SCK = 40;
constexpr uint8_t DISPLAY_DC = 45;      // RS/DC
constexpr uint8_t DISPLAY_CS = 41;
constexpr uint8_t DISPLAY_RST = 21;
constexpr uint8_t DISPLAY_BL = 38;
constexpr uint8_t DISPLAY_MISO = 255;   // not connected

// Shared I2C for BMI270 (0x68), M5PM1 (0x6E) and ES8311 control (0x18)
constexpr uint8_t I2C_SCL = 48;
constexpr uint8_t I2C_SDA = 47;

// M5PM1 signals
constexpr uint8_t PM1_IRQ = 1;          // PYG1_IRQ -> ESP32 GPIO1
constexpr uint8_t PM1_L3B_EN = 2;       // PYG2_L3B_EN
constexpr uint8_t PM1_SPK_PULSE = 3;    // PYG3_SPK_Pulse
constexpr uint8_t PM1_IMU_INT = 4;      // PYG4_IMU_INT

// Audio (ES8311)
constexpr uint8_t AUDIO_MCLK = 18;
constexpr uint8_t AUDIO_DOUT = 14;
constexpr uint8_t AUDIO_BCLK = 17;
constexpr uint8_t AUDIO_LRCK = 15;
constexpr uint8_t AUDIO_DIN = 16;

// Buttons
constexpr uint8_t BUTTON_KEY1_PIN = 11;
constexpr uint8_t BUTTON_KEY2_PIN = 12;

// Display configuration
constexpr uint16_t DISPLAY_WIDTH = 135;
constexpr uint16_t DISPLAY_HEIGHT = 240;
constexpr uint8_t DISPLAY_ROTATION = 1;
constexpr const char* DISPLAY_TYPE = "ST7789 135x240";

// IR
constexpr uint8_t IR_TX_PIN = 46;
constexpr uint8_t IR_RX_PIN = 42;

// HY2.0-4P PORT.CUSTOM
constexpr uint8_t PORT_CUSTOM_YELLOW = 9;
constexpr uint8_t PORT_CUSTOM_WHITE = 10;

// Hat2-Bus (right side GPIO pins)
constexpr uint8_t HAT2_RIGHT_PIN2 = 5;
constexpr uint8_t HAT2_RIGHT_PIN4 = 4;
constexpr uint8_t HAT2_RIGHT_PIN6 = 6;
constexpr uint8_t HAT2_RIGHT_PIN8 = 7;
constexpr uint8_t HAT2_RIGHT_PIN10 = 43;
constexpr uint8_t HAT2_RIGHT_PIN12 = 44;
constexpr uint8_t HAT2_RIGHT_PIN14 = 2;
constexpr uint8_t HAT2_RIGHT_PIN16 = 3;

// Default input button for app flow
const uint8_t BUTTON_PIN = BUTTON_KEY1_PIN;
constexpr bool BUTTON_ACTIVE_LOW = true;
constexpr bool BUTTON_USE_PULLUP = true;

// Serial configuration
constexpr uint32_t SERIAL_BAUD = 115200;

// Features
constexpr bool HAS_USB_CDC = false;
constexpr bool HAS_WIFI = true;

}  // namespace shiftlog::board
