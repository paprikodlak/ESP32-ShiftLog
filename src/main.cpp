/*
 * ESP32-ShiftLog — Time Tracker Firmware
 * Hardware: Waveshare ESP32-S2-LCD-0.96 (ST7735S 160×80 px)
 *
 * Pin mapping:
 *   LCD_CS  = 7   LCD_DC = 9   LCD_RST = 8   LCD_BL = 15
 *   BOOT button (input) = GPIO 0
 *
 * Features:
 *   - WiFiManager (Config Portal fallback)
 *   - NTP time sync + Manual Time Set
 *   - State machine: IDLE → SELECTING → TRACKING → SETTINGS → MANUAL_TIME → USB_MODE
 *   - OneButton short/long press on BOOT
 *   - 500 ms blink for pending project switch
 *   - 3-second confirmation delay on project select
 *   - LittleFS for project list + session logs (CSV per project)
 *   - Adafruit TinyUSB MSC — exposes LittleFS partition as USB drive
 *     USB Mode flag: LittleFS is unmounted while PC has the drive mounted,
 *     preventing flash corruption.
 *   - WiFi / Play-Pause icons on status bar
 */

// ── Must come before Arduino.h on ESP32-S2 so TinyUSB initialises correctly ──
#include <Adafruit_TinyUSB.h>

#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <OneButton.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <time.h>
#include <esp_partition.h>

// ── Constants ────────────────────────────────────────────────────────────────
static constexpr uint8_t  BOOT_PIN          = 0;
static constexpr uint8_t  BL_PIN            = 15;
static constexpr uint16_t DISPLAY_W         = 160;
static constexpr uint16_t DISPLAY_H         = 80;
static constexpr uint32_t BLINK_INTERVAL_MS = 500;
static constexpr uint32_t CONFIRM_DELAY_MS  = 3000;
static constexpr uint16_t STATUS_BAR_H      = 16;
static constexpr uint32_t MSC_SECTOR_SIZE   = 512;

static const char* NTP_SERVER    = "pool.ntp.org";
// POSIX TZ string — "UTC0" = UTC.  Examples:
//   "CET-1CEST,M3.5.0,M10.5.0/3"  Central European Time (CET/CEST)
//   "EST5EDT,M3.2.0,M11.1.0"       US Eastern Time
//   "PST8PDT,M3.2.0,M11.1.0"       US Pacific Time
// Full list: https://github.com/nayarsystems/posix_tz_db
static const char* TZ_INFO       = "UTC0";
static const char* PROJECTS_FILE = "/projects.json";
static const char* LOGS_DIR      = "/logs";

// ── UI Colours ───────────────────────────────────────────────────────────────
static constexpr uint16_t COL_BG       = TFT_BLACK;
static constexpr uint16_t COL_STATUS   = TFT_DARKGREY;
static constexpr uint16_t COL_TEXT     = TFT_WHITE;
static constexpr uint16_t COL_ACCENT   = TFT_CYAN;
static constexpr uint16_t COL_WARN     = TFT_YELLOW;
static constexpr uint16_t COL_PLAY     = TFT_GREEN;
static constexpr uint16_t COL_PAUSE    = TFT_RED;
static constexpr uint16_t COL_WIFI_OK  = TFT_GREEN;
static constexpr uint16_t COL_WIFI_NO  = TFT_RED;
static constexpr uint16_t COL_SETTINGS = TFT_MAGENTA;

// ── State Machine ────────────────────────────────────────────────────────────
enum class State : uint8_t {
    IDLE,        // No project running, none selected
    SELECTING,   // User is cycling through projects
    TRACKING,    // A project timer is active
    SETTINGS,    // Settings sub-menu (add project)
    MANUAL_TIME, // Manual time-set sub-menu
    USB_MODE     // USB mass-storage connected — flash locked
};

// ── Project ──────────────────────────────────────────────────────────────────
static constexpr uint8_t MAX_PROJECTS = 16;

struct Project {
    char     id[12];        // "XXXX.X\0" + spare
    uint32_t totalSeconds;
};

// ── Global Objects ───────────────────────────────────────────────────────────
TFT_eSPI  tft;
OneButton btn(BOOT_PIN, /*active_low=*/true, /*pullup=*/true);

