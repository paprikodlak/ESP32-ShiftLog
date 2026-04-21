# ESP32-ShiftLog - Product Requirements

## 1) End Goal
Build a reliable single-button shift logger for Waveshare ESP32-S2-LCD-0.96 that lets a user:
- select a project,
- start/stop time tracking,
- keep accurate timestamps,
- store logs safely in flash,
- and export data to a host PC without corrupting storage.

The device must be usable daily with minimal interaction and clear on-screen feedback.

## 2) Hardware Scope
- MCU: ESP32-S2.
- Display: ST7735S 160x80 TFT.
- Input: BOOT button on GPIO 0 as the primary UI control.
- Storage: LittleFS (projects + time log data).
- USB: native ESP32-S2 USB peripheral.

## 3) Software Stack (Standard and Current)
Use Arduino framework on PlatformIO with actively maintained libraries:
- Display: `Adafruit GFX` + `Adafruit ST7735 and ST7789 Library`.
- Button handling: `OneButton`.
- Wi-Fi provisioning: `WiFiManager`.
- Data serialization: `ArduinoJson`.
- Time: ESP32 SNTP/NTP APIs from Arduino-ESP32 (`configTime`, `getLocalTime`).

USB requirement correction:
- Do not add `Adafruit TinyUSB Library` in `lib_deps` for this ESP32-S2 PlatformIO target.
- Use Arduino-ESP32 native USB APIs (for example `USB.h`/MSC support in the core) to avoid TinyUSB symbol conflicts.

## 4) Core Functional Requirements

### 4.1 Project and Tracking Flow
- User can cycle through projects with short press.
- Long press toggles Start/Stop tracking for the selected project.
- If a different project is selected while another is running, UI shows pending switch state.
- Confirmation delay: after project selection change, wait 3 seconds of no further input, then switch tracking automatically.

### 4.2 Time Management
- On boot, attempt Wi-Fi auto-connect.
- If no known credentials/network, open WiFiManager config portal.
- After connectivity, sync time from NTP.
- Provide manual time-set fallback path when NTP is unavailable.
- Device must not write log entries with invalid epoch/time state.

### 4.3 UI/UX Behavior
- Main screen must always show:
	- current project,
	- tracking state (running/stopped),
	- Wi-Fi status icon,
	- clock/time validity indicator.
- Pending project switch uses blinking text at 500 ms period (non-blocking via `millis()`).
- No UI action may block loop responsiveness longer than 50 ms, except controlled transitions during startup.

### 4.4 Data Persistence
- Persist project list in LittleFS (`projects.json`).
- Persist time log in append-safe format (JSON lines or CSV with explicit schema).
- Flush and close writes predictably to survive power loss.
- On boot, validate file integrity; if malformed, preserve raw file and create a recoverable fallback.

### 4.5 USB Export / Storage Safety
- Device supports USB mass-storage style export of the log/data partition or an equivalent readout workflow.
- Mandatory storage lock rule: when host owns mounted storage, firmware must switch to read-only/no-write mode for affected files.
- UI must show explicit `USB MODE` indicator while write lock is active.

## 5) State Machine Requirements
Implement explicit state machine with at least:
- `IDLE`
- `TRACKING`
- `SELECTING`
- `SETTINGS`
- `USB_MODE` (or equivalent guard state)

Each state must define:
- accepted input events,
- transition conditions,
- allowed file-write behavior,
- UI rendering behavior.

## 6) Project Format Rules
- Project code format: `XXXX.X` (example: `1234.5`).
- Input validation must reject invalid format before save.
- Maximum project count must be defined and enforced.

## 7) Non-Functional Requirements
- Non-blocking main loop design.
- Deterministic button handling (debounced, click/long-press separated).
- Safe behavior on Wi-Fi failure and on filesystem errors.
- Serial debug logging behind compile-time flag.
- Build reproducibly with pinned PlatformIO library versions.

