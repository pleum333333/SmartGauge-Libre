// SmartGauge Libre — Copyright (c) 2026 khanatuch — code written by khanatuch & Claude (AI)
// SPDX-License-Identifier: GPL-3.0-or-later
// Hardware test for Sunton ESP32-4827S043 (ESP32-S3, 4.3" 480x272 RGB565 panel)
// Tests: chip/PSRAM/flash info, display colors + gradient + fill-rate,
//        backlight PWM, touch (GT911 capacitive, auto-detected), microSD card.
// Serial monitor: 115200 baud (CH340 on UART0).

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>

// ---- Board pins ----
static const int PIN_BL = 2;
static const int TOUCH_SDA = 19, TOUCH_SCL = 20, TOUCH_RST = 38;
static const int SD_CS = 10, SD_MOSI = 11, SD_SCK = 12, SD_MISO = 13;

class LGFX : public lgfx::LGFX_Device {
  lgfx::Bus_RGB _bus;
  lgfx::Panel_RGB _panel;
  lgfx::Light_PWM _light;

public:
  LGFX() {
    {
      auto cfg = _panel.config();
      cfg.memory_width = cfg.panel_width = 480;
      cfg.memory_height = cfg.panel_height = 272;
      cfg.offset_x = cfg.offset_y = 0;
      _panel.config(cfg);
    }
    {
      auto cfg = _panel.config_detail();
      cfg.use_psram = 1;
      _panel.config_detail(cfg);
    }
    {
      auto cfg = _bus.config();
      cfg.panel = &_panel;
      // B0..B4
      cfg.pin_d0 = 8;  cfg.pin_d1 = 3;  cfg.pin_d2 = 46; cfg.pin_d3 = 9;  cfg.pin_d4 = 1;
      // G0..G5
      cfg.pin_d5 = 5;  cfg.pin_d6 = 6;  cfg.pin_d7 = 7;  cfg.pin_d8 = 15; cfg.pin_d9 = 16; cfg.pin_d10 = 4;
      // R0..R4
      cfg.pin_d11 = 45; cfg.pin_d12 = 48; cfg.pin_d13 = 47; cfg.pin_d14 = 21; cfg.pin_d15 = 14;
      cfg.pin_henable = 40;
      cfg.pin_vsync = 41;
      cfg.pin_hsync = 39;
      cfg.pin_pclk = 42;
      cfg.freq_write = 9000000;
      cfg.hsync_polarity = 0; cfg.hsync_front_porch = 8; cfg.hsync_pulse_width = 4; cfg.hsync_back_porch = 43;
      cfg.vsync_polarity = 0; cfg.vsync_front_porch = 8; cfg.vsync_pulse_width = 4; cfg.vsync_back_porch = 12;
      cfg.pclk_active_neg = 1;
      cfg.de_idle_high = 0;
      cfg.pclk_idle_high = 0;
      _bus.config(cfg);
    }
    _panel.setBus(&_bus);
    {
      auto cfg = _light.config();
      cfg.pin_bl = PIN_BL;
      cfg.freq = 12000;
      cfg.pwm_channel = 7;
      _light.config(cfg);
    }
    _panel.light(&_light);
    setPanel(&_panel);
  }
};

static LGFX lcd;

// ---- Results ----
struct Result { const char *name; bool ok; String info; };
static Result results[8];
static int nResults = 0;
static void addResult(const char *name, bool ok, const String &info) {
  results[nResults++] = {name, ok, info};
  Serial.printf("[%s] %s  %s\n", ok ? "PASS" : "FAIL", name, info.c_str());
}

// ---- GT911 (raw, no library) ----
static uint8_t gtAddr = 0;

static bool gtRead(uint16_t reg, uint8_t *buf, size_t len) {
  Wire.beginTransmission(gtAddr);
  Wire.write(reg >> 8);
  Wire.write(reg & 0xFF);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(gtAddr, (uint8_t)len) != len) return false;
  for (size_t i = 0; i < len; i++) buf[i] = Wire.read();
  return true;
}

static void gtWrite8(uint16_t reg, uint8_t v) {
  Wire.beginTransmission(gtAddr);
  Wire.write(reg >> 8);
  Wire.write(reg & 0xFF);
  Wire.write(v);
  Wire.endTransmission();
}

static bool probe(uint8_t a) {
  Wire.beginTransmission(a);
  return Wire.endTransmission() == 0;
}