// ── Project list ─────────────────────────────────────────────────────────────
static Project  projects[MAX_PROJECTS];
static uint8_t  numProjects  = 0;
static int8_t   activeIdx    = -1;   // currently running project index
static int8_t   selectedIdx  = 0;    // cursor while in SELECTING
static uint32_t sessionStart = 0;    // millis() when current session started

// ── Timing ───────────────────────────────────────────────────────────────────
static uint32_t blinkTimer    = 0;
static bool     blinkState    = false;
static uint32_t confirmTimer  = 0;
static bool     confirmPending = false;

// ── Application state ────────────────────────────────────────────────────────
static State state         = State::IDLE;
static bool  wifiConnected = false;
static bool  ntpSynced     = false;
static bool  usbMounted    = false;  // true while host OS has the drive mounted

// ── Settings sub-menu ────────────────────────────────────────────────────────
// Project code entry via XXXX.X mask
static char    newProjectBuf[8]       = "0000.0";
static uint8_t editPos                = 0;   // which digit is selected (0-4)
static const uint8_t EDIT_POSITIONS[] = {0, 1, 2, 3, 5};
static const uint8_t NUM_EDIT_POS     = 5;

// ── Manual time-set ──────────────────────────────────────────────────────────
static struct tm manualTm{};
static uint8_t   mtField = 0; // 0=hour 1=min 2=mday 3=mon 4=year

// ── USB MSC (TinyUSB) ────────────────────────────────────────────────────────
Adafruit_USBD_MSC usb_msc;
static const esp_partition_t* mscPartition = nullptr;
static uint32_t               mscSectors   = 0;

// ── Forward declarations ─────────────────────────────────────────────────────
static void saveProjects();
static void loadProjects();
static void drawUI();
static void drawStatusBar();
static void drawMainContent();
static void drawSettings();
static void drawManualTime();
static void startTracking(int8_t idx);
static void stopTracking(bool doSave);
static void logSession(int8_t idx, uint32_t seconds);
static void enterSettings();
static void addNewProject();

// ────────────────────────────────────────────────────────────────────────────
// USB MSC callbacks
// ────────────────────────────────────────────────────────────────────────────

// Called by host to read sectors — LittleFS is unmounted at this point
static int32_t msc_read_cb(uint32_t lba, void* buffer, uint32_t bufsize)
{
    if (!mscPartition) return -1;
    esp_err_t err = esp_partition_read(mscPartition,
                                       (size_t)lba * MSC_SECTOR_SIZE,
                                       buffer, bufsize);
    return (err == ESP_OK) ? (int32_t)bufsize : -1;
}

// Called by host to write sectors.
// We acknowledge the write to avoid host-side errors but do NOT persist to
// flash — the "USB Mode" flag in saveProjects()/logSession() is the real guard
// ensuring the ESP32 never writes while the host has the drive mounted.
// This prevents any risk of simultaneous-access corruption.
static int32_t msc_write_cb(uint32_t /*lba*/, uint8_t* /*buffer*/,
                             uint32_t bufsize)
{
    return (int32_t)bufsize;  // acknowledge without writing
}

static void msc_flush_cb() { /* no cache to flush */ }

// Called when host mounts (start=true) or ejects (start=false)
static void msc_startstop_cb(uint8_t /*power_condition*/,
                              bool start, bool /*load_eject*/)
{
    if (start) {
        // Host mounting — unmount LittleFS to prevent corruption
        if (activeIdx >= 0) stopTracking(/*doSave=*/false);
        LittleFS.end();
        usbMounted = true;
        state      = State::USB_MODE;
    } else {
        // Host ejected — remount LittleFS and reload data
        LittleFS.begin(/*formatIfFailed=*/true);
        loadProjects();
        usbMounted = false;
        state      = State::IDLE;
    }
    drawUI();
}

// ────────────────────────────────────────────────────────────────────────────
// NTP / time helpers
// ────────────────────────────────────────────────────────────────────────────
static void syncNTP()
{
    configTzTime(TZ_INFO, NTP_SERVER);
    struct tm ti{};
    if (getLocalTime(&ti, 8000)) {
        ntpSynced = true;
    }
}

static void getTimeStr(char* buf, size_t len)
{
    struct tm ti{};
    if (getLocalTime(&ti, 50)) {
        snprintf(buf, len, "%02d:%02d:%02d",
                 ti.tm_hour, ti.tm_min, ti.tm_sec);
    } else {
        snprintf(buf, len, "--:--:--");
    }
}

