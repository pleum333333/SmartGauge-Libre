// SmartGauge Libre — Copyright (c) 2026 khanatuch — code written by khanatuch & Claude (AI)
// SPDX-License-Identifier: GPL-3.0-or-later
// Two motorbike-style LCD pages (480x246 below the status bar):
//
//   ScooterPage — e-scooter LCD: black bezel, teal panel, stepped speed bar that
//                 climbs the left side and runs along the top (0-120 km/h, cyan,
//                 orange from 80), big italic green-yellow 7-segment speed, RPM
//                 steps, fuel bar E..F, battery volts + trip A km, warning lamps,
//                 P (stopped) and READY (engine running).
//   BlueLcdPage — maxi-scooter LCD: blue panel with light "unlit segment" ghosts,
//                 hollow-segment speed, engine run time where a clock would be,
//                 CHECK tag, long fuel bar E..F, average km/L, trip B km, coolant,
//                 volts, and two side pods with lamp icons.
//
// No brand logos or names: shapes and layout only. Every icon is drawn from
// primitives. Rendering follows docs/07: the panel art (gradients, ghost segments,
// labels) is drawn once into a PSRAM sprite at boot; per frame only changed
// segments are drawn straight to the screen, and changed numbers/lamps are
// composed in a small internal-RAM tile (background copied from the art, then the
// value drawn on top) and pushed through a clip rect, so nothing flickers.
#pragma once
#include "ui.h"
#include "race.h"

namespace moto {
static const int Y0 = Dashboard::BAR_H;
static const int W = 480, H = 272 - Y0;

// Drawing with screen coordinates into any target that starts at (ox, oy), with an
// optional italic shear: x moves right by shear * (baseY - y).
struct Pen {
  LovyanGFX *g;
  int ox = 0, oy = 0;
  float shear = 0, baseY = 0;