static bool gtInit(String &info) {
  pinMode(TOUCH_RST, OUTPUT);
  digitalWrite(TOUCH_RST, LOW);
  delay(10);
  digitalWrite(TOUCH_RST, HIGH);
  delay(60);
  Wire.begin(TOUCH_SDA, TOUCH_SCL, 400000);

  String found;
  for (uint8_t a = 1; a < 127; a++)
    if (probe(a)) found += String(" 0x") + String(a, HEX);
  Serial.printf("I2C scan (SDA=%d SCL=%d):%s\n", TOUCH_SDA, TOUCH_SCL, found.length() ? found.c_str() : " none");

  if (probe(0x5D)) gtAddr = 0x5D;
  else if (probe(0x14)) gtAddr = 0x14;
  if (!gtAddr) { info = "GT911 not found (resistive variant?) I2C:" + found; return false; }

  uint8_t id[4] = {0};
  uint8_t res[4] = {0};
  gtRead(0x8140, id, 4);
  gtRead(0x8048, res, 4);
  char idStr[5] = {(char)id[0], (char)id[1], (char)id[2], (char)id[3], 0};
  info = String("GT") + idStr + " @0x" + String(gtAddr, HEX) +
         " res " + String(res[0] | (res[1] << 8)) + "x" + String(res[2] | (res[3] << 8));
  return true;
}

// Returns number of touch points (0..5), fills first point.
static int gtGetTouch(int &x, int &y) {
  if (!gtAddr) return 0;
  uint8_t st;
  if (!gtRead(0x814E, &st, 1)) return 0;
  if (!(st & 0x80)) return 0;
  int n = st & 0x0F;
  if (n > 0 && n <= 5) {
    uint8_t p[8];
    gtRead(0x8150, p, 8);
    x = p[0] | (p[1] << 8);
    y = p[2] | (p[3] << 8);
  }
  gtWrite8(0x814E, 0);
  return n > 5 ? 0 : n;
}

// ---- Tests ----
static void testSystem() {
  Serial.printf("Chip: %s rev %d, %d cores, %lu MHz\n", ESP.getChipModel(), ESP.getChipRevision(),
                ESP.getChipCores(), (unsigned long)ESP.getCpuFreqMHz());
  uint32_t flash = ESP.getFlashChipSize();
  addResult("Flash", flash >= 4 * 1024 * 1024, String(flash / 1024 / 1024) + " MB");

  uint32_t psram = ESP.getPsramSize();
  bool psramOk = psram > 0;
  if (psramOk) {
    // quick read/write check on a 1 MB PSRAM block
    const size_t N = 1024 * 1024 / 4;
    uint32_t *buf = (uint32_t *)ps_malloc(N * 4);
    if (!buf) psramOk = false;
    else {
      for (size_t i = 0; i < N; i++) buf[i] = i * 2654435761u;
      for (size_t i = 0; i < N; i++) if (buf[i] != i * 2654435761u) { psramOk = false; break; }
      free(buf);
    }
  }
  addResult("PSRAM", psramOk, String(psram / 1024 / 1024) + " MB, free " + String(ESP.getFreePsram() / 1024) + " KB");
}

static void colorBars() {
  const uint16_t cols[] = {TFT_RED, TFT_GREEN, TFT_BLUE, TFT_WHITE, TFT_BLACK};
  const char *names[] = {"RED", "GREEN", "BLUE", "WHITE", "BLACK"};
  for (int i = 0; i < 5; i++) {
    lcd.fillScreen(cols[i]);
    lcd.setTextColor(i == 3 ? TFT_BLACK : TFT_WHITE);
    lcd.setTextDatum(middle_center);
    lcd.setFont(&fonts::FreeSansBold18pt7b);
    lcd.drawString(names[i], lcd.width() / 2, lcd.height() / 2);
    delay(600);
  }
}

static void gradients() {
  int w = lcd.width(), h = lcd.height(), band = h / 4;
  for (int x = 0; x < w; x++) {
    uint8_t v = x * 255 / (w - 1);
    lcd.drawFastVLine(x, 0, band, lcd.color565(v, 0, 0));
    lcd.drawFastVLine(x, band, band, lcd.color565(0, v, 0));
    lcd.drawFastVLine(x, band * 2, band, lcd.color565(0, 0, v));
    lcd.drawFastVLine(x, band * 3, h - band * 3, lcd.color565(v, v, v));
  }
  delay(1500);
}