static void getDateStr(char* buf, size_t len)
{
    struct tm ti{};
    if (getLocalTime(&ti, 50)) {
        snprintf(buf, len, "%04d-%02d-%02d",
                 ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday);
    } else {
        snprintf(buf, len, "----/--/--");
    }
}

// ────────────────────────────────────────────────────────────────────────────
// LittleFS helpers
// ────────────────────────────────────────────────────────────────────────────
static void loadProjects()
{
    numProjects = 0;
    File f = LittleFS.open(PROJECTS_FILE, "r");
    if (!f) return;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) return;

    for (JsonObject obj : doc.as<JsonArray>()) {
        if (numProjects >= MAX_PROJECTS) break;
        const char* id = obj["id"] | "";
        strlcpy(projects[numProjects].id, id, sizeof(projects[0].id));
        projects[numProjects].totalSeconds = obj["totalSeconds"] | 0u;
        numProjects++;
    }
}

static void saveProjects()
{
    if (usbMounted) return;  // USB Mode guard — never write while host has FS

    File f = LittleFS.open(PROJECTS_FILE, "w");
    if (!f) return;

    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (uint8_t i = 0; i < numProjects; i++) {
        JsonObject obj    = arr.add<JsonObject>();
        obj["id"]         = projects[i].id;
        obj["totalSeconds"] = projects[i].totalSeconds;
    }
    serializeJson(doc, f);
    f.close();
}

static void logSession(int8_t idx, uint32_t seconds)
{
    if (usbMounted) return;  // USB Mode guard
    if (idx < 0 || idx >= (int8_t)numProjects) return;

    if (!LittleFS.exists(LOGS_DIR)) LittleFS.mkdir(LOGS_DIR);

    char path[40];
    snprintf(path, sizeof(path), "%s/%s.csv", LOGS_DIR, projects[idx].id);

    File f = LittleFS.open(path, "a");
    if (!f) return;

    char ts[24] = "unknown";
    struct tm ti{};
    if (getLocalTime(&ti, 50)) {
        snprintf(ts, sizeof(ts), "%04d-%02d-%02dT%02d:%02d:%02d",
                 ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday,
                 ti.tm_hour, ti.tm_min, ti.tm_sec);
    }
    f.printf("%s,%lu\n", ts, (unsigned long)seconds);
    f.close();
}

// ────────────────────────────────────────────────────────────────────────────
// Tracking helpers
// ────────────────────────────────────────────────────────────────────────────
static void startTracking(int8_t idx)
{
    if (idx < 0 || idx >= (int8_t)numProjects) return;
    activeIdx    = idx;
    sessionStart = millis();
    state        = State::TRACKING;
}

static void stopTracking(bool doSave)
{
    if (activeIdx < 0) return;

    uint32_t elapsed = (millis() - sessionStart) / 1000;
    projects[activeIdx].totalSeconds += elapsed;
    logSession(activeIdx, elapsed);
    if (doSave) saveProjects();

    activeIdx    = -1;
    sessionStart = 0;
}

// ────────────────────────────────────────────────────────────────────────────
// Icon helpers
// ────────────────────────────────────────────────────────────────────────────

// WiFi icon — 9×8 px centred at (x+4, y+4)
static void drawWifiIcon(uint16_t x, uint16_t y, bool connected)
{
    uint16_t col = connected ? COL_WIFI_OK : COL_WIFI_NO;
    tft.fillCircle(x + 4, y + 7, 1, col);                      // centre dot
    tft.drawArc(x + 4, y + 7, 4, 3, 210, 330, col, COL_BG);   // inner arc
    tft.drawArc(x + 4, y + 7, 7, 6, 210, 330, col, COL_BG);   // outer arc
}

// Play ▶ (tracking) or Pause ‖ (idle) icon — 8×8 px at (x, y)
static void drawPlayPauseIcon(uint16_t x, uint16_t y, bool playing)
{
    if (playing) {
        for (int i = 0; i < 8; i++) {
            tft.drawFastVLine(x + i, y + i / 2, 8 - i, COL_PLAY);
        }
    } else {
        tft.fillRect(x,     y, 3, 8, COL_PAUSE);
        tft.fillRect(x + 5, y, 3, 8, COL_PAUSE);
    }
}