  void map(float x, float y, int &X, int &Y) const {
    X = (int)lroundf(x + shear * (baseY - y)) - ox;
    Y = (int)lroundf(y) - oy;
  }
  void tri(float x0, float y0, float x1, float y1, float x2, float y2, uint16_t c) const {
    int a, b, d, e, f, h;
    map(x0, y0, a, b);
    map(x1, y1, d, e);
    map(x2, y2, f, h);
    g->fillTriangle(a, b, d, e, f, h, c);
  }
  void quad(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, uint16_t c) const {
    tri(x0, y0, x1, y1, x2, y2, c);
    tri(x0, y0, x2, y2, x3, y3, c);
  }
  void rect(int x, int y, int w, int h, uint16_t c) const { g->fillRect(x - ox, y - oy, w, h, c); }
  void text(const char *s, int x, int y, const lgfx::IFont *f, uint16_t c, uint8_t datum) const {
    g->setFont(f);
    g->setTextDatum(datum);
    g->setTextColor(c);
    g->drawString(s, x - ox, y - oy);
  }
  // "*C" style unit: a degree ring then the rest, starting at x (middle_left).
  void unit(const char *u, int x, int y, const lgfx::IFont *f, uint16_t c) const {
    if (u[0] == '*') {
      g->drawCircle(x + 3 - ox, y - 6 - oy, 3, c);
      g->drawCircle(x + 3 - ox, y - 6 - oy, 2, c);
      x += 9;
      u++;
    }
    text(u, x, y, f, c, middle_left);
  }
};

// ---- 7-segment digits (hexagonal segments, ghost "8" for unlit ones) ----
static const uint8_t SEG7[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};

struct Seg {
  float w, h, t;     // digit box and stroke
  float pitch, dotW; // advance per digit / per '.' or ':'
  float slant;       // px the top leans right
  uint16_t on, off;  // lit / ghost colour
  bool hollow;       // lit segments drawn as outlines filled with `inner`
  uint16_t inner;
};

static void hseg(const Pen &p, float x0, float x1, float y, float t, uint16_t c) {
  float h = t / 2;
  if (x1 - x0 < t) return;
  p.quad(x0 + h, y - h, x1 - h, y - h, x1 - h, y + h, x0 + h, y + h, c);
  p.tri(x0, y, x0 + h, y - h, x0 + h, y + h, c);
  p.tri(x1, y, x1 - h, y - h, x1 - h, y + h, c);
}

static void vseg(const Pen &p, float x, float y0, float y1, float t, uint16_t c) {
  float h = t / 2;
  if (y1 - y0 < t) return;
  p.quad(x - h, y0 + h, x + h, y0 + h, x + h, y1 - h, x - h, y1 - h, c);
  p.tri(x, y0, x - h, y0 + h, x + h, y0 + h, c);
  p.tri(x, y1, x - h, y1 - h, x + h, y1 - h, c);
}

// One segment k (a..g = 0..6) of a digit at (x, y), shrunk by `in` px on every side.
static void segment(const Pen &p, const Seg &s, float x, float y, int k, float in, uint16_t c) {
  float g = max(1.0f, s.t * 0.15f) + in * 1.4f, t = s.t - 2 * in, h2 = s.t / 2, ym = y + s.h / 2;
  switch (k) {
    case 0: hseg(p, x + g, x + s.w - g, y + h2, t, c); break;
    case 1: vseg(p, x + s.w - h2, y + h2 + g, ym - g, t, c); break;
    case 2: vseg(p, x + s.w - h2, ym + g, y + s.h - h2 - g, t, c); break;
    case 3: hseg(p, x + g, x + s.w - g, y + s.h - h2, t, c); break;
    case 4: vseg(p, x + h2, ym + g, y + s.h - h2 - g, t, c); break;
    case 5: vseg(p, x + h2, y + h2 + g, ym - g, t, c); break;
    case 6: hseg(p, x + g, x + s.w - g, ym, t, c); break;
  }
}

static void digit(Pen p, const Seg &s, float x, float y, uint8_t bits) {
  p.shear = s.slant / s.h;
  p.baseY = y + s.h;
  for (int k = 0; k < 7; k++) {
    bool lit = bits >> k & 1;
    segment(p, s, x, y, k, 0, lit ? s.on : s.off);
    if (lit && s.hollow) segment(p, s, x, y, k, max(2.0f, s.t * 0.25f), s.inner);
  }
}

// Draws a fixed-layout string: digits, ' ' (ghost 8), '-', '.' and ':' (narrow slots).
// Keep the format fixed width ("%3d", "%5.1f") so the ghost layout never moves.
static void number(const Pen &pen, const Seg &s, const char *str, float x, float y) {
  Pen p = pen;
  p.shear = s.slant / s.h;
  p.baseY = y + s.h;
  for (const char *c = str; *c; c++) {
    if (*c == '.' || *c == ':') {
      float d = s.t * 0.9f, cx = x + (s.dotW - d) / 2;
      auto dot = [&](float dy) { p.quad(cx, dy, cx + d, dy, cx + d, dy + d, cx, dy + d, s.on); };
      if (*c == '.') dot(y + s.h - d);
      else { dot(y + s.h * 0.28f); dot(y + s.h * 0.66f); }
      x += s.dotW;
      continue;
    }
    uint8_t bits = *c >= '0' && *c <= '9' ? SEG7[*c - '0'] : *c == '-' ? 0x40 : 0;
    digit(pen, s, x, y, bits);
    x += s.pitch;
  }
}

// ---- shared helpers ----
static LGFX_Sprite *tile = nullptr;  // one composing tile for both pages (internal RAM)
static const int TILE_W = 192, TILE_H = 96;

static void beginShared(LGFX &lcd) {
  if (tile) return;
  tile = new LGFX_Sprite(&lcd);
  race::makeSprite(*tile, TILE_W, TILE_H);
}

// Copies the art under (x, y, w, h) into the tile, lets f draw on top, pushes it.
template <typename F>
static void compose(LGFX &lcd, LGFX_Sprite &bg, int x, int y, int w, int h, F f) {
  bg.pushSprite(tile, -x, -(y - Y0));
  Pen p{tile, x, y};
  f(p);
  lcd.setClipRect(x, y, min(w, TILE_W), min(h, TILE_H));
  tile->pushSprite(x, y);
  lcd.clearClipRect();
}

// Rounded panel with a vertical gradient and a tint that fades out to the right.
static void panel(LGFX_Sprite &bg, int x0, int y0, int w, int h, int r, uint16_t top, uint16_t bottom,
                  uint16_t tint, float tintK) {
  for (int y = y0; y < y0 + h; y++) {
    uint16_t row = mix565(top, bottom, (float)(y - y0) / h);
    for (int x = x0; x < x0 + w; x++) {
      int cx = x < x0 + r ? x0 + r : x >= x0 + w - r ? x0 + w - r - 1 : x;
      int cy = y < y0 + r ? y0 + r : y >= y0 + h - r ? y0 + h - r - 1 : y;
      if ((x - cx) * (x - cx) + (y - cy) * (y - cy) > r * r) continue;
      bg.drawPixel(x, y - Y0, mix565(row, tint, tintK * (1.0f - (float)(x - x0) / w)));
    }
  }
}

// Engine-block glyph, 30x22 (same shape as the DTC page's).
static void engineIcon(const Pen &p, int x, int y, uint16_t c) {
  p.rect(x + 6, y + 6, 18, 14, c);
  p.rect(x + 10, y + 2, 10, 4, c);
  p.rect(x + 1, y + 10, 5, 6, c);
  p.rect(x + 24, y + 9, 5, 8, c);
}

static void raceIcon(const Pen &p, int id, int x, int y, uint16_t c) { race::icon(*p.g, id, x - p.ox, y - p.oy, c); }

// Left/right turn arrow, about 26x20, pointing left when dir < 0.
static void arrow(const Pen &p, int cx, int cy, int dir, uint16_t c) {
  p.tri(cx + dir * 13, cy, cx + dir * 1, cy - 10, cx + dir * 1, cy + 10, c);
  p.rect(dir > 0 ? cx - 12 : cx, cy - 4, 12, 9, c);
}

static uint8_t litCount(float v, float step, int n) {
  if (isnan(v) || v <= 0) return 0;
  return (uint8_t)constrain((int)(v / step + 0.5f), 0, n);
}
}  // namespace moto

