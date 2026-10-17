// SmartGauge Libre — Copyright (c) 2026 khanatuch — code written by khanatuch & Claude (AI)
// SPDX-License-Identifier: GPL-3.0-or-later
// Two racing-style pages (480x246 below the status bar):
//
//   RacePage    — carbon-fibre background, big round tach with a lit segment band
//                 (white -> green -> amber -> red), speed inside, shift-light LEDs on
//                 top, boost (+ peak hold) / ignition / intake on the left, load +
//                 throttle rings on the right, water / oil / battery / fuel strip at
//                 the bottom.
//   DigitalPage — amber "OLED" style: chevron shift indicator and four big
//                 7-segment readouts (boost + peak hold, water, air/fuel, battery).
//
// Values with an active alarm (alarms.h) flash red; the caller passes them as a
// bit mask in Input::alarm, one bit per field in field order.
//
// Same rendering rules as the other pages: static art is rendered once into a PSRAM
// sprite, and each frame only changed LEDs, tach segments, ring arcs and numbers are
// redrawn (numbers through small sprites with a solid background, so no flicker).
#pragma once
#include "ui.h"

namespace race {
static constexpr uint16_t CARBON_A = rgb(14, 14, 16);
static constexpr uint16_t CARBON_B = rgb(34, 34, 38);
static constexpr uint16_t PANEL = rgb(10, 10, 12);
static constexpr uint16_t EDGE = rgb(58, 58, 64);
static constexpr uint16_t LABEL = rgb(160, 160, 170);
static constexpr uint16_t WHITE = rgb(240, 242, 245);
static constexpr uint16_t RED = rgb(235, 30, 40);
static constexpr uint16_t DARKRED = rgb(70, 16, 20);
static constexpr uint16_t AMBER = rgb(255, 165, 0);
static constexpr uint16_t GREEN = rgb(50, 220, 90);
static constexpr uint16_t BLUE = rgb(40, 120, 255);
static constexpr uint16_t SEG_OFF = rgb(40, 40, 46);

static void makeSprite(LGFX_Sprite &s, int w, int h, bool psram = false) {
  s.setColorDepth(16);
  s.setPsram(psram);
  if (!s.createSprite(w, h)) { s.setPsram(true); s.createSprite(w, h); }
}

template <typename G>
static void arc(G &g, int cx, int cy, int r0, int r1, float a0, float a1, uint16_t c) {
  if (a1 <= a0) return;
  if (a0 >= 360) { a0 -= 360; a1 -= 360; }
  if (a1 > 360) {
    g.fillArc(cx, cy, r0, r1, a0, 360, c);
    g.fillArc(cx, cy, r0, r1, 0, a1 - 360, c);
  } else {
    g.fillArc(cx, cy, r0, r1, a0, a1, c);
  }
}

// Peak value with a small up-triangle in front, right-aligned in its sprite.
static void peak(LGFX_Sprite &s, int x, int y, float v, const lgfx::IFont *f, uint16_t fg, uint16_t bgc) {
  s.fillSprite(bgc);
  s.setFont(f);
  s.setTextDatum(middle_right);
  s.setTextColor(fg);
  String t = isnan(v) ? String("--") : String(v, 2);
  int w = s.textWidth(t), cy = s.height() / 2, th = s.height() / 5;
  s.drawString(t, s.width() - 1, cy);
  int tx = s.width() - 1 - w - 2 - th * 2;
  s.fillTriangle(tx, cy + th, tx + th * 2, cy + th, tx + th, cy - th, fg);
  s.pushSprite(x, y);
}

// Draws "value" right-aligned at (right, cy) in a reusable sprite with a solid background.
static void number(LGFX &lcd, LGFX_Sprite &s, int x, int y, const String &v, const lgfx::IFont *f,
                   uint16_t fg, uint16_t bgc, uint8_t datum = middle_right) {
  s.fillSprite(bgc);
  s.setFont(f);
  s.setTextDatum(datum);
  s.setTextColor(fg);
  int ax = datum == middle_right ? s.width() - 1 : datum == middle_left ? 0 : s.width() / 2;
  s.drawString(v, ax, s.height() / 2);
  s.pushSprite(x, y);
}

static String fmt(float v, int dec) { return isnan(v) ? String("--") : String(v, dec); }

// Degree-aware unit label ("*C" -> °C) drawn into any target.
template <typename G>
static void unit(G &g, int x, int cy, const char *u, uint16_t c) {
  g.setFont(&fonts::FreeSansBold9pt7b);
  g.setTextDatum(middle_left);
  g.setTextColor(c);
  if (u[0] == '*') {
    g.drawCircle(x + 3, cy - 5, 3, c);
    g.drawCircle(x + 3, cy - 5, 2, c);
    x += 9;
    u++;
  }
  g.drawString(u, x, cy);
}
// Small dash icons (about 30x24) drawn with primitives into any target.
enum Icon { IC_TEMP, IC_OIL, IC_BATT, IC_FUEL };
template <typename G>
static void icon(G &g, int id, int x, int y, uint16_t c) {
  switch (id) {
    case IC_TEMP:
      g.fillRect(x + 13, y + 0, 4, 13, c);
      g.fillCircle(x + 15, y + 14, 4, c);
      g.drawFastHLine(x + 18, y + 3, 4, c);
      g.drawFastHLine(x + 18, y + 7, 4, c);
      for (int k = 0; k < 2; k++)
        for (int s = 0; s < 6; s++) g.drawLine(x + 2 + s * 4, y + 20 + k * 3 + (s & 1), x + 6 + s * 4, y + 20 + k * 3 + !(s & 1), c);
      break;
    case IC_OIL:
      g.fillRect(x + 7, y + 10, 15, 8, c);
      g.fillTriangle(x + 22, y + 10, x + 22, y + 14, x + 29, y + 7, c);
      g.drawRect(x + 1, y + 10, 6, 6, c);
      g.fillRect(x + 12, y + 7, 5, 3, c);
      g.fillCircle(x + 28, y + 16, 2, c);
      break;
    case IC_BATT:
      g.drawRect(x + 2, y + 5, 26, 15, c);
      g.drawRect(x + 3, y + 6, 24, 13, c);
      g.fillRect(x + 6, y + 2, 5, 3, c);
      g.fillRect(x + 19, y + 2, 5, 3, c);
      g.drawFastHLine(x + 6, y + 12, 6, c);
      g.drawFastVLine(x + 9, y + 9, 7, c);
      g.drawFastHLine(x + 18, y + 12, 6, c);
      break;
    case IC_FUEL:
      g.drawRect(x + 4, y + 1, 14, 21, c);
      g.drawRect(x + 5, y + 2, 12, 19, c);
      g.fillRect(x + 7, y + 4, 8, 6, c);
      g.fillRect(x + 2, y + 21, 18, 2, c);
      g.drawLine(x + 18, y + 7, x + 23, y + 7, c);
      g.drawLine(x + 23, y + 7, x + 23, y + 17, c);
      g.drawLine(x + 23, y + 17, x + 26, y + 17, c);
      break;
  }
}
}  // namespace race
class RacePage {
public:
  struct Input {
    float rpm, speed, boostBar, ignition, intake, load, throttle, coolant, oil, battery, fuel;
    float boostPeak;
    uint16_t alarm;  // bit n = field n above (rpm = bit 0 ... fuel = bit 10)
  };
  enum { F_RPM, F_SPEED, F_BOOST, F_IGN, F_INTAKE, F_LOAD, F_THROTTLE, F_COOLANT, F_OIL, F_BATTERY, F_FUEL };