// ────────────────────────────────────────────────────────────────────────────
// Status bar (top 16 px)
// ────────────────────────────────────────────────────────────────────────────
static void drawStatusBar()
{
    tft.fillRect(0, 0, DISPLAY_W, STATUS_BAR_H, COL_STATUS);

    drawWifiIcon(2, 3, wifiConnected);
    drawPlayPauseIcon(14, 3, activeIdx >= 0);

    char tbuf[12];
    getTimeStr(tbuf, sizeof(tbuf));
    tft.setTextColor(COL_TEXT, COL_STATUS);
    tft.setTextSize(1);
    tft.setCursor(50, 4);
    tft.print(tbuf);

    if (usbMounted) {
        tft.setTextColor(COL_WARN, COL_STATUS);
        tft.setCursor(138, 4);
        tft.print("USB");
    }
}

// ────────────────────────────────────────────────────────────────────────────
// Utility: format seconds → "HH:MM:SS"
// ────────────────────────────────────────────────────────────────────────────
static void formatDuration(uint32_t secs, char* buf, size_t len)
{
    uint32_t h = secs / 3600;
    uint32_t m = (secs % 3600) / 60;
    uint32_t s = secs % 60;
    snprintf(buf, len, "%02lu:%02lu:%02lu",
             (unsigned long)h, (unsigned long)m, (unsigned long)s);
}

// ────────────────────────────────────────────────────────────────────────────
// Content area drawing (below status bar)
// ────────────────────────────────────────────────────────────────────────────
static void drawSettings()
{
    uint16_t y = STATUS_BAR_H + 4;
    tft.setTextColor(COL_SETTINGS, COL_BG);
    tft.setCursor(4, y);
    tft.print("Settings: Add Project");

    tft.setTextColor(COL_TEXT, COL_BG);
    tft.setCursor(4, y + 12);
    tft.print("Code (XXXX.X):");

    // Draw each character; blink active digit
    tft.setTextSize(2);
    uint16_t cx = 4;
    uint16_t cy = y + 24;
    uint8_t  bufLen = (uint8_t)strlen(newProjectBuf);
    for (uint8_t i = 0; i < bufLen; i++) {
        // Is this char the currently-edited position?
        bool isActive = (editPos < NUM_EDIT_POS) && (EDIT_POSITIONS[editPos] == i);
        if (isActive && blinkState) {
            tft.setTextColor(COL_WARN, COL_BG);
        } else {
            tft.setTextColor(COL_ACCENT, COL_BG);
        }
        tft.setCursor(cx, cy);
        tft.print(newProjectBuf[i]);
        cx += 12;
    }
    tft.setTextSize(1);

    tft.setTextColor(COL_TEXT, COL_BG);
    tft.setCursor(4, y + 46);
    tft.print("Short: inc digit");
    tft.setCursor(4, y + 56);
    tft.print("Long:  next / confirm");
}

static void drawManualTime()
{
    uint16_t y = STATUS_BAR_H + 4;
    tft.setTextColor(COL_SETTINGS, COL_BG);
    tft.setCursor(4, y);
    tft.print("Set Time");

    static const char* labels[] = {"Hour", "Min ", "Day ", "Mon ", "Year"};
    int fields[5] = {
        manualTm.tm_hour,
        manualTm.tm_min,
        manualTm.tm_mday,
        manualTm.tm_mon + 1,       // 0-based → 1-based
        manualTm.tm_year + 1900    // years since 1900
    };

    for (uint8_t i = 0; i < 5; i++) {
        uint16_t rowY = y + 14 + (uint16_t)(i * 10);
        tft.setCursor(4, rowY);
        tft.setTextColor((i == mtField) ? COL_WARN : COL_TEXT, COL_BG);
        tft.printf("%-5s %d", labels[i], fields[i]);
    }
}