## 8) Acceptance Criteria
Done means all checks below pass:
- Can add/select projects and start/stop tracking using only GPIO 0.
- Project switch delay works exactly at 3 seconds inactivity.
- NTP sync works when Wi-Fi is available; manual set works offline.
- Logs survive reboot and power interruption tests.
- USB export workflow does not corrupt LittleFS data.
- UI clearly indicates tracking, Wi-Fi, and USB/write-lock status.
- Firmware builds cleanly in PlatformIO on current stable espressif32 platform.

## 9) Out of Scope (for now)
- Multi-button UI redesign.
- Cloud synchronization.
- Mobile app integration.
- Complex analytics/reporting on-device.

## 10) Incremental Implementation Plan (Verify Each Part First)

### 10.1 Development Strategy
- Build core features as reusable modules first, then integrate through thin glue code in main.
- Every step must pass its own test checklist before moving forward.
- Use feature flags to enable isolated tests from main without deleting production code paths.

Suggested module layout:
- lib/hal_display: display init, drawing primitives, text and icon helpers.
- lib/hal_button: OneButton setup, event translation (short, long, hold).
- lib/core_clock: time source abstraction, NTP sync, manual set, time-valid state.
- lib/core_storage: LittleFS mount, projects file, log append, integrity checks.
- lib/core_tracker: project selection, running session state, switch-delay logic.
- lib/core_ui: screen models, blink timing, render orchestration.
- lib/core_usb_guard: USB mode detection, write lock policy.
- lib/app_controller: state machine and module wiring.

### 10.2 Step-by-Step Build Order

1. Board bring-up and diagnostics
- Goal: verify serial, loop timing, watchdog-safe runtime, basic pin map.
- Exit criteria: stable boot and periodic heartbeat without blocking.

2. Display HAL module
- Implement: display init, clear, text draw, tiny icon draw, buffered update helpers.
- Test app: color bars + static labels + refresh stress test for 5 minutes.
- Exit criteria: no crashes, no visual corruption, predictable redraw timing.

3. Button HAL module
- Implement: OneButton callbacks converted into internal events.
- Test app: live event counter on display and serial (short and long presses).
- Exit criteria: no false double events during 200 manual presses.

4. Time module (clock abstraction)
- Implement: time-valid flag, NTP sync path, manual set API, monotonic checks.
- Test app: run with Wi-Fi and without Wi-Fi, force manual fallback.
- Exit criteria: valid timestamp available in both online and offline workflows.

5. Storage module (LittleFS)
- Implement: mount, projects read/write, append-safe log writer, corruption fallback.
- Test app: create and reload projects, append 1000 log lines, reboot and verify.
- Exit criteria: persisted data remains intact across reboot and power cycle.

6. Tracker logic module
- Implement: selected project, active project, start/stop, 3-second pending switch.
- Test app: simulated button events and real button events.
- Exit criteria: transition table matches spec with deterministic outcomes.

7. UI module
- Implement: screens for idle, tracking, selecting, settings; 500 ms blink; status icons.
- Test app: feed fake model data to renderer (no storage dependency required).
- Exit criteria: all required indicators appear correctly; no blocking redraw loops.

8. USB guard module
- Implement: USB mode flag, storage write lock enforcement, on-screen USB indicator.
- Test app: mount/export path and verify write attempts are blocked in USB mode.
- Exit criteria: zero writes happen while host-controlled storage mode is active.

9. App controller integration
- Implement: state machine glue for all modules with explicit transitions.
- Test app: full user journey using only GPIO 0.
- Exit criteria: all transitions valid for IDLE, TRACKING, SELECTING, SETTINGS, USB_MODE.

10. System integration test pass
- Run full acceptance suite from section 8.
- Add regression scripts/checklists for future firmware updates.
- Exit criteria: every acceptance item is verified and repeatable.

## 11) Dependency Tree for Progress
Use this tree to decide what can be developed and tested in parallel.

- Foundation
	- Board bring-up
	- Build config and logging
- HAL Layer
	- Display HAL (depends on Foundation)
	- Button HAL (depends on Foundation)
	- Storage HAL/LittleFS mount (depends on Foundation)
	- USB Guard HAL (depends on Foundation + Storage HAL)