  explicit RacePage(LGFX &l) : lcd(l), bg(&l), spd(&l), rpmS(&l), row(&l), ringS(&l), strip(&l), pk(&l) {}

  // Long-press here resets the boost peak (screen coordinates).
  static bool isPeakArea(int x, int y) {
    return x >= ROW_X && x < ROW_X + 122 && y >= Y0 + ROW_Y[0] && y < Y0 + ROW_Y[0] + 60;
  }

  void begin() {
    race::makeSprite(bg, W, H, true);
    renderBackground();
    race::makeSprite(spd, 96, 48);
    race::makeSprite(rpmS, 96, 24);
    race::makeSprite(row, 80, 30);
    race::makeSprite(ringS, 40, 20);
    race::makeSprite(strip, 54, 22);
    race::makeSprite(pk, 44, 16);
  }

  void show() {
    bg.pushSprite(0, Y0);
    for (auto &s : seg) s = 0xFF;
    for (auto &s : led) s = 0xFFFF;
    for (auto &a : ringDrawn) a = -1;
    for (auto &s : shown) s = "\x01";
    first = true;
  }

  void update(const Input &in) {
    uint32_t now = millis();
    float k = first ? 1 : constrain((now - lastMs) / 1000.0f * 14.0f, 0.0f, 1.0f);
    lastMs = now;
    first = false;
    eRpm += ((isnan(in.rpm) ? 0 : in.rpm) - eRpm) * k;

    drawTach(eRpm);
    drawShiftLights(isnan(in.rpm) ? 0 : in.rpm, now);

    using namespace race;
    bool fl = alarms::flash();
    // alarmed value: red text, and on the flash phase white text on a red box
    auto colors = [&](int field, uint16_t normal, uint16_t normalBg, uint16_t &fg, uint16_t &bgc) {
      fg = normal;
      bgc = normalBg;
      if (!(in.alarm & (1u << field))) return;
      fg = fl ? WHITE : RED;
      bgc = fl ? RED : normalBg;
    };
    uint16_t fg, bgc;
    colors(F_SPEED, WHITE, rgb(0, 0, 0), fg, bgc);
    setText(0, fmt(in.speed, 0), fg, bgc, [&](const String &s) {
      number(lcd, spd, CX - spd.width() / 2, Y0 + CY - 4 - spd.height() / 2, s, &fonts::Font6, fg, bgc, middle_center);
    });
    colors(F_RPM, in.rpm >= Dashboard::RPM_RED ? RED : WHITE, PANEL, fg, bgc);
    setText(1, isnan(in.rpm) ? String("----") : String((int)(in.rpm / 10) * 10), fg, bgc, [&](const String &s) {
      number(lcd, rpmS, CX - rpmS.width() / 2, Y0 + CY + 84 - rpmS.height() / 2, s, &fonts::FreeSansBold12pt7b,
             fg, bgc, middle_center);
    });

    // left column
    const float lv[3] = {in.boostBar, in.ignition, in.intake};
    const int ld[3] = {2, 1, 0};
    for (int i = 0; i < 3; i++) {
      colors(F_BOOST + i, i == 0 && !isnan(lv[i]) && lv[i] > 0.05f ? AMBER : WHITE, PANEL, fg, bgc);
      setText(2 + i, fmt(lv[i], ld[i]), fg, bgc, [&](const String &s) {
        number(lcd, row, ROW_X + 14, Y0 + ROW_Y[i] + 24, s, &fonts::FreeSansBold18pt7b, fg, bgc);
      });
    }
    // boost peak in the free corner right of the "BOOST" label
    setText(11, fmt(in.boostPeak, 2), AMBER, PANEL, [&](const String &) {
      peak(pk, ROW_X + 76, Y0 + ROW_Y[0] + 4, in.boostPeak, &fonts::FreeSansBold9pt7b, AMBER, PANEL);
    });

    // right rings
    drawRing(0, in.load);
    drawRing(1, in.throttle);

    // bottom strip
    const float bv[4] = {in.coolant, in.oil, in.battery, in.fuel};
    const int bd[4] = {0, 0, 1, 0};
    for (int i = 0; i < 4; i++) {
      colors(F_COOLANT + i, WHITE, PANEL, fg, bgc);
      setText(7 + i, fmt(bv[i], bd[i]), fg, bgc, [&](const String &s) {
        number(lcd, strip, STRIP_X[i] + 38, Y0 + STRIP_Y - strip.height() / 2, s, &fonts::FreeSansBold12pt7b, fg, bgc);
      });
    }
  }

private:
  static const int Y0 = Dashboard::BAR_H, W = 480, H = 246;
  static const int CX = 240, CY = 122, R_IN = 86, R_OUT = 100;
  static const int SEG = 48;
  static constexpr float RPM_MAX = 8000;
  static const int ROW_X = 6;
  static constexpr int ROW_Y[3] = {6, 72, 138};
  static constexpr int STRIP_X[4] = {4, 122, 240, 358};
  static const int STRIP_Y = 232;
  static const int RING_X = 410;
  static constexpr int RING_Y[2] = {62, 156};
  static const int LEDS = 12;