static void drawMainContent()
{
    tft.fillRect(0, STATUS_BAR_H, DISPLAY_W,
                 (uint16_t)(DISPLAY_H - STATUS_BAR_H), COL_BG);
    tft.setTextSize(1);

    uint16_t y = STATUS_BAR_H + 4;

    switch (state) {

    case State::IDLE: {
        tft.setTextColor(COL_TEXT, COL_BG);
        tft.setCursor(4, y);
        tft.print("ShiftLog Ready");
        tft.setCursor(4, y + 14);
        tft.print("Short: select project");
        tft.setCursor(4, y + 26);
        tft.print("Long:  settings");

        char dbuf[14];
        getDateStr(dbuf, sizeof(dbuf));
        tft.setTextColor(COL_ACCENT, COL_BG);
        tft.setCursor(4, y + 44);
        tft.print(dbuf);
        break;
    }

    case State::SELECTING: {
        if (numProjects == 0) {
            tft.setTextColor(COL_WARN, COL_BG);
            tft.setCursor(4, y);
            tft.print("No projects!");
            tft.setTextColor(COL_TEXT, COL_BG);
            tft.setCursor(4, y + 14);
            tft.print("Long: go to Settings");
            break;
        }

        bool pending = (activeIdx >= 0) && (selectedIdx != activeIdx);
        // Blink the project name if a different project is already running
        uint16_t nameCol = pending && !blinkState ? COL_BG : COL_ACCENT;

        tft.setTextColor(COL_TEXT, COL_BG);
        tft.setCursor(4, y);
        tft.print("Select project:");

        tft.setTextColor(nameCol, COL_BG);
        tft.setTextSize(2);
        tft.setCursor(4, y + 12);
        tft.print(projects[selectedIdx].id);
        tft.setTextSize(1);

        if (activeIdx >= 0 && activeIdx != selectedIdx) {
            tft.setTextColor(COL_WARN, COL_BG);
            tft.setCursor(4, y + 34);
            tft.print("Running: ");
            tft.print(projects[activeIdx].id);

            uint32_t elapsed  = millis() - confirmTimer;
            uint32_t left     = (elapsed < CONFIRM_DELAY_MS)
                                ? (CONFIRM_DELAY_MS - elapsed + 999) / 1000
                                : 0;
            tft.setTextColor(COL_TEXT, COL_BG);
            tft.setCursor(4, y + 46);
            tft.printf("Switch in: %lus", (unsigned long)left);

        } else if (activeIdx >= 0 && activeIdx == selectedIdx) {
            tft.setTextColor(COL_TEXT, COL_BG);
            tft.setCursor(4, y + 34);
            tft.print("(already running)");
        }
        break;
    }

    case State::TRACKING: {
        uint32_t sessionSecs = (millis() - sessionStart) / 1000;
        uint32_t totalSecs   = projects[activeIdx].totalSeconds + sessionSecs;

        tft.setTextColor(COL_PLAY, COL_BG);
        tft.setCursor(4, y);
        tft.print("Tracking:");

        tft.setTextColor(COL_ACCENT, COL_BG);
        tft.setTextSize(2);
        tft.setCursor(4, y + 10);
        tft.print(projects[activeIdx].id);
        tft.setTextSize(1);

        char dur[12];
        formatDuration(sessionSecs, dur, sizeof(dur));
        tft.setTextColor(COL_TEXT, COL_BG);
        tft.setCursor(4, y + 32);
        tft.print("Session: ");
        tft.print(dur);

        formatDuration(totalSecs, dur, sizeof(dur));
        tft.setCursor(4, y + 44);
        tft.print("Total:   ");
        tft.print(dur);
        break;
    }

    case State::SETTINGS:
        drawSettings();
        break;

    case State::MANUAL_TIME:
        drawManualTime();
        break;

    case State::USB_MODE: {
        tft.setTextColor(COL_WARN, COL_BG);
        tft.setCursor(4, y);
        tft.print("USB Drive Mode");
        tft.setTextColor(COL_TEXT, COL_BG);
        tft.setCursor(4, y + 16);
        tft.print("Eject from PC before");
        tft.setCursor(4, y + 28);
        tft.print("unplugging cable!");
        break;
    }

    default: break;
    }
}

static void drawUI()
{
    drawStatusBar();
    drawMainContent();
}

// ────────────────────────────────────────────────────────────────────────────
// Project validation  (XXXX.X format)
// ────────────────────────────────────────────────────────────────────────────
static bool isValidProjectCode(const char* code)
{
    if (!code || strlen(code) != 6) return false;
    for (int i = 0; i < 4; i++)
        if (!isdigit((unsigned char)code[i])) return false;
    if (code[4] != '.') return false;
    if (!isdigit((unsigned char)code[5])) return false;
    return true;
}

static void addNewProject()
{
    if (!isValidProjectCode(newProjectBuf)) return;
    if (numProjects >= MAX_PROJECTS) return;
    for (uint8_t i = 0; i < numProjects; i++) {
        if (strcmp(projects[i].id, newProjectBuf) == 0) return; // duplicate
    }
    strlcpy(projects[numProjects].id, newProjectBuf, sizeof(projects[0].id));
    projects[numProjects].totalSeconds = 0;
    numProjects++;
    saveProjects();
    strlcpy(newProjectBuf, "0000.0", sizeof(newProjectBuf));
    editPos = 0;
}

