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
- MCU: ESP32-S2 or ESP32-S3.
- Display: ST7735S/ST7789 color TFT.
- Input: BOOT button on GPIO 0 as the primary UI control.
- Storage: LittleFS (projects + time log data).
- USB: native ESP32 USB peripheral.
- Battery: Optional Li-Po battery management with ADC-based voltage monitoring and low-battery detection.
- Status LED: RGB or single-color LED on GPIO for indicating device state (running/stopped/offline/charging) when LCD is off.

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

### 4.6 Battery Management (if applicable)
- Monitor battery voltage via ADC on designated GPIO.
- Calculate estimated battery capacity percentage based on voltage curve mapping (empirical for Li-Po).
- Display battery icon with three states: high (≥20%), medium (5-20%), low (<5%).
- On low battery (<3%), disable Wi-Fi and non-critical features to extend runtime.
- Provide graceful shutdown warning if battery drops below 2.5V (battery protection cutoff threshold).
- Log battery state (voltage, capacity %) periodically to storage for post-analysis.
- Do not prevent logging when battery is low; ensure continued operation until hardware cutoff.

### 4.7 Status LED Indication (if enabled)
- LED shows device state when LCD is off (power-saving mode).
- LED patterns:
  - **Solid green**: tracking active.
  - **Blinking green (1 Hz)**: idle/selected project ready.
  - **Solid red**: offline, no valid time.
  - **Blinking red (2 Hz)**: low battery (<5%).
  - **Orange/yellow**: charging (if charger input connected).
  - **Off**: USB mode or device sleep.
- LED update: non-blocking, driven by millis() timer every 100 ms.
- Do not block loop; LED state calculation must complete in <10 ms.
- RGB LED (if supported): use same pin count as single-color but with more expressive state combinations.

## 5) Battery Configuration (if enabled)
When building with battery support (`-DENABLE_BATTERY=1`), define:
- `BATTERY_ADC_PIN`: GPIO pin connected to battery voltage divider (e.g., GPIO 2 on M5StickS3).
- `BATTERY_ADC_CHANNEL`: ADC channel (ADC1_CHANNEL_0 for GPIO 2, etc.).
- `BATTERY_VOLTAGE_MIN`: minimum usable voltage in mV (e.g., 3000 for Li-Po).
- `BATTERY_VOLTAGE_MAX`: nominal full-charge voltage in mV (e.g., 4200 for Li-Po).
- `BATTERY_VOLTAGE_SHUTDOWN`: critical shutdown threshold in mV (e.g., 2500).
- `BATTERY_ADC_SAMPLES`: number of samples to average per reading (e.g., 10 for smoothing).
- `BATTERY_UPDATE_INTERVAL_MS`: how often to poll ADC (e.g., 60000 for 1 minute).

## 6) LED Configuration (if enabled)
When building with LED support (`-DENABLE_STATUS_LED=1`), define:
- `STATUS_LED_PIN`: GPIO pin(s) (e.g., GPIO 35 on M5StickS3, or 3 pins for RGB).
- `STATUS_LED_TYPE`: "SINGLE" (on/off) or "RGB" (3 channels).
- `STATUS_LED_PWM_CHANNEL`: ESP32 LEDC PWM channel (0-15).
- `STATUS_LED_BRIGHTNESS_MAX`: PWM duty max (255).
- `STATUS_LED_UPDATE_INTERVAL_MS`: how often to refresh LED state (100 ms default).

## 7) State Machine Requirements
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

## 8) Project Format Rules
- Project code format: `XXXX.X` (example: `1234.5`).
- Input validation must reject invalid format before save.
- Maximum project count must be defined and enforced.

## 9) Non-Functional Requirements
- Non-blocking main loop design.
- Deterministic button handling (debounced, click/long-press separated).
- Safe behavior on Wi-Fi failure and on filesystem errors.
- Serial debug logging behind compile-time flag.
- Build reproducibly with pinned PlatformIO library versions.