// ======================================================================================
class ScooterPage {
public:
  struct Input {
    float speed, rpm, coolant, battery, fuel, tripKm;
    bool alarmCoolant, alarmBattery, alarmFuel, mil;
  };

  explicit ScooterPage(LGFX &l) : lcd(l), bg(&l) {}

  void begin() {
    moto::beginShared(lcd);
    race::makeSprite(bg, moto::W, moto::H, true);
    renderBackground();
  }

  void show() {
    bg.pushSprite(0, moto::Y0);
    for (auto &s : barState) s = 0;
    for (auto &s : shown) s = "\x01";
    rpmLit = fuelLit = 0xFF;
    lamps = 0xFFFF;
  }

  void update(const Input &in) {
    using namespace moto;
    // speed bar: only segments whose state changed
    Pen p{&lcd};
    int lit = litCount(in.speed, STEP, BAR_N);
    for (int i = 0; i < BAR_N; i++) {
      uint8_t want = i < lit ? 1 : 0;
      if (want != barState[i]) {
        barState[i] = want;
        barSeg(p, i, want ? (i * STEP >= ORANGE_FROM ? ORANGE_ON : CYAN_ON) : GHOST);
      }
    }
    uint8_t r = litCount(in.rpm, 1000, 8);
    if (r != rpmLit) {
      for (int i = 0; i < 8; i++)
        if ((i < r) != (i < rpmLit) || rpmLit == 0xFF) rpmBar(p, i, i < r ? (i >= 6 ? ORANGE_ON : CYAN_ON) : GHOST);
      rpmLit = r;
    }
    bool flash = in.alarmFuel && alarms::flash();
    uint8_t f = flash ? 0 : litCount(in.fuel, 10, 10);
    if (f != fuelLit) {
      for (int i = 0; i < 10; i++) fuelSeg(p, i, i < f ? (i < 2 ? ORANGE_ON : CYAN_ON) : GHOST);
      fuelLit = f;
    }

    char b[16];
    if (isnan(in.speed)) strcpy(b, "   ");
    else snprintf(b, sizeof b, "%3d", constrain((int)in.speed, 0, 999));
    value(0, b, SP_X - 4, SP_Y - 2, 190, 92, SPEED, SP_X, SP_Y);
    if (isnan(in.coolant)) strcpy(b, "   ");
    else snprintf(b, sizeof b, "%3d", constrain((int)lroundf(in.coolant), -99, 999));
    Seg cs = SMALL;
    if (in.alarmCoolant) cs.on = RED_ON;
    value(1, b, 388, 100, 58, 30, cs, 388, 102);
    if (isnan(in.battery)) strcpy(b, "  . ");
    else snprintf(b, sizeof b, "%4.1f", constrain(in.battery, 0.0f, 99.9f));
    Seg bs = SMALL;
    if (in.alarmBattery) bs.on = RED_ON;
    value(2, b, 196, 188, 62, 28, bs, 200, 190);
    if (isnan(in.tripKm)) strcpy(b, "    . ");
    else snprintf(b, sizeof b, "%6.1f", fmodf(in.tripKm, 10000));
    value(3, b, 286, 188, 98, 28, SMALL, 288, 190);

    bool stopped = !isnan(in.speed) && in.speed < 1;
    bool ready = !isnan(in.rpm) && in.rpm > 300;
    uint16_t want = in.alarmBattery | in.alarmCoolant << 1 | in.mil << 2 | stopped << 3 | ready << 4;
    if (want != lamps) {
      uint16_t was = lamps;
      lamps = want;
      auto changed = [&](int bit) { return was == 0xFFFF || ((was ^ want) >> bit & 1); };
      if (changed(0)) lamp(0, want & 1);
      if (changed(1)) lamp(1, want >> 1 & 1);
      if (changed(2)) lamp(2, want >> 2 & 1);
      if (changed(3)) lamp(3, want >> 3 & 1);
      if (changed(4)) lamp(4, want >> 4 & 1);
    }
  }

private:
  static constexpr uint16_t BEZEL = rgb(10, 10, 12), BEZEL_EDGE = rgb(52, 54, 60);
  static constexpr uint16_t LCD_TOP = rgb(22, 108, 118), LCD_BOTTOM = rgb(10, 60, 72), TINT = rgb(70, 135, 70);
  static constexpr uint16_t GHOST = rgb(38, 122, 128), GHOST_SMALL = rgb(20, 82, 92);
  static constexpr uint16_t CYAN_ON = rgb(120, 248, 252), ORANGE_ON = rgb(255, 112, 40);
  static constexpr uint16_t RED_ON = rgb(255, 60, 50);
  static constexpr uint16_t YG = rgb(205, 240, 40), SCALE = rgb(235, 225, 50);
  static constexpr uint16_t LAMP_OFF = rgb(40, 108, 114), BEZEL_OFF = rgb(70, 72, 78), READY_ON = rgb(60, 230, 110);