static void enterSettings()
{
    strlcpy(newProjectBuf, "0000.0", sizeof(newProjectBuf));
    editPos = 0;
    state   = State::SETTINGS;
}

// ────────────────────────────────────────────────────────────────────────────
// Settings digit-increment helpers
// ────────────────────────────────────────────────────────────────────────────
static void settingsShortPress()
{
    uint8_t ci = EDIT_POSITIONS[editPos];
    newProjectBuf[ci] = (newProjectBuf[ci] >= '9') ? '0' : newProjectBuf[ci] + 1;
}

static void settingsLongPress()
{
    editPos++;
    if (editPos >= NUM_EDIT_POS) {
        addNewProject();
        editPos = 0;
        state   = State::IDLE;
    }
}

// ────────────────────────────────────────────────────────────────────────────
// Manual time helpers
// ────────────────────────────────────────────────────────────────────────────
static void manualTimeShortPress()
{
    switch (mtField) {
    case 0: manualTm.tm_hour = (manualTm.tm_hour + 1) % 24;     break;
    case 1: manualTm.tm_min  = (manualTm.tm_min  + 1) % 60;     break;
    case 2: manualTm.tm_mday = (manualTm.tm_mday % 31) + 1;     break;
    case 3: manualTm.tm_mon  = (manualTm.tm_mon  + 1) % 12;     break;
    case 4: manualTm.tm_year = ((manualTm.tm_year - (2024 - 1900)) % 20 + (2024 - 1900)); break; // 2024-2043
    }
}

static void manualTimeLongPress()
{
    mtField++;
    if (mtField >= 5) {
        manualTm.tm_sec  = 0;
        manualTm.tm_isdst = -1;
        time_t t = mktime(&manualTm);
        struct timeval tv = { .tv_sec = t, .tv_usec = 0 };
        settimeofday(&tv, NULL);
        ntpSynced = true;
        mtField   = 0;
        state     = State::IDLE;
    }
}

// ────────────────────────────────────────────────────────────────────────────
// OneButton callbacks
// ────────────────────────────────────────────────────────────────────────────
static void onShortPress()
{
    switch (state) {
    case State::IDLE:
    case State::TRACKING:
        selectedIdx    = (activeIdx >= 0) ? activeIdx : 0;
        confirmTimer   = millis();
        confirmPending = (activeIdx >= 0);
        state          = State::SELECTING;
        break;

    case State::SELECTING:
        if (numProjects == 0) break;
        selectedIdx    = (int8_t)((selectedIdx + 1) % numProjects);
        confirmTimer   = millis();  // reset countdown on each cycle press
        confirmPending = (activeIdx >= 0) && (selectedIdx != activeIdx);
        break;

    case State::SETTINGS:
        settingsShortPress();
        break;

    case State::MANUAL_TIME:
        manualTimeShortPress();
        break;

    case State::USB_MODE:
        break;  // no interaction while USB is active
    }
}

static void onLongPress()
{
    switch (state) {
    case State::IDLE:
        enterSettings();
        break;

    case State::SELECTING:
        if (numProjects == 0) {
            enterSettings();
            break;
        }
        if (activeIdx >= 0 && selectedIdx == activeIdx) {
            // Stop the running project
            stopTracking(/*doSave=*/true);
            state = State::IDLE;
        } else {
            // Start (or switch to) the selected project
            if (activeIdx >= 0) stopTracking(/*doSave=*/true);
            startTracking(selectedIdx);
        }
        confirmPending = false;
        break;

    case State::TRACKING:
        stopTracking(/*doSave=*/true);
        state = State::IDLE;
        break;

    case State::SETTINGS:
        settingsLongPress();
        break;

    case State::MANUAL_TIME:
        manualTimeLongPress();
        break;

    case State::USB_MODE:
        break;
    }
}

// ────────────────────────────────────────────────────────────────────────────
// USB MSC setup
// ────────────────────────────────────────────────────────────────────────────
static void setupUSB()
{
    mscPartition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, NULL);

    if (mscPartition) {
        mscSectors = mscPartition->size / MSC_SECTOR_SIZE;
    }

    usb_msc.setID("ShiftLog", "DataDrive", "1.0");
    usb_msc.setCapacity(mscSectors, MSC_SECTOR_SIZE);
    usb_msc.setReadWriteCallback(msc_read_cb, msc_write_cb, msc_flush_cb);
    usb_msc.setStartStopCallback(msc_startstop_cb);
    usb_msc.setUnitReady(true);
    usb_msc.begin();
}

