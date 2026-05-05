#pragma once

// Board configuration selection
// Include this file to get hardware definitions for the target board

#if defined(BOARD_M5STICKS3)
#include "board_m5sticks3.h"
#else
// Default to Waveshare ESP32-S2-LCD-0.96
#include "board_esp32s2_waveshare.h"
#endif
