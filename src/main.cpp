#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include <WiFi.h>

namespace {
constexpr uint8_t PIN_LCD_RST = 21;
constexpr uint8_t PIN_LCD_BL = 45;
constexpr uint8_t PIN_LCD_DC = 18;
constexpr uint8_t PIN_LCD_CS = 9;
constexpr uint8_t PIN_LCD_MOSI = 11;
constexpr uint8_t PIN_LCD_MISO = 12;
constexpr uint8_t PIN_LCD_SCK = 10;
constexpr uint32_t WIFI_SCAN_INTERVAL_MS = 5000;

Adafruit_ST7735 display(PIN_LCD_CS, PIN_LCD_DC, PIN_LCD_RST);


void runDisplayColorTest() {
  display.fillScreen(ST77XX_RED);
  delay(800);
  display.fillScreen(ST77XX_GREEN);
  delay(800);
  display.fillScreen(ST77XX_BLUE);
  delay(800);
}

void printWifiScan() {
  Serial.println("scan start");
  const int networkCount = WiFi.scanNetworks();
  Serial.println("scan done");

  if (networkCount <= 0) {
    Serial.println("no networks found");
    Serial.println();
    return;
  }

  Serial.printf("%d networks found\n", networkCount);
  for (int i = 0; i < networkCount; ++i) {
    Serial.printf("%d: %s (%d)\n", i + 1, WiFi.SSID(i).c_str(), WiFi.RSSI(i));
    delay(10);
  }
  Serial.println();
}
}  // namespace

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);

  pinMode(PIN_LCD_BL, OUTPUT);
  digitalWrite(PIN_LCD_BL, HIGH);

  SPI.begin(PIN_LCD_SCK, PIN_LCD_MISO, PIN_LCD_MOSI, PIN_LCD_CS);
  display.initR(INITR_MINI160x80_PLUGIN);
  display.setRotation(3);
 
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, false);
  delay(100);

  Serial.println("setup done");
}

void loop() {
  runDisplayColorTest();
  printWifiScan();
  delay(WIFI_SCAN_INTERVAL_MS);
}