  LGFX &lcd;
  LGFX_Sprite bg, spd, rpmS, row, ringS, strip, pk;
  uint8_t seg[SEG];
  uint16_t led[LEDS];
  int ringDrawn[2];
  String shown[12];
  float eRpm = 0;
  uint32_t lastMs = 0;
  bool first = true;

  // Redraws a slot only when its text or colours changed.
  template <typename F>
  void setText(int slot, const String &s, uint16_t fg, uint16_t bgc, F draw) {
    String key = s + '|' + fg + '|' + bgc;
    if (key == shown[slot]) return;
    shown[slot] = key;
    draw(s);
  }

  static float tachAngle(float rpm) { return 135 + constrain(rpm / RPM_MAX, 0.0f, 1.0f) * 270; }

  static uint16_t segColor(int i, uint8_t state) {
    using namespace race;
    float r = (i + 0.5f) / SEG * RPM_MAX;
    if (state == 0) return r >= Dashboard::RPM_RED ? DARKRED : SEG_OFF;
    uint16_t c = r >= Dashboard::RPM_RED ? RED : r >= 6000 ? AMBER : r >= 4500 ? GREEN : WHITE;
    return state == 2 ? mix565(c, rgb(255, 255, 255), 0.5f) : c;
  }

  template <typename G>
  void drawSeg(G &g, int oy, int i, uint8_t state) {
    float span = 270.0f / SEG;
    race::arc(g, CX, CY + oy, R_IN, R_OUT, 135 + i * span + 0.7f, 135 + (i + 1) * span - 0.7f, segColor(i, state));
  }

