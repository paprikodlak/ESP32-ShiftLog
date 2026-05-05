# Agent Instructions for ESP32-ShiftLog Development

## Project Overview

This is the **ESP32-ShiftLog** project: a reliable single-button shift logger built on Waveshare ESP32-S2-LCD-0.96 or similar hardware. The device enables users to:
- Select projects
- Start/stop time tracking with a single button
- Maintain accurate timestamps via NTP
- Store logs safely in flash storage
- Export data to a PC without corrupting storage

For detailed product requirements, see [GOAL.md](GOAL.md).

---

## Core Development Principles

### 1. Technology Stack
- **MCU**: ESP32-S2 or ESP32-S3
- **Framework**: Arduino on PlatformIO
- **Display**: ST7735S/ST7789 TFT via `Adafruit GFX` + `Adafruit ST7735/ST7789 Library`
- **Button**: `OneButton` library on GPIO 0 (BOOT)
- **WiFi**: `WiFiManager` for provisioning
- **Storage**: LittleFS for projects.json and logs
- **JSON**: `ArduinoJson`
- **Time**: ESP32 SNTP/NTP from Arduino-ESP32 core

### 2. Critical Constraints
- **No TinyUSB conflicts**: Do NOT add `Adafruit TinyUSB Library` to `lib_deps`; use Arduino-ESP32 native USB APIs (`USB.h`, `USBCDC`)
- **Response time**: No UI action may block the main loop longer than 50 ms (except controlled startup transitions)
- **Data safety**: Files must flush and close predictably; implement recovery fallback for malformed data
- **USB safety**: When host owns mounted storage, device must switch to read-only mode for affected files
- **C++ porting**: Add forward declarations in `main.cpp` for functions that Arduino IDE auto-prototypes

---

## Development Workflow

### Before Starting Work
1. **Review scope**: Read the relevant section of [GOAL.md](GOAL.md) for the feature or fix you're implementing
2. **Check constraints**: Ensure your approach respects the core constraints above
3. **Plan commits**: Identify logical breakpoints where git commits will be made

### During Implementation
1. **Write incremental changes**: Keep changes focused and testable
2. **Test on target hardware** (or simulator if available):
   - Verify UI responsiveness (no blocking >50 ms)
   - Check storage operations (writes, flushes, recovery)
   - Test button interaction and state transitions
3. **Follow code style**: Use the existing code in the project as a reference
4. **Add forward declarations** if writing new functions in `main.cpp`

### After Major Changes
**MANDATORY**: Commit to git after completing a logical unit of work. Examples of major changes:
- Adding/modifying a core feature (e.g., project switching, time sync, USB export)
- Fixing a significant bug (especially storage/USB-related)
- Refactoring subsystems (button handler, display, storage layer)
- Adding new dependencies or changing platformio.ini
- Updating configuration structures

**Commit message format:**
```
[FEATURE|FIX|REFACTOR|CONFIG] Brief description

Detailed explanation of what changed and why, if non-obvious.
```

Example:
```
[FEATURE] Implement low-battery LED indicator

Added battery voltage monitoring via ADC on GPIO 2. LED shows:
- Blinking red (2 Hz) when voltage < 5%
- Smooth transitions via millis() to avoid blocking loop
- Graceful fallback if ADC unavailable
```

---

## Key Functional Areas

### Project & Tracking Flow
- **Short press**: Cycle through projects
- **Long press**: Toggle Start/Stop for selected project
- **Project switch with tracking**: Show pending state with 500 ms blinking text
- **Auto-switch**: After 3 seconds of no input, finalize project change

See GOAL.md § 4.1 for details.

### Time Management
- Boot sequence: Wi-Fi auto-connect → WiFiManager fallback → NTP sync → manual time-set path
- **Validation rule**: Never write log entries with invalid epoch/time state
- Maintain `time_valid` flag throughout execution

See GOAL.md § 4.2 for details.

### UI/UX Requirements
- Main screen must show: current project, tracking state, Wi-Fi status, time validity indicator
- Non-blocking animations (500 ms blink) via `millis()`, not `delay()`
- Maintain loop responsiveness at all times

See GOAL.md § 4.3 for details.

### Data Persistence
- Projects: stored in `projects.json` (LittleFS)
- Time logs: append-safe format (JSON lines or CSV)
- Recovery: validate on boot; if malformed, preserve raw file + create fallback
- **Critical**: Flush and close writes explicitly to survive power loss