  // speed bar path: up the left (0-25), quarter arc (25-35), along the top (35-120)
  static const int BAR_N = 48;
  static constexpr float STEP = 2.5f, ORANGE_FROM = 80, VMAX = 120;
  static constexpr float VX = 56, VB = 212, VT = 106, AX = 96, AY = 106, R = 40, TY = 66, TX = 452, BW = 18;
  static const int SP_X = 200, SP_Y = 84;
  static constexpr moto::Seg SPEED = {50, 88, 12, 60, 0, 10, YG, GHOST, false};
  static constexpr moto::Seg SMALL = {13, 22, 4.5f, 17, 7, 3, CYAN_ON, GHOST_SMALL, false};

  LGFX &lcd;
  LGFX_Sprite bg;
  uint8_t barState[BAR_N] = {};
  uint8_t rpmLit = 0xFF, fuelLit = 0xFF;
  uint16_t lamps = 0xFFFF;
  String shown[4];

  // Centre line point and outward normal at speed v.
  static void pathAt(float v, float &x, float &y, float &nx, float &ny) {
    if (v <= 25) {
      x = VX; y = VB - (VB - VT) * v / 25; nx = -1; ny = 0;
    } else if (v <= 35) {
      float a = PI + (v - 25) / 10 * HALF_PI;
      nx = cosf(a); ny = sinf(a);
      x = AX + R * nx; y = AY + R * ny;
    } else {
      x = AX + (TX - AX) * (v - 35) / (VMAX - 35); y = TY; nx = 0; ny = -1;
    }
  }

  static void barSeg(const moto::Pen &p, int i, uint16_t c) {
    float x0, y0, nx0, ny0, x1, y1, nx1, ny1, gap = 0.35f, h = BW / 2;
    pathAt(i * STEP + gap, x0, y0, nx0, ny0);
    pathAt((i + 1) * STEP - gap, x1, y1, nx1, ny1);
    p.quad(x0 + nx0 * h, y0 + ny0 * h, x1 + nx1 * h, y1 + ny1 * h, x1 - nx1 * h, y1 - ny1 * h, x0 - nx0 * h, y0 - ny0 * h, c);
  }

  static void rpmBar(const moto::Pen &p, int i, uint16_t c) {
    int h = 8 + i * 2, x = 80 + i * 14;
    p.quad(x + 3, 130 - h, x + 14, 130 - h, x + 11, 130, x, 130, c);  // slanted steps
  }

  static void fuelSeg(const moto::Pen &p, int i, uint16_t c) { p.rect(94 + i * 9, 158, 7, 14, c); }

  // Composes value slot `slot` only when its text changes.
  void value(int slot, const char *text, int x, int y, int w, int h, const moto::Seg &s, int dx, int dy) {
    String key = String(text) + s.on;
    if (key == shown[slot]) return;
    shown[slot] = key;
    moto::compose(lcd, bg, x, y, w, h, [&](moto::Pen &p) { moto::number(p, s, text, dx, dy); });
  }

  // 0 battery, 1 coolant, 2 engine (MIL), 3 P (stopped), 4 READY
  void lamp(int id, bool on) {
    using namespace moto;
    static const int LX[3] = {80, 116, 152};
    if (id <= 2) {
      compose(lcd, bg, LX[id], 188, 32, 28, [&](Pen &p) {
        uint16_t c = on ? (id == 2 ? ORANGE_ON : RED_ON) : LAMP_OFF;
        if (id == 0) raceIcon(p, race::IC_BATT, LX[id], 190, c);
        else if (id == 1) raceIcon(p, race::IC_TEMP, LX[id], 189, c);
        else engineIcon(p, LX[id], 191, c);
      });
    } else if (id == 3) {
      compose(lcd, bg, 424, 186, 32, 30, [&](Pen &p) {
        uint16_t c = on ? YG : LAMP_OFF;
        p.g->drawCircle(440 - p.ox, 201 - p.oy, 13, c);
        p.g->drawCircle(440 - p.ox, 201 - p.oy, 12, c);
        p.text("P", 441, 202, &fonts::FreeSansBold12pt7b, c, middle_center);
      });
    } else {
      compose(lcd, bg, 250, 238, 80, 26, [&](Pen &p) {
        p.text("READY", 290, 251, &fonts::FreeSansBold9pt7b, on ? READY_ON : BEZEL_OFF, middle_center);
      });
    }
  }