## 10) Acceptance Criteria
Done means all checks below pass:
- Can add/select projects and start/stop tracking using only GPIO 0.
- Project switch delay works exactly at 3 seconds inactivity.
- NTP sync works when Wi-Fi is available; manual set works offline.
- Logs survive reboot and power interruption tests.
- USB export workflow does not corrupt LittleFS data.
- UI clearly indicates tracking, Wi-Fi, and USB/write-lock status.
- Firmware builds cleanly in PlatformIO on current stable espressif32 platform.
- Tracker state machine transitions match spec; pending switch executes at exactly 3 seconds.
- I11 battery module enabled: battery icon displays correctly, low-battery warnings appear, and log entries include voltage readings.
- If LED module enabled: tracking state shows correct pattern, low-battery blinks appear, offline state is distinguishable.

## 9) Out of Scope (for now)
- Multi-button UI redesign.
- Cloud synchronization.
- Mo2ile app integration.
- Complex analytics/reporting on-device.

## 10) Incremental Implementation Plan (Verify Each Part First)

### 10.1 Development Strategy
- Build core features as reusable modules first, then integrate through thin glue code in main.
- Every step must pass its own test checklist before moving forward.
- Use feature flags to enable isolated tests from main without deleting production code paths.

Suggested module layout:
- lib/hal_display: display init, drawing primitives, text and icon helpers.
- lib/hal_button: OneButton setup, event translation (short, long, hold).
- lib/hal_battery: ADC reading, voltage-to-capacity mapping, battery state calculation (if enabled).
- lib/hal_led: LED init, pattern generation (solid, blink, pulse), state-to-pattern mapping (if enabled).
- lib/core_clock: time source abstraction, NTP sync, manual set, time-valid state.
- lib/core_storage: LittleFS mount, projects file, log append, integrity checks.
- lib/core_tracker: project selection, running session state, switch-delay logic.
- lib/core_ui: screen models, blink timing, render orchestration, battery icon rendering.
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

4. Battery module (if enabled, optional)
- Implement: ADC init, voltage reading, capacity calculation, state persistence in logs.
- Test app: display live voltage/capacity, monitor over time as device runs.
- Exit criteria: voltage reads are stable and within expected range; capacity calculation is reasonable for Li-Po discharge curve.

5. Status LED module (if enabled, optional)
- Implement: GPIO PWM setup, pattern generation (solid/blink), state mapping.
- Test app: cycle through all states manually (via buttons) and verify LED patterns.
- Exit criteria: LED responds within 200 ms, patterns are visually distinct, loop remains responsive.

6. Time module (clock abstraction)
- Implement: time-valid flag, NTP sync path, manual set API, monotonic checks.
- Test app: run with Wi-Fi and without Wi-Fi, force manual fallback.
- Exit criteria: valid timestamp available in both online and offline workflows.

7. Storage module (LittleFS)
- Implement: mount, projects read/write, append-safe log writer, corruption fallback.
- Test app: create and reload projects, append 1000 log lines, reboot and verify.
- Exit criteria: persisted data remains intact across reboot and power cycle.

8. Tracker logic module
- Implement: selected project, active project, start/stop, 3-second pending switch.
- Test app: simulated button events and real button events.
- Exit criteria: transition table matches spec with deterministic outcomes.

9. UI module
- Implement: screens for idle, tracking, selecting, settings; 500 ms blink; status icons (WiFi, battery).
- Test app: feed fake model data to renderer (no storage dependency required).
- Exit criteria: all required indicators appear correctly; no blocking redraw loops.

10. USB guard module
- Implement: USB mode flag, storage write lock enforcement, on-screen USB indicator.
- Test app: mount/export path and verify write attempts are blocked in USB mode.
- Exit criteria: zero writes happen while host-controlled storage mode is active.

11. App controller integration
- Implement: state machine glue for all modules with explicit transitions.
- Test app: full user journey using only GPIO 0.
- Exit criteria: all transitions valid for IDLE, TRACKING, SELECTING, SETTINGS, USB_MODE.

12. System integration test pass
- Run full acceptance suite from section 10.
- Add regression scripts/checklists for future firmware updates.
- Exit criteria: every acceptance item is verified and repeatable.