  void drawTach(float rpm) {
    int lit = (int)(constrain(rpm / RPM_MAX, 0.0f, 1.0f) * SEG + 0.5f);
    for (int i = 0; i < SEG; i++) {
      uint8_t want = i < lit - 1 ? 1 : i == lit - 1 ? 2 : 0;
      if (want == seg[i]) continue;
      seg[i] = want;
      drawSeg(lcd, Y0, i, want);
    }
  }

  void drawShiftLights(float rpm, uint32_t now) {
    using namespace race;
    const float start = 4500, step = (Dashboard::RPM_RED - start) / (LEDS - 1);
    bool limiter = rpm >= Dashboard::RPM_RED + 300;
    bool blink = (now / 62) & 1;  // ~8 Hz
    for (int i = 0; i < LEDS; i++) {
      uint16_t on = i < 4 ? GREEN : i < 8 ? AMBER : RED;
      uint16_t c = limiter ? (blink ? BLUE : SEG_OFF) : rpm >= start + i * step ? on : SEG_OFF;
      if (c == led[i]) continue;
      led[i] = c;
      lcd.fillRoundRect(LED_X0 + i * 15, Y0 + 2, 12, 9, 2, c);
    }
  }
  static const int LED_X0 = CX - (LEDS * 15 - 3) / 2;

  void drawRing(int i, float v) {
    using namespace race;
    int target = isnan(v) ? 0 : (int)(constrain(v, 0.0f, 100.0f) * 2.7f);  // degrees of 270
    int cy = Y0 + RING_Y[i];
    if (target != ringDrawn[i]) {
      int from = ringDrawn[i] < 0 ? 0 : ringDrawn[i];
      if (ringDrawn[i] < 0) arc(lcd, RING_X, cy, 27, 34, 135, 405, SEG_OFF);
      if (target > from) arc(lcd, RING_X, cy, 27, 34, 135 + from, 135 + target, RED);
      else arc(lcd, RING_X, cy, 27, 34, 135 + target, 135 + from, SEG_OFF);
      ringDrawn[i] = target;
    }
    setText(5 + i, fmt(v, 0), WHITE, 0, [&](const String &s) {
      number(lcd, ringS, RING_X - ringS.width() / 2, cy - ringS.height() / 2 + 2, s, &fonts::FreeSansBold12pt7b,
             WHITE, rgb(0, 0, 0), middle_center);
    });
  }

  // ---------- static artwork ----------
  void carbon() {
    for (int y = 0; y < H; y++) {
      for (int x = 0; x < W; x++) {
        bool a = ((x >> 2) + (y >> 2)) & 1;
        int t = a ? (x & 3) : (y & 3);
        bg.drawPixel(x, y, mix565(race::CARBON_A, race::CARBON_B, t / 3.0f * 0.55f));
      }
    }
  }

  void panel(int x, int y, int w, int h) {
    bg.fillRoundRect(x, y, w, h, 6, race::PANEL);
    bg.drawRoundRect(x, y, w, h, 6, race::EDGE);
    bg.fillRect(x, y + 6, 3, h - 12, race::RED);
  }