  void renderBackground() {
    using namespace moto;
    Pen p{&bg, 0, Y0};
    bg.fillSprite(BEZEL);
    bg.drawRoundRect(4, 2, W - 8, H - 4, 18, BEZEL_EDGE);
    panel(bg, 10, 30, 460, 200, 16, LCD_TOP, LCD_BOTTOM, TINT, 0.4f);
    bg.drawRoundRect(9, 29 - Y0, 462, 202, 17, BEZEL_EDGE);

    // speed bar ghosts, yellow guide line and scale
    for (int i = 0; i < BAR_N; i++) barSeg(p, i, GHOST);
    float px = 0, py = 0;
    for (float v = 0; v <= VMAX + 0.01f; v += 0.5f) {
      float x, y, nx, ny;
      pathAt(v, x, y, nx, ny);
      float gx = x + nx * (BW / 2 + 4), gy = y + ny * (BW / 2 + 4);
      if (v > 0) {
        bg.drawLine(px, py - Y0, gx, gy - Y0, SCALE);
        bg.drawLine(px + nx, py + ny - Y0, gx + nx, gy + ny - Y0, SCALE);
      }
      px = gx;
      py = gy;
    }
    for (int v = 0; v <= 120; v += 10) {
      float x, y, nx, ny;
      pathAt(v, x, y, nx, ny);
      char s[4];
      snprintf(s, sizeof s, "%d", v);
      if (v < 30) p.text(s, VX - BW / 2 - 8, (int)y - 2, &fonts::FreeSansBold9pt7b, SCALE, middle_right);
      else if (v == 30) p.text(s, (int)(AX + nx * 66), (int)(AY + ny * 66), &fonts::FreeSansBold9pt7b, SCALE, middle_center);
      else p.text(s, (int)x, 44, &fonts::FreeSansBold9pt7b, SCALE, middle_center);
    }

    // left block: RPM steps, fuel bar
    p.text("RPM x1000", 80, 98, &fonts::FreeSansBold9pt7b, CYAN_ON, middle_left);
    for (int i = 0; i < 8; i++) rpmBar(p, i, GHOST);
    p.text("FUEL", 80, 147, &fonts::FreeSansBold9pt7b, CYAN_ON, middle_left);
    p.text("E", 80, 165, &fonts::FreeSansBold9pt7b, ORANGE_ON, middle_left);
    for (int i = 0; i < 10; i++) fuelSeg(p, i, GHOST);
    p.text("F", 186, 165, &fonts::FreeSansBold9pt7b, CYAN_ON, middle_left);

    // speed + units
    number(p, SPEED, "   ", SP_X, SP_Y);
    p.text("km/h", 386, 160, &fonts::FreeSansBold12pt7b, YG, middle_left);
    p.text("WATER", 392, 90, &fonts::FreeSansBold9pt7b, CYAN_ON, middle_left);
    number(p, SMALL, "   ", 388, 102);
    p.unit("*C", 446, 118, &fonts::FreeSansBold9pt7b, CYAN_ON);

    // bottom line: orange step line, volts, trip A
    bg.drawLine(176, 180 - Y0, 192, 182 - Y0, ORANGE_ON);
    bg.drawFastHLine(192, 182 - Y0, 270, ORANGE_ON);
    bg.drawFastHLine(176, 181 - Y0, 2, ORANGE_ON);
    number(p, SMALL, "  . ", 200, 190);
    p.text("V", 258, 204, &fonts::FreeSansBold9pt7b, CYAN_ON, middle_left);
    number(p, SMALL, "    . ", 288, 190);
    p.text("km", 386, 204, &fonts::FreeSansBold9pt7b, CYAN_ON, middle_left);
    p.text("TRIP A", 288, 222, &fonts::FreeSansBold9pt7b, rgb(90, 180, 185), middle_left);

    // bezel strip under the panel: turn arrows + headlight (no signal from OBD) and READY
    arrow(p, 130, 251, -1, BEZEL_OFF);
    arrow(p, 370, 251, 1, BEZEL_OFF);
    bg.fillArc(198, 251 - Y0, 8, 10, 90, 270, BEZEL_OFF);
    bg.fillRect(198, 241 - Y0, 3, 21, BEZEL_OFF);
    for (int k = 0; k < 4; k++) bg.fillRect(203, 243 + k * 5 - Y0, 9, 2, BEZEL_OFF);
  }
};