## 13) Dependency Tree for Progress
Use this tree to decide what can be developed and tested in parallel.

- Foundation
	- Board bring-up
	- Build config and logging
- HAL Layer
	- Display HAL (depends on Foundation)
	- Button HAL (depends on Foundation)
	- Battery HAL (depends on Foundation, optional)
	- LED HAL (depends on Foundation, optional)
	- Storage HAL/LittleFS mount (depends on Foundation)
	- USB Guard HAL (depends on Foundation + Storage HAL)
- Core Services
	- Time Service (depends on Foundation, optional Wi-Fi)
	- Storage Service (depends on Storage HAL)
	- Tracker Service (depends on Button HAL + Time Service + Storage Service)
	- Battery Service (depends on Battery HAL, optional)
- UI Layer
	- Renderer (depends on Display HAL + Battery HAL if enabled)
	- UI Presenter/ViewModel (depends on Tracker Service + Time Service + Battery Service + USB Guard HAL)
- LED Control Layer
	- LED pattern controller (depends on LED HAL + Tracker Service + Battery Service)
- Application Layer
	- App State Machine (depends on Button HAL + UI Presenter + Tracker Service + USB Guard HAL + Battery Service if enabled)
	- Main Integration (depends on all above)

Practical sequencing rule:
- GUI integration starts only after Display HAL and Button HAL are validated.
- Persistent tracking starts only after Time Service and Storage Service pass standalone tests.
- USB export is integrated only after Storage Service is proven stable.
- Tracker integration starts only after Button HAL, Clock, and Storage all pass standalone tests.
- Battery and LED modules can be developed in parallel with Tracker (no interdependency).

## 14) Verification Matrix (Per Step)
Each step above must include these checks before merge:
- Unit-level behavior check (where feasible on host or via lightweight harness).
- On-device functional test.
- Reboot persistence check (if storage or state involved).
- Negative-path check (invalid input, missing Wi-Fi, malformed data).
- Pe5) Using ESP-Prog with Waveshare ESP32-S2-LCD-0.96

### 15 Using ESP-Prog with Waveshare ESP32-S2-LCD-0.96

### 13.1 Why use ESP-Prog here
- Stable UART flashing and serial logging during bring-up.
- Easier recovery if native USB firmware configuration becomes invalid.
- Opt5onal external debug workflow preparation.

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

### 15.3 PlatformIO profile for ESP-Prog
For ESP-Prog sessions, use a dedicated environment profile (recommended):
- Keep `upload_protocol = esptool`.
- Set `upload_port` to ESP-Prog COM port.
- Set `monitor_port` to the same COM port.
- Keep `monitor_speed = 115200`.

USB CDC note:
- Current project enables `-DARDUINO_USB_CDC_ON_BOOT=1` for native USB serial.
- When primarily using ESP-Prog UART monitor, disable that flag in the ESP-Prog profile to avoid split/confusing serial behavior.

### 15.4 Flash workflow
1. Connect wiring above and confirm COM port appears.
2. Build firmware.
3. Upload via ESP-Prog from PlatformIO.
4. If auto-reset does not enter bootloader:
- hold BOOT,
- tap RESET/EN,
- release BOOT after upload starts.
5. Open serial monitor and verify boot log and heartbeat.

### 15.5 Step-gate checks with ESP-Prog (matches Section 12)
- Step 1 (bring-up): verify clean boot logs every reset.
- Step 2 (display): watch for redraw timing and crash-free operation for 5 minutes.
- Step 3 (button): press test with event counters over UART logs.
- Step 4 (battery): if enabled, verify ADC reads are stable and capacity % is reasonable.
- Step 5 (LED): if enabled, verify LED patterns change correctly with state transitions.
- Step 6 (time): verify online NTP and offline/manual fallback paths in logs.
- Step 7 (storage): run write/reboot/read loops; verify no file corruption.
- Step 8 (tracker): simulate button presses and verify state transitions, 3-second pending switch.
- Step 10 (USB guard): while host owns storage mode, confirm blocked writes are reported in logs.

### 15.6 Common failure patterns and fixes
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