- Core Services
	- Time Service (depends on Foundation, optional Wi-Fi)
	- Storage Service (depends on Storage HAL)
	- Tracker Service (depends on Button HAL + Time Service + Storage Service)
- UI Layer
	- Renderer (depends on Display HAL)
	- UI Presenter/ViewModel (depends on Tracker Service + Time Service + USB Guard HAL)
- Application Layer
	- App State Machine (depends on Button HAL + UI Presenter + Tracker Service + USB Guard HAL)
	- Main Integration (depends on all above)

Practical sequencing rule:
- GUI integration starts only after Display HAL and Button HAL are validated.
- Persistent tracking starts only after Time Service and Storage Service pass standalone tests.
- USB export is integrated only after Storage Service is proven stable.

## 12) Verification Matrix (Per Step)
Each step above must include these checks before merge:
- Unit-level behavior check (where feasible on host or via lightweight harness).
- On-device functional test.
- Reboot persistence check (if storage or state involved).
- Negative-path check (invalid input, missing Wi-Fi, malformed data).
- Performance check: loop remains responsive and non-blocking.

## 13) Using ESP-Prog with Waveshare ESP32-S2-LCD-0.96

### 13.1 Why use ESP-Prog here
- Stable UART flashing and serial logging during bring-up.
- Easier recovery if native USB firmware configuration becomes invalid.
- Optional external debug workflow preparation.

### 13.2 Wiring (UART flashing)
Use the board silk labels where available. Typical ESP32-S2 UART0 mapping is:
- U0TXD: GPIO43
- U0RXD: GPIO44
- BOOT: GPIO0
- EN: reset/enable

Connect ESP-Prog to target as follows:
- ESP-Prog `GND` -> board `GND`
- ESP-Prog `TXD` -> board `U0RXD` (GPIO44)
- ESP-Prog `RXD` -> board `U0TXD` (GPIO43)
- ESP-Prog `IO0` -> board `BOOT` (GPIO0)
- ESP-Prog `EN` (or `RST`) -> board `EN`

Power rule:
- Use exactly one primary power source for the target board at a time.
- If USB already powers the board, do not additionally feed conflicting power rails from ESP-Prog.

### 13.3 PlatformIO profile for ESP-Prog
For ESP-Prog sessions, use a dedicated environment profile (recommended):
- Keep `upload_protocol = esptool`.
- Set `upload_port` to ESP-Prog COM port.
- Set `monitor_port` to the same COM port.
- Keep `monitor_speed = 115200`.

USB CDC note:
- Current project enables `-DARDUINO_USB_CDC_ON_BOOT=1` for native USB serial.
- When primarily using ESP-Prog UART monitor, disable that flag in the ESP-Prog profile to avoid split/confusing serial behavior.

### 13.4 Flash workflow
1. Connect wiring above and confirm COM port appears.
2. Build firmware.
3. Upload via ESP-Prog from PlatformIO.
4. If auto-reset does not enter bootloader:
- hold BOOT,
- tap RESET/EN,
- release BOOT after upload starts.
5. Open serial monitor and verify boot log and heartbeat.

### 13.5 Step-gate checks with ESP-Prog (matches Section 10)
- Step 1 (bring-up): verify clean boot logs every reset.
- Step 2 (display): watch for redraw timing and crash-free operation for 5 minutes.
- Step 3 (button): press test with event counters over UART logs.
- Step 4 (time): verify online NTP and offline/manual fallback paths in logs.
- Step 5 (storage): run write/reboot/read loops; verify no file corruption.
- Step 8 (USB guard): while host owns storage mode, confirm blocked writes are reported in logs.

### 13.6 Common failure patterns and fixes
- Upload timeout / no sync:
	- swap TX/RX lines,
	- verify GND common,
	- use manual BOOT + RESET sequence.
- Garbled serial output:
	- verify `monitor_speed` matches firmware baud.
- Random resets under load:
	- check power stability and avoid dual-power conflicts.
- No auto-programming:
	- check IO0/EN wiring; fall back to manual BOOT sequence.