// ======================================================================================
class BlueLcdPage {
public:
  struct Input {
    float speed, rpm, runS, maf, fuelRate, fuel, tripKm, coolant, battery;
    bool check, live, alarmCoolant, alarmBattery, alarmFuel, alarmOil, mil;
    int health;  // health::Level of the worst check (0 ok, 1 warn, 2 bad)
  };

  explicit BlueLcdPage(LGFX &l) : lcd(l), bg(&l) {}

  void begin() {
    moto::beginShared(lcd);
    race::makeSprite(bg, moto::W, moto::H, true);
    renderBackground();
  }

  void show() {
    bg.pushSprite(0, moto::Y0);
    for (auto &s : shown) s = "\x01";
    fuelLit = 0xFF;
    lamps = 0xFFFFFFFF;
    lastMs = 0;
  }

  // Average km/L since power-on, counted while this page is open (MAF or fuel-rate
  // samples only arrive here). Gasoline: fuel g/s = MAF / 14.7, 740 g per litre.
  float avgKmL() const { return litres > 0.02 ? km / litres : NAN; }

  void update(const Input &in) {
    using namespace moto;
    uint32_t now = millis();
    float lph = !isnan(in.fuelRate) ? in.fuelRate : !isnan(in.maf) ? in.maf * 3600 / 14.7f / 740 : NAN;
    if (lastMs && now - lastMs < 1000 && !isnan(lph) && !isnan(in.speed)) {
      double h = (now - lastMs) / 3600000.0;
      km += in.speed * h;
      litres += lph * h;
    }
    lastMs = now;

    Pen p{&lcd};
    bool flash = in.alarmFuel && alarms::flash();
    uint8_t f = flash ? 0 : litCount(in.fuel, 10, 10);
    if (f != fuelLit) {
      for (int i = 0; i < 10; i++) {
        uint8_t was = fuelLit == 0xFF ? !(i < f) : i < fuelLit;
        if (was != (i < f)) fuelSeg(i, i < f);
      }
      fuelLit = f;
    }

    char b[16];
    if (isnan(in.speed)) strcpy(b, "   ");
    else snprintf(b, sizeof b, "%3d", constrain((int)in.speed, 0, 999));
    value(0, b, 212, 38, 152, 86, SPEED, 216, 42);
    if (isnan(in.runS)) strcpy(b, "  :  ");
    else {
      uint32_t m = (uint32_t)in.runS / 60;
      snprintf(b, sizeof b, "%2u:%02u", (unsigned)(m / 60 % 100), (unsigned)(m % 60));
    }
    value(1, b, 122, 40, 84, 28, SMALL, 124, 42);
    float avg = avgKmL();
    if (isnan(avg)) strcpy(b, "  . ");
    else snprintf(b, sizeof b, "%4.1f", min(avg, 99.9f));
    value(2, b, 122, 188, 60, 28, SMALL, 124, 190);
    if (isnan(in.tripKm)) strcpy(b, "    . ");
    else snprintf(b, sizeof b, "%6.1f", fmodf(in.tripKm, 10000));
    value(3, b, 244, 188, 92, 28, SMALL, 246, 190);
    if (isnan(in.coolant)) strcpy(b, "   ");
    else snprintf(b, sizeof b, "%3d", constrain((int)lroundf(in.coolant), -99, 999));
    Seg cs = SMALL;
    if (in.alarmCoolant) cs.on = RED_ON;
    value(4, b, 186, 222, 56, 28, cs, 188, 224);
    if (isnan(in.battery)) strcpy(b, "  . ");
    else snprintf(b, sizeof b, "%4.1f", constrain(in.battery, 0.0f, 99.9f));
    Seg bs = SMALL;
    if (in.alarmBattery) bs.on = RED_ON;
    value(5, b, 274, 222, 60, 28, bs, 276, 224);

    bool shift = !isnan(in.rpm) && in.rpm >= Dashboard::RPM_RED;
    uint32_t want = in.alarmBattery | in.alarmCoolant << 1 | in.mil << 2 | in.alarmOil << 3 | in.alarmFuel << 4 |
                    (uint32_t)constrain(in.health, 0, 2) << 5 | in.live << 7 | shift << 8 | in.check << 9;
    if (want != lamps) {
      uint32_t was = lamps;
      lamps = want;
      for (int id = 0; id < 9; id++) {
        uint32_t m = id == 5 ? 3u << 5 : id < 5 ? 1u << id : 1u << (id + 1);
        if (was == 0xFFFFFFFF || ((was ^ want) & m)) lamp(id, want);
      }
    }
  }

private:
  static constexpr uint16_t BEZEL = rgb(14, 15, 18), POD = rgb(36, 38, 44), POD_EDGE = rgb(84, 86, 94);
  static constexpr uint16_t STRIP = rgb(40, 210, 90);
  static constexpr uint16_t LCD_TOP = rgb(50, 96, 196), LCD_BOTTOM = rgb(22, 46, 122), TINT = rgb(80, 130, 225);
  static constexpr uint16_t GHOST = rgb(54, 90, 176), GHOST_SMALL = rgb(42, 74, 160), ON = rgb(228, 236, 252);
  static constexpr uint16_t HOLLOW = rgb(30, 58, 140);
  static constexpr uint16_t RED_ON = rgb(255, 70, 70), AMBER_ON = rgb(255, 170, 20), GREEN_ON = rgb(50, 220, 110);
  static constexpr uint16_t BLUE_ON = rgb(60, 130, 255), LAMP_OFF = rgb(62, 64, 72);
  static constexpr moto::Seg SPEED = {42, 78, 11, 50, 0, 0, ON, GHOST, true, HOLLOW};
  static constexpr moto::Seg SMALL = {13, 22, 4.5f, 17, 7, 0, ON, GHOST_SMALL, false};
  static const int LX0 = 114, LY0 = 34, LW = 252, LH = 228;