// ────────────────────────────────────────────────────────────────────────────
// WiFi + NTP setup
// ────────────────────────────────────────────────────────────────────────────
static void setupWiFi()
{
    tft.fillRect(0, STATUS_BAR_H, DISPLAY_W,
                 (uint16_t)(DISPLAY_H - STATUS_BAR_H), COL_BG);
    tft.setTextColor(COL_TEXT, COL_BG);
    tft.setTextSize(1);
    tft.setCursor(4, STATUS_BAR_H + 4);
    tft.print("Connecting WiFi...");
    tft.setCursor(4, STATUS_BAR_H + 18);
    tft.print("AP: ShiftLog-AP");

    WiFiManager wm;
    wm.setConfigPortalTimeout(120);
    wm.setConnectTimeout(20);

    bool ok = wm.autoConnect("ShiftLog-AP", "shiftlog");
    wifiConnected = ok;

    if (ok) {
        tft.setCursor(4, STATUS_BAR_H + 32);
        tft.print("Syncing NTP...");
        syncNTP();
    }

    if (!ntpSynced) {
        // Fall back to manual time entry
        getLocalTime(&manualTm, 0);
        if (manualTm.tm_year < 100) {   // struct is zero/garbage
            manualTm = {};
            manualTm.tm_year  = 2024 - 1900;   // default to 2024
            manualTm.tm_mon   = 0;
            manualTm.tm_mday  = 1;
            manualTm.tm_hour  = 0;
            manualTm.tm_min   = 0;
        }
        mtField = 0;
        state   = State::MANUAL_TIME;
    }
}

// ────────────────────────────────────────────────────────────────────────────
// Arduino setup
// ────────────────────────────────────────────────────────────────────────────
void setup()
{
    Serial.begin(115200);

    // Backlight on
    pinMode(BL_PIN, OUTPUT);
    digitalWrite(BL_PIN, HIGH);

    // TFT
    tft.init();
    tft.setRotation(1);   // landscape: 160 wide × 80 tall
    tft.fillScreen(COL_BG);
    tft.setTextSize(1);
    tft.setTextColor(COL_TEXT, COL_BG);
    tft.setCursor(4, 4);
    tft.print("ESP32-ShiftLog");
    tft.setCursor(4, 18);
    tft.print("Starting...");

    // USB MSC must be initialised before any other USB activity
    setupUSB();

    // LittleFS
    if (!LittleFS.begin(/*formatIfFailed=*/true)) {
        tft.setTextColor(COL_PAUSE, COL_BG);
        tft.setCursor(4, 32);
        tft.print("FS Error!");
        delay(3000);
    } else {
        loadProjects();
    }

    // WiFi / NTP (or fall through to MANUAL_TIME)
    setupWiFi();

    // OneButton
    btn.attachClick(onShortPress);
    btn.attachLongPressStart(onLongPress);

    if (state != State::MANUAL_TIME) {
        state = State::IDLE;
    }

    drawUI();
}

// ────────────────────────────────────────────────────────────────────────────
// Arduino loop
// ────────────────────────────────────────────────────────────────────────────
void loop()
{
    btn.tick();

    uint32_t now = millis();

    // Blink timer — drives project-name flash and settings-digit cursor
    if (now - blinkTimer >= BLINK_INTERVAL_MS) {
        blinkTimer = now;
        blinkState = !blinkState;
    }

    // Auto-confirm: if SELECTING and another project is running, switch after 3 s
    if (state == State::SELECTING && confirmPending) {
        if (now - confirmTimer >= CONFIRM_DELAY_MS) {
            if (activeIdx >= 0 && selectedIdx != activeIdx) {
                stopTracking(/*doSave=*/true);
                startTracking(selectedIdx);
            } else if (activeIdx < 0) {
                startTracking(selectedIdx);
            }
            confirmPending = false;
        }
    }

    // Redraw every 500 ms (covers blink cycles and clock updates)
    static uint32_t lastDraw = 0;
    if (now - lastDraw >= BLINK_INTERVAL_MS) {
        lastDraw = now;
        drawUI();
    }
}