See GOAL.md § 4.4 for details.

### USB Export & Storage Safety
- When host owns mounted storage, firmware must enter read-only mode
- Display explicit "USB MODE" indicator
- Prevent data corruption via storage lock during USB access

See GOAL.md § 4.5 for details.

### Battery Management (if enabled)
- Monitor voltage via ADC
- Display battery icon: high (≥20%), medium (5–20%), low (<5%)
- On low battery (<3%), disable Wi-Fi and non-critical features
- Do NOT prevent logging; ensure continued operation until hardware cutoff
- Configuration: `BATTERY_ADC_PIN`, `BATTERY_ADC_CHANNEL`, voltage thresholds (min/max/shutdown)

See GOAL.md § 4.6 & 4.7 for details.

---

## Git Commit Checklist

Before committing, verify:
- [ ] Code compiles without warnings (run `pio run -e <target>`)
- [ ] Feature is tested and works as intended
- [ ] No blocking delays >50 ms introduced in main loop
- [ ] Storage operations flush/close predictably
- [ ] New functions have forward declarations (if in `main.cpp`)
- [ ] No `Adafruit TinyUSB` added to dependencies
- [ ] Commit message follows the format above

---

## Testing & Validation

### Local Compilation
```bash
pio run -e m5stack-sticks3  # or other target in platformio.ini
```

### Hardware Testing Checklist
- [ ] Button responses are immediate (no lag)
- [ ] Display updates smoothly without flicker
- [ ] Time is synced from NTP on boot
- [ ] Projects persist across reboot
- [ ] Log entries are saved to LittleFS
- [ ] USB export doesn't corrupt storage
- [ ] Battery monitoring displays correctly (if enabled)
- [ ] LED states match specification (if enabled)

### Edge Cases to Verify
- Power loss during file write
- Switching projects while tracking
- No network / NTP unavailable
- Low battery conditions
- USB connection/disconnection
- Multiple rapid button presses

---

## File Structure Reference

```
src/
  main.cpp              # Main firmware entry point (add forward declarations here)
include/
  config/               # Board configurations
    board_esp32s2_waveshare.h
    board_m5sticks3.h
lib/
  core_clock/           # Time management subsystem
  core_storage/         # LittleFS storage layer
  core_tracker/         # Tracking logic
  hal_button/           # Button handler (OneButton wrapper)
  hal_display/          # Display driver (ST7735/ST7789)
data/
  projects.json         # Project list (created/updated at runtime)
platformio.ini          # PlatformIO configuration
GOAL.md                 # Product requirements (read first!)
```

---

## Common Tasks

### Adding a New Feature
1. Read the relevant section in [GOAL.md](GOAL.md)
2. Plan the implementation (which subsystems need changes?)
3. Update headers in `lib/` or `include/config/` if needed
4. Implement in `src/main.cpp` or create a new library
5. Add forward declarations
6. Compile and test
7. **Commit with detailed message**

### Fixing a Bug
1. Reproduce the bug (note the exact conditions)
2. Locate the problematic code
3. Implement fix (minimal change, don't refactor surrounding code)
4. Test the fix thoroughly
5. **Commit with bug description and fix explanation**

### Refactoring
1. Identify the code smell (e.g., repeated logic, unclear structure)
2. Plan refactoring scope (single module, multiple modules?)
3. Refactor incrementally (keep changes focused)
4. Test after each refactoring step
5. **Commit after each refactoring unit**

### Updating Dependencies
1. Update `platformio.ini` (lib_deps section)
2. Run `pio update` to fetch new libraries
3. Check for breaking API changes
4. Update code if needed
5. Compile and test
6. **Commit with dependency change details**

---

## Resources

- [ESP32 Arduino Core Docs](https://docs.espressif.com/projects/arduino-esp32/en/latest/)
- [PlatformIO Docs](https://docs.platformio.org/)
- [Adafruit GFX Library](https://github.com/adafruit/Adafruit-GFX-Library)
- [OneButton Library](https://github.com/mathertel/OneButton)
- [ArduinoJson](https://arduinojson.org/)
- [WiFiManager](https://github.com/tzapu/WiFiManager)

---

## Summary

Your role is to advance the ESP32-ShiftLog firmware toward the goals defined in GOAL.md. Work in focused, testable units and **commit after each major change**. Respect the core constraints (no blocking loops, no TinyUSB conflicts, storage safety), and always verify your changes work on target hardware before moving to the next task.

Good luck! 🚀