  void renderBackground() {
    using namespace race;
    carbon();

    // tach bezel + face
    for (int g = 6; g > 0; g--) bg.drawCircle(CX, CY, R_OUT + 6 + g, mix565(CARBON_A, rgb(120, 120, 130), 0.25f * (1 - g / 7.0f)));
    bg.fillCircle(CX, CY, R_OUT + 6, PANEL);
    bg.drawCircle(CX, CY, R_OUT + 6, rgb(110, 110, 120));
    bg.drawCircle(CX, CY, R_OUT + 5, rgb(70, 70, 78));
    for (int i = 0; i < SEG; i++) drawSeg(bg, 0, i, 0);
    // fine ticks just inside the band
    for (int r = 0; r <= RPM_MAX; r += 250) {
      float a = tachAngle(r) * DEG_TO_RAD;
      bool major = r % 1000 == 0;
      int r0 = major ? R_IN - 9 : R_IN - 5;
      bg.drawLine(CX + cosf(a) * r0, CY + sinf(a) * r0, CX + cosf(a) * (R_IN - 2), CY + sinf(a) * (R_IN - 2),
                  r >= Dashboard::RPM_RED ? RED : LABEL);
    }
    // big oblique numerals
    bg.setFont(&fonts::FreeSansBoldOblique12pt7b);
    bg.setTextDatum(middle_center);
    for (int k = 0; k <= 8; k++) {
      float a = tachAngle(k * 1000) * DEG_TO_RAD;
      bg.setTextColor(k * 1000 >= Dashboard::RPM_RED ? RED : WHITE);
      bg.drawNumber(k, CX + cosf(a) * (R_IN - 22), CY + sinf(a) * (R_IN - 22));
    }
    // inner black disc with red ring (speed lives here)
    bg.fillCircle(CX, CY, 56, rgb(0, 0, 0));
    bg.drawCircle(CX, CY, 57, RED);
    bg.drawCircle(CX, CY, 58, RED);
    bg.setFont(&fonts::FreeSansBold9pt7b);
    bg.setTextColor(LABEL);
    bg.drawString("km/h", CX, CY + 36);
    bg.fillRoundRect(CX - 50, CY + 72, 100, 25, 6, PANEL);

    // shift light housing
    bg.fillRoundRect(LED_X0 - 4, 0, LEDS * 15 + 5, 13, 4, rgb(0, 0, 0));

    // left column
    static const char *const LL[3] = {"BOOST", "IGNITION", "INTAKE"};
    static const char *const LU[3] = {"bar", "*", "*C"};
    for (int i = 0; i < 3; i++) {
      panel(ROW_X, ROW_Y[i], 122, 60);
      bg.setFont(&fonts::FreeSansBold9pt7b);
      bg.setTextDatum(top_left);
      bg.setTextColor(LABEL);
      bg.drawString(LL[i], ROW_X + 10, ROW_Y[i] + 4);
      unit(bg, ROW_X + 97, ROW_Y[i] + 40, LU[i], LABEL);
    }

    // right rings
    static const char *const RL[2] = {"LOAD", "THROTTLE"};
    for (int i = 0; i < 2; i++) {
      bg.fillCircle(RING_X, RING_Y[i], 38, PANEL);
      bg.drawCircle(RING_X, RING_Y[i], 38, EDGE);
      bg.fillCircle(RING_X, RING_Y[i], 26, rgb(0, 0, 0));
      bg.setFont(&fonts::FreeSansBold9pt7b);
      bg.setTextDatum(middle_center);
      bg.setTextColor(LABEL);
      bg.drawString("%", RING_X, RING_Y[i] + 16);
      bg.setFont(&fonts::FreeSansBold9pt7b);
      bg.setTextDatum(middle_center);
      bg.setTextColor(LABEL);
      bg.drawString(RL[i], RING_X, RING_Y[i] + 48);
    }

    // bottom strip
    bg.fillRect(0, STRIP_Y - 14, W, 28, PANEL);
    bg.drawFastHLine(0, STRIP_Y - 14, W, RED);
    static const int BI[4] = {IC_TEMP, IC_OIL, IC_BATT, IC_FUEL};
    static const char *const BU[4] = {"*C", "*C", "V", "%"};
    for (int i = 0; i < 4; i++) {
      icon(bg, BI[i], STRIP_X[i] + 4, STRIP_Y - 12, LABEL);
      unit(bg, STRIP_X[i] + 94, STRIP_Y, BU[i], LABEL);
      if (i) bg.drawFastVLine(STRIP_X[i] - 4, STRIP_Y - 9, 18, EDGE);
    }
  }
};

class DigitalPage {
public:
  struct Input {
    float rpm, boostBar, coolant, afr, battery;
    float boostPeak;
    uint8_t alarm;  // bit 0 boost, 1 water, 2 air/fuel, 3 battery (cell order)
  };

  explicit DigitalPage(LGFX &l) : lcd(l), bg(&l), val(&l), pk(&l) {}