  LGFX &lcd;
  LGFX_Sprite bg;
  String shown[6];
  uint8_t fuelLit = 0xFF;
  uint32_t lamps = 0xFFFFFFFF;
  uint32_t lastMs = 0;
  double km = 0, litres = 0;

  static void fuelSegAt(const moto::Pen &p, int i, uint16_t c) { p.rect(142 + i * 19, 150, 16, 14, c); }

  void fuelSeg(int i, bool on) {
    moto::compose(lcd, bg, 142 + i * 19, 150, 16, 14, [&](moto::Pen &p) { fuelSegAt(p, i, on ? ON : GHOST); });
  }

  void value(int slot, const char *text, int x, int y, int w, int h, const moto::Seg &s, int dx, int dy) {
    String key = String(text) + s.on;
    if (key == shown[slot]) return;
    shown[slot] = key;
    moto::compose(lcd, bg, x, y, w, h, [&](moto::Pen &p) { moto::number(p, s, text, dx, dy); });
  }

  // Pod lamps: 0 battery, 1 coolant, 2 engine, 3 oil (left pod); 4 fuel, 5 health,
  // 6 OBD link, 7 shift (right pod); 8 = CHECK tag on the panel.
  static void lampAt(int id, int &x, int &y) {
    static const int PX[8] = {24, 66, 24, 66, 388, 430, 388, 430};
    static const int PY[8] = {140, 140, 190, 190, 140, 140, 190, 190};
    if (id < 8) { x = PX[id]; y = PY[id]; }
    else { x = 124; y = 88; }
  }

  void lamp(int id, uint32_t st) {
    using namespace moto;
    int x, y;
    lampAt(id, x, y);
    int w = id == 8 ? 68 : 34, h = id == 8 ? 22 : 30;
    compose(lcd, bg, x - 2, y - 3, w, h, [&](Pen &p) { drawLamp(p, id, st, x, y); });
  }

  static void drawLamp(const moto::Pen &p, int id, uint32_t st, int x, int y) {
    using namespace moto;
    int hl = st >> 5 & 3;
    switch (id) {
      case 0: raceIcon(p, race::IC_BATT, x, y, st & 1 ? RED_ON : LAMP_OFF); break;
      case 1: raceIcon(p, race::IC_TEMP, x, y - 1, st >> 1 & 1 ? RED_ON : LAMP_OFF); break;
      case 2: engineIcon(p, x, y + 1, st >> 2 & 1 ? AMBER_ON : LAMP_OFF); break;
      case 3: raceIcon(p, race::IC_OIL, x, y, st >> 3 & 1 ? RED_ON : LAMP_OFF); break;
      case 4: raceIcon(p, race::IC_FUEL, x + 2, y, st >> 4 & 1 ? AMBER_ON : LAMP_OFF); break;
      case 5: {
        uint16_t c = hl >= 2 ? RED_ON : hl == 1 ? AMBER_ON : LAMP_OFF;
        p.tri(x + 15, y, x + 29, y + 23, x + 1, y + 23, c);
        p.rect(x + 14, y + 8, 3, 9, POD);
        p.rect(x + 14, y + 19, 3, 3, POD);
        break;
      }
      case 6: {
        uint16_t c = st >> 7 & 1 ? GREEN_ON : LAMP_OFF;
        p.g->drawCircle(x + 15 - p.ox, y + 12 - p.oy, 13, c);
        p.g->drawCircle(x + 15 - p.ox, y + 12 - p.oy, 12, c);
        p.text("OK", x + 16, y + 13, &fonts::FreeSansBold9pt7b, c, middle_center);
        break;
      }
      case 7: {
        uint16_t c = st >> 8 & 1 ? AMBER_ON : LAMP_OFF;
        p.tri(x + 15, y, x + 28, y + 13, x + 2, y + 13, c);
        p.rect(x + 10, y + 13, 11, 10, c);
        break;
      }
      case 8: {
        bool on = st >> 9 & 1;
        p.g->fillRoundRect(x - p.ox, y - p.oy, 64, 18, 3, on ? ON : GHOST);
        p.text("CHECK", x + 32, y + 9, &fonts::FreeSansBold9pt7b, on ? LCD_BOTTOM : rgb(82, 120, 200), middle_center);
        break;
      }
    }
  }