static void gridAndBorder() {
  lcd.fillScreen(TFT_BLACK);
  for (int x = 0; x < lcd.width(); x += 40) lcd.drawFastVLine(x, 0, lcd.height(), TFT_DARKGREY);
  for (int y = 0; y < lcd.height(); y += 40) lcd.drawFastHLine(0, y, lcd.width(), TFT_DARKGREY);
  lcd.drawRect(0, 0, lcd.width(), lcd.height(), TFT_YELLOW);
  lcd.fillCircle(lcd.width() / 2, lcd.height() / 2, 60, TFT_CYAN);
  lcd.setTextColor(TFT_BLACK);
  lcd.setTextDatum(middle_center);
  lcd.setFont(&fonts::FreeSansBold12pt7b);
  lcd.drawString("480x272", lcd.width() / 2, lcd.height() / 2);
  delay(1500);
}

static void fillRate() {
  const int frames = 30;
  uint32_t t0 = millis();
  for (int i = 0; i < frames; i++) lcd.fillScreen(i & 1 ? TFT_NAVY : TFT_MAROON);
  uint32_t dt = millis() - t0;
  float fps = frames * 1000.0f / (dt ? dt : 1);
  addResult("Display", true, String(fps, 1) + " full-screen fills/s");
}

static void backlightFade() {
  lcd.fillScreen(TFT_WHITE);
  for (int b = 255; b >= 10; b -= 5) { lcd.setBrightness(b); delay(8); }
  for (int b = 10; b <= 255; b += 5) { lcd.setBrightness(b); delay(8); }
  lcd.setBrightness(255);
  addResult("Backlight", true, "PWM fade on GPIO2 (check visually)");
}

static void testSD() {
  SPIClass *spi = new SPIClass(FSPI);
  spi->begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS, *spi, 10000000)) {
    addResult("microSD", false, "no card / mount failed");
    return;
  }
  uint64_t size = SD.cardSize() / (1024 * 1024);
  bool ok = false;
  File f = SD.open("/esp_test.txt", FILE_WRITE);
  if (f) {
    f.print("ESP32-4827S043 OK");
    f.close();
    f = SD.open("/esp_test.txt");
    if (f) { ok = f.readString() == "ESP32-4827S043 OK"; f.close(); }
    SD.remove("/esp_test.txt");
  }
  addResult("microSD", ok, String((uint32_t)size) + " MB, write/read " + (ok ? "ok" : "failed"));
  SD.end();
}

static void showSummary() {
  lcd.fillScreen(TFT_BLACK);
  lcd.setTextDatum(top_left);
  lcd.setFont(&fonts::FreeSansBold12pt7b);
  lcd.setTextColor(TFT_WHITE);
  lcd.drawString("ESP32-4827S043 self test", 10, 8);
  lcd.setFont(&fonts::FreeSans9pt7b);
  for (int i = 0; i < nResults; i++) {
    int y = 42 + i * 24;
    lcd.setTextColor(results[i].ok ? TFT_GREEN : TFT_RED);
    lcd.drawString(results[i].ok ? "PASS" : "FAIL", 10, y);
    lcd.setTextColor(TFT_WHITE);
    lcd.drawString(results[i].name, 70, y);
    lcd.setTextColor(TFT_LIGHTGREY);
    lcd.drawString(results[i].info, 170, y);
  }
  lcd.setTextColor(TFT_YELLOW);
  lcd.drawString(gtAddr ? "Touch the screen to draw. Hold 2 fingers to clear."
                        : "No capacitive touch detected.", 10, lcd.height() - 24);
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n===== ESP32-4827S043 hardware test =====");

  testSystem();

  if (!lcd.init()) {
    addResult("Display", false, "lcd.init() failed");
  } else {
    lcd.setBrightness(255);
    colorBars();
    gradients();
    gridAndBorder();
    fillRate();
    backlightFade();
  }

  String tinfo;
  bool tok = gtInit(tinfo);
  addResult("Touch", tok, tinfo);

  testSD();

  Serial.println("===== done =====");
  showSummary();
}

void loop() {
  static uint32_t lastPrint = 0;
  int x, y;
  int n = gtGetTouch(x, y);
  if (n >= 2) {
    showSummary();
  } else if (n == 1) {
    lcd.fillCircle(x, y, 4, TFT_MAGENTA);
    if (millis() - lastPrint > 100) {
      Serial.printf("touch x=%d y=%d\n", x, y);
      lastPrint = millis();
    }
  }
  delay(10);
}