  // Long-press on the BOOST cell resets the peak (screen coordinates).
  static bool isPeakArea(int x, int y) {
    return x < W / 2 && y >= Y0 + CELL_Y[0] - 6 && y < Y0 + CELL_Y[1] - 4;
  }

  void begin() {
    race::makeSprite(bg, W, H, true);
    renderBackground();
    race::makeSprite(val, 124, 54);
    race::makeSprite(pk, 92, 22);
  }

  void show() {
    bg.pushSprite(0, Y0);
    for (auto &s : shown) s = "\x01";
    for (auto &c : chev) c = 0xFFFF;
  }

  void update(const Input &in) {
    const float v[4] = {in.boostBar, in.coolant, in.afr, in.battery};
    const int dec[4] = {2, 0, 1, 1};
    bool fl = alarms::flash();
    for (int i = 0; i < 4; i++) {
      String s = isnan(v[i]) ? String("--") : String(v[i], dec[i]);
      uint16_t c = AMB;
      if (in.alarm & (1u << i)) c = fl ? race::RED : AMB_DIM;
      String key = s + c;
      if (key == shown[i]) continue;
      shown[i] = key;
      int x = CELL_X[i % 2], y = CELL_Y[i / 2];
      race::number(lcd, val, x + 66, Y0 + y + 34, s, &fonts::Font7, c, rgb(0, 0, 0));
    }
    String p = race::fmt(in.boostPeak, 2);
    if (p != shown[4]) {
      shown[4] = p;
      race::peak(pk, CELL_X[0] + 224 - 6 - pk.width(), Y0 + CELL_Y[0] - 3, in.boostPeak, &fonts::FreeSansBold12pt7b, AMB,
                 rgb(0, 0, 0));
    }
    drawChevrons(isnan(in.rpm) ? 0 : in.rpm, millis());
  }

private:
  static const int Y0 = Dashboard::BAR_H, W = 480, H = 246;
  static constexpr uint16_t AMB = rgb(255, 150, 0);
  static constexpr uint16_t AMB_DIM = rgb(60, 34, 0);
  static constexpr int CELL_X[2] = {6, 244};
  static constexpr int CELL_Y[2] = {40, 140};
  static const int CHEV = 10;

  LGFX &lcd;
  LGFX_Sprite bg, val, pk;
  String shown[5];  // 4 cells + boost peak
  uint16_t chev[CHEV];

  void chevron(int x, int y, uint16_t c) {
    // ">" made of two thick strokes
    lcd.fillTriangle(x, y, x + 8, y, x + 18, y + 10, c);
    lcd.fillTriangle(x, y, x + 18, y + 10, x + 10, y + 10, c);
    lcd.fillTriangle(x, y + 20, x + 8, y + 20, x + 18, y + 10, c);
    lcd.fillTriangle(x, y + 20, x + 18, y + 10, x + 10, y + 10, c);
  }

  void drawChevrons(float rpm, uint32_t now) {
    const float start = 4000, step = (Dashboard::RPM_RED - start) / (CHEV - 1);
    bool limiter = rpm >= Dashboard::RPM_RED + 300;
    bool blink = (now / 62) & 1;
    for (int i = 0; i < CHEV; i++) {
      uint16_t on = i < 7 ? AMB : race::RED;
      uint16_t c = limiter ? (blink ? race::RED : AMB_DIM) : rpm >= start + i * step ? on : AMB_DIM;
      if (c == chev[i]) continue;
      chev[i] = c;
      chevron(CHEV_X0 + i * 30, Y0 + 6, c);
    }
  }
  static const int CHEV_X0 = 240 - (CHEV * 30 - 12) / 2;

  void renderBackground() {
    bg.fillSprite(rgb(0, 0, 0));
    bg.drawRoundRect(2, 32, W - 4, H - 36, 10, AMB_DIM);
    bg.drawFastVLine(W / 2, 40, H - 52, AMB_DIM);
    bg.drawFastHLine(10, 136, W - 20, AMB_DIM);
    static const char *const L[4] = {"BOOST", "WATER", "AIR / FUEL", "BATTERY"};
    static const char *const U[4] = {"bar", "*C", ":1", "V"};
    for (int i = 0; i < 4; i++) {
      int x = CELL_X[i % 2], y = CELL_Y[i / 2];
      bg.setFont(&fonts::FreeSansBold12pt7b);
      bg.setTextDatum(top_left);
      bg.setTextColor(AMB);
      bg.drawString(L[i], x + 10, y - 2);
      race::unit(bg, x + 224 - 30, y + 54, U[i], AMB);
    }
  }
};