  void pod(const moto::Pen &p, int x0, int x1, bool left) {
    int inner = left ? x1 : x0, outer = left ? x0 : x1;
    // trapezoid: taller at the outer edge, top slopes down towards the panel
    p.quad(outer, 40, inner, 58, inner, 258, outer, 258, POD);
    bg.drawLine(outer, 40 - moto::Y0, inner, 58 - moto::Y0, POD_EDGE);
    bg.drawLine(inner, 58 - moto::Y0, inner, 258 - moto::Y0, POD_EDGE);
    // green light strip across the pod with a turn arrow ghost above it
    p.quad(outer, 114, inner, 100, inner, 105, outer, 119, STRIP);
    moto::arrow(p, left ? x0 + 30 : x1 - 30, 86, left ? -1 : 1, LAMP_OFF);
  }

  void renderBackground() {
    using namespace moto;
    Pen p{&bg, 0, Y0};
    bg.fillSprite(BEZEL);
    pod(p, 8, 108, true);
    pod(p, 372, 472, false);
    bg.fillRoundRect(LX0 - 4, LY0 - 4 - Y0, LW + 8, LH + 8, 13, POD_EDGE);
    bg.fillRoundRect(LX0 - 2, LY0 - 2 - Y0, LW + 4, LH + 4, 11, BEZEL);
    panel(bg, LX0, LY0, LW, LH, 10, LCD_TOP, LCD_BOTTOM, TINT, 0.3f);

    // ghosts + fixed labels
    number(p, SPEED, "   ", 216, 42);
    p.text("km/h", 360, 134, &fonts::FreeSansBold12pt7b, ON, middle_right);
    number(p, SMALL, "  :  ", 124, 42);
    p.text("RUN", 124, 77, &fonts::FreeSansBold9pt7b, ON, middle_left);
    raceIcon(p, race::IC_FUEL, 126, 118, ON);
    p.text("E", 124, 158, &fonts::FreeSansBold9pt7b, RED_ON, middle_left);
    for (int i = 0; i < 10; i++) fuelSegAt(p, i, GHOST);
    p.text("F", 334 + 9, 158, &fonts::FreeSansBold9pt7b, ON, middle_left);
    p.text("AVG.", 124, 180, &fonts::FreeSansBold9pt7b, ON, middle_left);
    number(p, SMALL, "  . ", 124, 190);
    p.text("km/L", 184, 206, &fonts::FreeSansBold9pt7b, ON, middle_left);
    p.text("TRIP B", 250, 180, &fonts::FreeSansBold9pt7b, ON, middle_left);
    number(p, SMALL, "    . ", 246, 190);
    p.text("km", 340, 206, &fonts::FreeSansBold9pt7b, ON, middle_left);
    p.text("WATER", 124, 236, &fonts::FreeSansBold9pt7b, ON, middle_left);
    number(p, SMALL, "   ", 188, 224);
    p.unit("*C", 240, 238, &fonts::FreeSansBold9pt7b, ON);
    number(p, SMALL, "  . ", 276, 224);
    p.text("V", 334, 238, &fonts::FreeSansBold9pt7b, ON, middle_left);
    for (int id = 0; id < 9; id++) {
      int x, y;
      lampAt(id, x, y);
      drawLamp(p, id, 0, x, y);
    }
    // headlight lamp ghost on the left pod (no OBD signal for it)
    bg.fillArc(42, 228 - Y0, 8, 10, 90, 270, LAMP_OFF);
    bg.fillRect(42, 218 - Y0, 3, 21, LAMP_OFF);
    for (int k = 0; k < 4; k++) bg.fillRect(47, 220 + k * 5 - Y0, 9, 2, LAMP_OFF);
  }
};
