#include <Arduino.h>
#include <Wire.h>

#include "config/board_config.h"
#include "core_clock.h"
#include "core_storage.h"
#include "core_tracker.h"
#include "hal_button.h"
#include "hal_display.h"

#ifndef SHIFTLOG_BOOTSTRAP_STEP
#define SHIFTLOG_BOOTSTRAP_STEP 0
#endif

namespace {
// Pin definitions from board config
constexpr uint8_t PIN_LCD_RST = shiftlog::board::DISPLAY_RST;
constexpr uint8_t PIN_LCD_BL = shiftlog::board::DISPLAY_BL;
constexpr uint8_t PIN_LCD_DC = shiftlog::board::DISPLAY_DC;
constexpr uint8_t PIN_LCD_CS = shiftlog::board::DISPLAY_CS;
constexpr uint8_t PIN_LCD_MOSI = shiftlog::board::DISPLAY_MOSI;
constexpr uint8_t PIN_LCD_MISO = shiftlog::board::DISPLAY_MISO;
constexpr uint8_t PIN_LCD_SCK = shiftlog::board::DISPLAY_SCK;
constexpr uint8_t PIN_BOOT_BUTTON = shiftlog::board::BUTTON_PIN;

constexpr uint32_t HEARTBEAT_INTERVAL_MS = 1000;
constexpr uint32_t DISPLAY_UPDATE_INTERVAL_MS = 100;
constexpr uint32_t COLOR_CYCLE_INTERVAL_MS = 600;
constexpr uint32_t CLOCK_UPDATE_INTERVAL_MS = 1000;
constexpr time_t DEFAULT_MANUAL_TIME = 1735689600;  // 2025-01-01 00:00:00 UTC

#if SHIFTLOG_BOOTSTRAP_STEP == 1
constexpr uint32_t SERIAL_HEARTBEAT_INTERVAL_MS = 1000;
constexpr uint32_t SERIAL_BANNER_REPEAT_MS = 10000;

uint32_t bootstrapLastHeartbeatMs = 0;
uint32_t bootstrapLastBannerMs = 0;
uint32_t bootstrapLoopCounter = 0;
uint32_t bootstrapLastLoopSnapshot = 0;

void runSerialBootstrap(const uint32_t nowMs) {
  ++bootstrapLoopCounter;

  if (nowMs - bootstrapLastBannerMs >= SERIAL_BANNER_REPEAT_MS) {
    bootstrapLastBannerMs = nowMs;
    Serial.println("[BOOTSTRAP] stage=1 serial diagnostics active");
    Serial.println("[BOOTSTRAP] next step: set SHIFTLOG_BOOTSTRAP_STEP=2 for display HAL");
  }

  if (nowMs - bootstrapLastHeartbeatMs < SERIAL_HEARTBEAT_INTERVAL_MS) {
    return;
  }

  const uint32_t loopsPerSec = bootstrapLoopCounter - bootstrapLastLoopSnapshot;
  bootstrapLastLoopSnapshot = bootstrapLoopCounter;
  bootstrapLastHeartbeatMs = nowMs;

  const int rawButton = digitalRead(PIN_BOOT_BUTTON);
  Serial.printf(
      "[SERIAL] uptime_ms=%lu heap=%lu button_raw=%d loops_per_sec=%lu\n",
      static_cast<unsigned long>(nowMs),
      static_cast<unsigned long>(ESP.getFreeHeap()),
      rawButton,
      static_cast<unsigned long>(loopsPerSec));
}
#endif

#if defined(BOARD_M5STICKS3)
constexpr uint32_t DISPLAY_BOOTSTRAP_HEARTBEAT_MS = 1000;
constexpr uint32_t DISPLAY_BOOTSTRAP_COLOR_MS = 800;
constexpr uint32_t DISPLAY_BOOTSTRAP_BL_TOGGLE_MS = 5000;
constexpr uint32_t DISPLAY_BOOTSTRAP_BL_SETTLE_MS = 180;
constexpr uint8_t PM1_I2C_ADDR = 0x6E;

uint32_t displayBootstrapLastHeartbeatMs = 0;
uint32_t displayBootstrapLastColorMs = 0;
uint32_t displayBootstrapLastBlToggleMs = 0;
uint32_t displayBootstrapBacklightReadyAtMs = 0;
size_t displayBootstrapColorIndex = 0;
bool displayBootstrapBacklightHigh = true;

extern shiftlog::hal::HalDisplay display;

bool pm1ReadReg(const uint8_t reg, uint8_t& value) {
  Wire.beginTransmission(PM1_I2C_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }

  const uint8_t bytes = Wire.requestFrom(static_cast<int>(PM1_I2C_ADDR), 1);
  if (bytes != 1 || !Wire.available()) {
    return false;
  }

  value = Wire.read();
  return true;
}

bool pm1WriteReg(const uint8_t reg, const uint8_t value) {
  Wire.beginTransmission(PM1_I2C_ADDR);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool pm1WriteBit(const uint8_t reg, const uint8_t bitMask, const bool setBit) {
  uint8_t current = 0;
  if (!pm1ReadReg(reg, current)) {
    return false;
  }

  const uint8_t updated = setBit ? (current | bitMask) : (current & ~bitMask);
  if (updated == current) {
    return true;
  }

  return pm1WriteReg(reg, updated);
}

void setupM5StickS3Pm1() {
  Wire.begin(shiftlog::board::I2C_SDA, shiftlog::board::I2C_SCL, 100000);

  // M5Unified uses PM1 reg 0x06 bit3 to control StickS3 external 5V output.
  // This line explicitly enables the L3B/5V power path for display-related rails.
  const bool ext5vEnable = pm1WriteBit(0x06, static_cast<uint8_t>(1U << 3), true);

  // Configure PM1 G2 (L3B_EN path) as GPIO output and drive HIGH.
  const bool g2Func = pm1WriteBit(0x16, static_cast<uint8_t>(1U << 2), false);
  const bool g2Mode = pm1WriteBit(0x10, static_cast<uint8_t>(1U << 2), true);
  const bool g2PushPull = pm1WriteBit(0x13, static_cast<uint8_t>(1U << 2), false);
  const bool g2High = pm1WriteBit(0x11, static_cast<uint8_t>(1U << 2), true);

  // Configure PM1 G3 as GPIO output low (same PA control direction as M5Unified).
  const bool g3Func = pm1WriteBit(0x16, static_cast<uint8_t>(1U << 3), false);
  const bool g3Mode = pm1WriteBit(0x10, static_cast<uint8_t>(1U << 3), true);
  const bool g3PushPull = pm1WriteBit(0x13, static_cast<uint8_t>(1U << 3), false);
  const bool g3Low = pm1WriteBit(0x11, static_cast<uint8_t>(1U << 3), false);

    uint8_t reg06 = 0;
    uint8_t reg10 = 0;
    uint8_t reg11 = 0;
    const bool read06 = pm1ReadReg(0x06, reg06);
    const bool read10 = pm1ReadReg(0x10, reg10);
    const bool read11 = pm1ReadReg(0x11, reg11);

  Serial.printf(
      "[DISPLAY] PM1 setup ext5v=%d g2(func/mode/push/high)=%d/%d/%d/%d g3(func/mode/push/low)=%d/%d/%d/%d\n",
      ext5vEnable ? 1 : 0,
      g2Func ? 1 : 0,
      g2Mode ? 1 : 0,
      g2PushPull ? 1 : 0,
      g2High ? 1 : 0,
      g3Func ? 1 : 0,
      g3Mode ? 1 : 0,
      g3PushPull ? 1 : 0,
      g3Low ? 1 : 0);

    Serial.printf("[DISPLAY] PM1 readback r06=%s:0x%02X r10=%s:0x%02X r11=%s:0x%02X\n",
          read06 ? "ok" : "err",
          static_cast<unsigned>(reg06),
          read10 ? "ok" : "err",
          static_cast<unsigned>(reg10),
          read11 ? "ok" : "err",
          static_cast<unsigned>(reg11));
}

void runDisplayBootstrap(const uint32_t nowMs) {
  constexpr uint16_t kDisplayBootstrapColors[] = {
      ST77XX_RED,
      ST77XX_GREEN,
      ST77XX_BLUE,
      ST77XX_YELLOW,
  };

  if (nowMs - displayBootstrapLastHeartbeatMs >= DISPLAY_BOOTSTRAP_HEARTBEAT_MS) {
    displayBootstrapLastHeartbeatMs = nowMs;
    Serial.printf("[DISPLAY] uptime_ms=%lu heap=%lu color_index=%u bl=%s\n",
                  static_cast<unsigned long>(nowMs),
                  static_cast<unsigned long>(ESP.getFreeHeap()),
                  static_cast<unsigned>(displayBootstrapColorIndex),
                  displayBootstrapBacklightHigh ? "HIGH" : "LOW");
  }

  if (nowMs - displayBootstrapLastBlToggleMs >= DISPLAY_BOOTSTRAP_BL_TOGGLE_MS) {
    displayBootstrapLastBlToggleMs = nowMs;
    displayBootstrapBacklightHigh = !displayBootstrapBacklightHigh;
    digitalWrite(PIN_LCD_BL, displayBootstrapBacklightHigh ? HIGH : LOW);
    if (displayBootstrapBacklightHigh) {
      // Re-sync the visible color sequence on backlight-on.
      displayBootstrapColorIndex = 0;
      displayBootstrapBacklightReadyAtMs = nowMs + DISPLAY_BOOTSTRAP_BL_SETTLE_MS;
      displayBootstrapLastColorMs = nowMs;
    }
    Serial.printf("[DISPLAY] backlight toggled -> %s\n",
                  displayBootstrapBacklightHigh ? "HIGH" : "LOW");
  }

  if (!displayBootstrapBacklightHigh) {
    return;
  }

  if (nowMs < displayBootstrapBacklightReadyAtMs) {
    return;
  }

  if (nowMs - displayBootstrapLastColorMs < DISPLAY_BOOTSTRAP_COLOR_MS) {
    return;
  }

  displayBootstrapLastColorMs = nowMs;
  display.clear(kDisplayBootstrapColors[displayBootstrapColorIndex]);
  displayBootstrapColorIndex =
    (displayBootstrapColorIndex + 1) %
    (sizeof(kDisplayBootstrapColors) / sizeof(kDisplayBootstrapColors[0]));
}
#endif

#if SHIFTLOG_BOOTSTRAP_STEP == 3
constexpr uint32_t BUTTON_DISPLAY_HEARTBEAT_MS = 1000;
constexpr uint32_t BUTTON_DISPLAY_COLOR_MS = 1000;
constexpr uint16_t kButtonDisplayColors[] = {
  ST77XX_RED,
  ST77XX_GREEN,
  ST77XX_BLUE,
  ST77XX_YELLOW,
};

uint32_t buttonDisplayLastHeartbeatMs = 0;
uint32_t buttonDisplayLastColorMs = 0;
size_t buttonDisplayColorIndex = 0;

extern shiftlog::hal::HalDisplay display;
extern shiftlog::hal::HalButton button;
extern uint32_t shortPressCount;
extern uint32_t longPressCount;

void handleButtonEventsBootstrap() {
  while (true) {
    const shiftlog::hal::ButtonEvent event = button.popEvent();
    if (event == shiftlog::hal::ButtonEvent::None) {
      return;
    }

    switch (event) {
      case shiftlog::hal::ButtonEvent::ShortPress:
        ++shortPressCount;
        Serial.printf("[BOOTSTRAP3] button short press (%lu)\n",
                      static_cast<unsigned long>(shortPressCount));
        break;
      case shiftlog::hal::ButtonEvent::LongPress:
        ++longPressCount;
        Serial.printf("[BOOTSTRAP3] button long press (%lu)\n",
                      static_cast<unsigned long>(longPressCount));
        break;
      case shiftlog::hal::ButtonEvent::None:
        break;
    }
  }
}

void runButtonDisplayBootstrap(const uint32_t nowMs) {
  if (nowMs - buttonDisplayLastHeartbeatMs >= BUTTON_DISPLAY_HEARTBEAT_MS) {
    buttonDisplayLastHeartbeatMs = nowMs;
    Serial.printf("[BOOTSTRAP3] uptime_ms=%lu heap=%lu short=%lu long=%lu\n",
                  static_cast<unsigned long>(nowMs),
                  static_cast<unsigned long>(ESP.getFreeHeap()),
                  static_cast<unsigned long>(shortPressCount),
                  static_cast<unsigned long>(longPressCount));
  }

  display.showButtonCounters(shortPressCount, longPressCount);

  if (nowMs - buttonDisplayLastColorMs < BUTTON_DISPLAY_COLOR_MS) {
    return;
  }

  buttonDisplayLastColorMs = nowMs;
  display.showColor(kButtonDisplayColors[buttonDisplayColorIndex]);
  buttonDisplayColorIndex =
      (buttonDisplayColorIndex + 1) %
      (sizeof(kButtonDisplayColors) / sizeof(kButtonDisplayColors[0]));
}
#endif

const shiftlog::hal::DisplayPins kDisplayPins{
    .rst = PIN_LCD_RST,
    .bl = PIN_LCD_BL,
    .dc = PIN_LCD_DC,
    .cs = PIN_LCD_CS,
    .mosi = PIN_LCD_MOSI,
    .miso = PIN_LCD_MISO,
    .sck = PIN_LCD_SCK,
};

shiftlog::hal::HalDisplay display(kDisplayPins);
shiftlog::hal::HalButton button;
shiftlog::core::CoreClock clockService;
shiftlog::core::CoreStorage storage;
shiftlog::core::CoreTracker tracker;

uint32_t shortPressCount = 0;
uint32_t longPressCount = 0;
uint32_t lastHeartbeatMs = 0;
uint32_t lastDisplayUpdateMs = 0;
uint32_t lastColorChangeMs = 0;
uint32_t lastClockUpdateMs = 0;
uint32_t lastStorageTestMs = 0;
uint32_t lastTrackerDisplayMs = 0;
size_t colorIndex = 0;
bool storageTestDone = false;
bool trackerTestDone = false;
uint8_t lastRenderedSelectedProjectId = 0xFF;
uint8_t lastRenderedActiveProjectId = 0xFF;
bool lastRenderedTracking = false;
bool lastRenderedTimeValid = false;
bool mainStatusDirty = true;

constexpr uint16_t kColors[] = {
    ST77XX_RED,
    ST77XX_GREEN,
    ST77XX_BLUE,
    ST77XX_YELLOW,
};

void handleButtonEvents() {
  while (true) {
    const shiftlog::hal::ButtonEvent event = button.popEvent();
    if (event == shiftlog::hal::ButtonEvent::None) {
      return;
    }

    switch (event) {
      case shiftlog::hal::ButtonEvent::ShortPress:
        ++shortPressCount;
        Serial.printf("button short press (%lu)\n",
                      static_cast<unsigned long>(shortPressCount));
        tracker.onShortPress();
        mainStatusDirty = true;
        break;
      case shiftlog::hal::ButtonEvent::LongPress:
        ++longPressCount;
        Serial.printf("button long press (%lu)\n",
                      static_cast<unsigned long>(longPressCount));
        tracker.onLongPress();
        mainStatusDirty = true;
        if (!clockService.isTimeValid()) {
          const bool manualTimeApplied =
              clockService.setManualUnixTime(DEFAULT_MANUAL_TIME);
          Serial.printf("manual time set: %s\n",
                        manualTimeApplied ? "ok" : "failed");
          mainStatusDirty = true;
        }
        break;
      case shiftlog::hal::ButtonEvent::None:
        break;
    }
  }
}

void runHeartbeat(const uint32_t nowMs) {
  if (nowMs - lastHeartbeatMs < HEARTBEAT_INTERVAL_MS) {
    return;
  }

  lastHeartbeatMs = nowMs;
  Serial.printf("heartbeat short=%lu long=%lu tracking=%s timeValid=%s\n",
                static_cast<unsigned long>(shortPressCount),
                static_cast<unsigned long>(longPressCount),
                tracker.isTracking() ? "yes" : "no",
                clockService.isTimeValid() ? "yes" : "no");
}

void runClockUpdate(const uint32_t nowMs) {
  if (nowMs - lastClockUpdateMs < CLOCK_UPDATE_INTERVAL_MS) {
    return;
  }

  lastClockUpdateMs = nowMs;
  clockService.update();
  mainStatusDirty = true;
}

void runDisplayUpdate(const uint32_t nowMs) {
  if (nowMs - lastDisplayUpdateMs >= DISPLAY_UPDATE_INTERVAL_MS) {
    lastDisplayUpdateMs = nowMs;

    const uint8_t selectedProjectId = tracker.getSelectedProjectId();
    const uint8_t activeProjectId = tracker.getActiveProjectId();
    const bool tracking = tracker.isTracking();
    const bool timeValid = clockService.isTimeValid();

    if (mainStatusDirty ||
        selectedProjectId != lastRenderedSelectedProjectId ||
        activeProjectId != lastRenderedActiveProjectId ||
        tracking != lastRenderedTracking ||
        timeValid != lastRenderedTimeValid) {
      display.showMainStatus(selectedProjectId, activeProjectId, tracking,
                             timeValid);
      lastRenderedSelectedProjectId = selectedProjectId;
      lastRenderedActiveProjectId = activeProjectId;
      lastRenderedTracking = tracking;
      lastRenderedTimeValid = timeValid;
      mainStatusDirty = false;
    }

    display.showButtonCounters(shortPressCount, longPressCount);
  }

  if (nowMs - lastColorChangeMs < COLOR_CYCLE_INTERVAL_MS) {
    return;
  }

  lastColorChangeMs = nowMs;
  display.showColor(kColors[colorIndex]);
  colorIndex = (colorIndex + 1) % (sizeof(kColors) / sizeof(kColors[0]));
}

void runStorageTest(const uint32_t nowMs) {
  if (storageTestDone) {
    return;
  }

  if (nowMs < 3000) {
    return;  // wait for boot stabilization
  }

  storageTestDone = true;

  Serial.println("\n--- Phase 2 Storage Tests ---");

  // Test: add project
  if (storage.addProject("1234.0")) {
    Serial.println("✓ add project 1234.0");
  } else {
    Serial.printf("✗ add project failed: %s\n", storage.getLastError());
  }

  // Test: add another project
  if (storage.addProject("5678.1")) {
    Serial.println("✓ add project 5678.1");
  } else {
    Serial.printf("✗ add project failed: %s\n", storage.getLastError());
  }

  // Test: append log entry
  shiftlog::core::LogEntry logEntry{
      .timestamp_start = 1700000000,
      .timestamp_end = 1700000300,
      .duration_seconds = 300,
      .source_time_mode = 1,
  };
  snprintf(logEntry.project_code, sizeof(logEntry.project_code), "%s",
           "1234.0");

  if (storage.appendLogEntry(logEntry)) {
    Serial.println("✓ append log entry");
  } else {
    Serial.printf("✗ append log failed: %s\n", storage.getLastError());
  }

  // Test: validate log integrity
  if (storage.validateLogIntegrity()) {
    Serial.println("✓ validate log integrity");
  } else {
    Serial.printf("✗ validate log failed: %s\n", storage.getLastError());
  }

  Serial.println("--- Storage tests complete ---\n");
}

void runTrackerTest(const uint32_t nowMs) {
  if (trackerTestDone) {
    return;
  }

  if (nowMs < 6000) {
    return;  // wait after storage test
  }

  trackerTestDone = true;

  Serial.println("\n--- Phase 3 Tracker Tests ---");

  // Initial state
  Serial.println("state: initial");
  Serial.printf("  selected=%u active=%u tracking=%s\n",
                static_cast<unsigned>(tracker.getSelectedProjectId()),
                static_cast<unsigned>(tracker.getActiveProjectId()),
                tracker.isTracking() ? "yes" : "no");

  // Simulate: select first project
  Serial.println("action: short press (select first project)");
  tracker.onShortPress();
  Serial.printf("  selected=%u\n",
                static_cast<unsigned>(tracker.getSelectedProjectId()));

  // Start tracking
  Serial.println("action: long press (start tracking)");
  tracker.onLongPress();
  Serial.printf("  tracking=%s active=%u\n", tracker.isTracking() ? "yes" : "no",
                static_cast<unsigned>(tracker.getActiveProjectId()));

  // Request project switch
  Serial.println("action: short press (request project switch)");
  tracker.onShortPress();
  Serial.printf("  pending_switch=%s remaining=%lu ms\n",
                tracker.hasPendingSwitch() ? "yes" : "no",
                static_cast<unsigned long>(tracker.getPendingSwitchRemainingMs()));

  // Wait for switch to execute
  Serial.println("action: advance time and call update (wait for switch)");
  for (int i = 0; i < 4; i++) {
    delay(1000);
    tracker.update(millis());
    if (!tracker.hasPendingSwitch()) {
      Serial.printf("  switch executed at iteration %d\n", i);
      break;
    }
  }

  // Stop tracking
  Serial.println("action: long press (stop tracking)");
  tracker.onLongPress();
  Serial.printf("  tracking=%s\n", tracker.isTracking() ? "yes" : "no");

  Serial.println("--- Tracker tests complete ---\n");
}
}  // namespace

void setup() {
  Serial.begin(shiftlog::board::SERIAL_BAUD);
  Serial.setDebugOutput(true);

#if SHIFTLOG_BOOTSTRAP_STEP == 1
  pinMode(PIN_BOOT_BUTTON, INPUT);

  Serial.println();
  Serial.println("=== ShiftLog Bootstrap ===");
  Serial.println("board: M5StickS3");
  Serial.printf("serial_baud=%lu\n",
                static_cast<unsigned long>(shiftlog::board::SERIAL_BAUD));
  Serial.printf("boot_button_pin=%u\n", static_cast<unsigned>(PIN_BOOT_BUTTON));
  Serial.println("stage=1 (serial only)");
  return;
#endif

#if SHIFTLOG_BOOTSTRAP_STEP == 2
  Serial.println();
  Serial.println("=== ShiftLog Bootstrap ===");
  Serial.println("board: M5StickS3");
  Serial.printf("serial_baud=%lu\n",
                static_cast<unsigned long>(shiftlog::board::SERIAL_BAUD));
  Serial.println("stage=2 (display HAL)");

  setupM5StickS3Pm1();

  // M5StickS3 routes panel power/backlight through PM1 control lines.
  pinMode(shiftlog::board::PM1_L3B_EN, OUTPUT);
  digitalWrite(shiftlog::board::PM1_L3B_EN, HIGH);
  Serial.printf("[DISPLAY] PM1_L3B_EN pin=%u -> HIGH\n",
                static_cast<unsigned>(shiftlog::board::PM1_L3B_EN));

  pinMode(PIN_LCD_BL, OUTPUT);
  digitalWrite(PIN_LCD_BL, HIGH);
  displayBootstrapBacklightHigh = true;
  Serial.printf("[DISPLAY] BL pin=%u -> HIGH\n", static_cast<unsigned>(PIN_LCD_BL));

  display.begin();
  display.showBootScreen("Stage 2 Display");
  Serial.println("[DISPLAY] initialized");
  return;
#endif

#if SHIFTLOG_BOOTSTRAP_STEP == 3
  Serial.println();
  Serial.println("=== ShiftLog Bootstrap ===");
  Serial.println("board: M5StickS3");
  Serial.printf("serial_baud=%lu\n",
                static_cast<unsigned long>(shiftlog::board::SERIAL_BAUD));
  Serial.println("stage=3 (button + display, stable BL HIGH)");

  setupM5StickS3Pm1();

  pinMode(shiftlog::board::PM1_L3B_EN, OUTPUT);
  digitalWrite(shiftlog::board::PM1_L3B_EN, HIGH);
  Serial.printf("[BOOTSTRAP3] PM1_L3B_EN pin=%u -> HIGH\n",
                static_cast<unsigned>(shiftlog::board::PM1_L3B_EN));

  pinMode(PIN_LCD_BL, OUTPUT);
  digitalWrite(PIN_LCD_BL, HIGH);
  Serial.printf("[BOOTSTRAP3] BL pin=%u -> HIGH\n",
                static_cast<unsigned>(PIN_LCD_BL));

  display.begin();
  display.showBootScreen("Stage 3 Button+Display");
  display.showButtonCounters(shortPressCount, longPressCount);

  button.begin(PIN_BOOT_BUTTON, shiftlog::board::BUTTON_ACTIVE_LOW,
               shiftlog::board::BUTTON_USE_PULLUP);
  Serial.println("[BOOTSTRAP3] initialized");
  return;
#endif

  Serial.printf("=== ShiftLog Boot [%s] ===\n", shiftlog::board::DISPLAY_TYPE);

#if defined(BOARD_M5STICKS3)
  setupM5StickS3Pm1();
  pinMode(shiftlog::board::PM1_L3B_EN, OUTPUT);
  digitalWrite(shiftlog::board::PM1_L3B_EN, HIGH);
  pinMode(PIN_LCD_BL, OUTPUT);
  digitalWrite(PIN_LCD_BL, HIGH);
  Serial.println("[M5STICKS3] PM1 display power path initialized");
#endif

  display.begin();
  display.showBootScreen("ESP32-ShiftLog");

  button.begin(PIN_BOOT_BUTTON, shiftlog::board::BUTTON_ACTIVE_LOW,
               shiftlog::board::BUTTON_USE_PULLUP);
  clockService.begin();
  storage.begin();
  tracker.begin(2);  // 2 projects for test harness
  display.showMainStatus(tracker.getSelectedProjectId(), tracker.getActiveProjectId(),
                         tracker.isTracking(), clockService.isTimeValid());
  mainStatusDirty = true;

  Serial.println("phase 1+2+3 harness ready");
}

void loop() {
  const uint32_t nowMs = millis();

#if SHIFTLOG_BOOTSTRAP_STEP == 1
  runSerialBootstrap(nowMs);
  return;
#endif

#if SHIFTLOG_BOOTSTRAP_STEP == 2
  runDisplayBootstrap(nowMs);
  return;
#endif

#if SHIFTLOG_BOOTSTRAP_STEP == 3
  button.tick();
  handleButtonEventsBootstrap();
  runButtonDisplayBootstrap(nowMs);
  return;
#endif

  button.tick();
  handleButtonEvents();
  runClockUpdate(nowMs);
  runTrackerTest(nowMs);
  tracker.update(nowMs);
  runHeartbeat(nowMs);
  runDisplayUpdate(nowMs);
  runStorageTest(nowMs);
}
