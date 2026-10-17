// SmartGauge Libre — Copyright (c) 2026 khanatuch — code written by khanatuch & Claude (AI)
// SPDX-License-Identifier: GPL-3.0-or-later
// Supercar-style instrument cluster page (480x246 below the status bar):
//   centre: magenta speedometer 0-240 km/h over 240 deg, needle + digital speed
//   left  : blue tach half-dial (needle sweeps the left half)
//   right : blue coolant half-dial (needle sweeps the right half)
//   corners: ignition advance, fuel level, and six warning telltales.
//
// Everything static is pre-rendered once into a PSRAM sprite. Each frame only the
// small rectangles around moving needles / changed numbers are recomposed in an
// internal-RAM tile (background + needles + text) and pushed, so nothing flickers.
#pragma once
#include "ui.h"

class Cluster {
public:
  struct Input {
    float speed, rpm, coolant, ignition, fuel, battery, oil;
    bool mil;
  };

  explicit Cluster(LGFX &l) : lcd(l), bg(&l), tile(&l), info(&l) {}

  void begin() {
    bg.setColorDepth(16);
    bg.setPsram(true);
    bg.createSprite(W, H);
    renderBackground();
    makeTile(tile, TILE, TILE);
    makeTile(info, 120, 24);
  }

  void show() {
    bg.pushSprite(0, Y0);
    for (auto &n : needles) n.drawnAngle = NAN;
    for (auto &t : texts) t.shown = "\x01";
    for (auto &s : tellState) s = 0xFFFF;
    shownIgn = shownFuel = "\x01";
    first = true;
  }

  void update(const Input &in) {
    uint32_t now = millis();
    float k = constrain((now - lastMs) / 1000.0f * 10.0f, 0.0f, 1.0f);
    lastMs = now;
    if (first) k = 1;
    first = false;
    ease(eSpeed, in.speed, k);
    ease(eRpm, in.rpm, k);
    ease(eCool, in.coolant, k);

    needles[0].angle = 150 + constrain(eSpeed / SPEED_MAX, 0.0f, 1.0f) * 240;
    needles[1].angle = 90 + constrain(eRpm / RPM_MAX, 0.0f, 1.0f) * 180;
    needles[2].angle = 90 - constrain((eCool - COOL_MIN) / (COOL_MAX - COOL_MIN), 0.0f, 1.0f) * 180;

    texts[0].value = isnan(in.speed) ? String("--") : String((int)roundf(in.speed));
    texts[1].value = isnan(in.rpm) ? String("----") : String((int)(in.rpm / 10) * 10);
    texts[2].value = isnan(in.coolant) ? String("--") : String((int)in.coolant);
    texts[1].color = in.rpm >= Dashboard::RPM_RED ? theme::RED : theme::TEXT;
    texts[2].color = in.coolant > 105 ? theme::RED : theme::TEXT;

    // collect dirty rectangles
    int nd = 0;
    Rect dirty[6];
    for (auto &n : needles) {
      if (!isnan(n.drawnAngle) && fabsf(n.angle - n.drawnAngle) < 0.15f) continue;
      Rect r = needleBox(n, n.angle);
      if (!isnan(n.drawnAngle)) r = r.unite(needleBox(n, n.drawnAngle));
      dirty[nd++] = r;
      n.drawnAngle = n.angle;
    }
    for (auto &t : texts) {
      if (t.value == t.shown && t.color == t.shownColor) continue;
      t.shown = t.value;
      t.shownColor = t.color;
      dirty[nd++] = t.box;
    }
    for (int i = 0; i < nd; i++) compose(dirty[i]);

    drawInfo(in);
    drawTelltales(in);
  }

private:
  static const int W = 480, H = 246, Y0 = Dashboard::BAR_H;
  static const int TILE = 128;
  static constexpr float SPEED_MAX = 240, RPM_MAX = 8000, COOL_MIN = 40, COOL_MAX = 130;

  struct Rect {
    int x, y, w, h;
    Rect unite(const Rect &o) const {
      int x0 = min(x, o.x), y0 = min(y, o.y);
      return {x0, y0, max(x + w, o.x + o.w) - x0, max(y + h, o.y + o.h) - y0};
    }
  };
  struct Needle {
    int cx, cy, len, tail;
    float halfW;
    uint16_t color, core;
    int hub;
    float angle, drawnAngle;
  };
  struct Text {
    Rect box;
    int x, y;  // anchor (middle_center)
    const lgfx::IFont *font;
    String value, shown;
    uint16_t color, shownColor;
  };

  // dial centres in sprite coordinates
  static const int CX = 240, CY = 122, CR = 100;
  static const int LX = 74, LY = 124, SR = 78;
  static const int RX = 406, RY = 124;

  LGFX &lcd;
  LGFX_Sprite bg, tile, info;
  Needle needles[3] = {
      {CX, CY, 92, 16, 4.0f, rgb(250, 248, 255), rgb(255, 30, 70), 7, 0, NAN},
      {LX, LY, 70, 10, 3.0f, rgb(255, 90, 40), rgb(255, 230, 200), 6, 0, NAN},
      {RX, RY, 70, 10, 3.0f, rgb(255, 90, 40), rgb(255, 230, 200), 6, 0, NAN},
  };
  Text texts[3] = {
      {{CX - 52, CY + 10, 104, 50}, CX, CY + 35, &fonts::Font7, "", "", theme::TEXT, 0},
      {{LX + 4, LY - 14, 54, 28}, LX + 31, LY, &fonts::FreeSansBold12pt7b, "", "", theme::TEXT, 0},
      {{RX - 58, RY - 14, 54, 28}, RX - 31, RY, &fonts::FreeSansBold12pt7b, "", "", theme::TEXT, 0},
  };
  float eSpeed = 0, eRpm = 0, eCool = COOL_MIN;
  uint32_t lastMs = 0;
  bool first = true;
  String shownIgn, shownFuel;
  uint16_t tellState[6];

  static void makeTile(LGFX_Sprite &s, int w, int h) {
    s.setColorDepth(16);
    s.setPsram(false);
    if (!s.createSprite(w, h)) { s.setPsram(true); s.createSprite(w, h); }
  }

  static void ease(float &e, float target, float k) {
    if (isnan(target)) target = 0;
    e += (target - e) * k;
  }

  static uint16_t mix(uint16_t a, uint16_t b, float t) {
    t = constrain(t, 0.0f, 1.0f);
    int ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31;
    int br = (b >> 11) & 31, bgc = (b >> 5) & 63, bb = b & 31;
    return ((int)(ar + (br - ar) * t) << 11) | ((int)(ag + (bgc - ag) * t) << 5) | (int)(ab + (bb - ab) * t);
  }

  static void polar(int cx, int cy, float deg, float r, float &x, float &y) {
    float a = deg * DEG_TO_RAD;
    x = cx + cosf(a) * r;
    y = cy + sinf(a) * r;
  }

  // ---------- static artwork ----------
  void radialFace(int cx, int cy, int r, uint16_t inner, uint16_t outer, float gamma) {
    for (int i = r; i > 0; i--) bg.fillCircle(cx, cy, i, mix(inner, outer, powf((float)i / r, gamma)));
  }

  void glowRing(int cx, int cy, int r, uint16_t col, int glow) {
    for (int g = glow; g > 0; g--) bg.drawCircle(cx, cy, r + g, mix(theme::BG, col, 0.5f * (1 - (float)g / (glow + 1))));
    bg.drawCircle(cx, cy, r, col);
    bg.drawCircle(cx, cy, r - 1, col);
  }

  void ticks(int cx, int cy, float a0, float a1, int majors, int minorsPer, int r, uint16_t col,
             const char *const *labels, int labelR, int redFrom) {
    int total = majors * minorsPer;
    for (int i = 0; i <= total; i++) {
      float a = a0 + (a1 - a0) * i / total;
      bool major = i % minorsPer == 0;
      float x0, y0, x1, y1;
      polar(cx, cy, a, r - (major ? 11 : 6), x0, y0);
      polar(cx, cy, a, r - 2, x1, y1);
      uint16_t c = i >= redFrom * minorsPer && redFrom >= 0 ? theme::RED : col;
      bg.drawLine(x0, y0, x1, y1, c);
      if (major) {
        float xa, ya;
        polar(cx, cy, a + 0.6f, r - (major ? 11 : 6), xa, ya);
        polar(cx, cy, a + 0.6f, r - 2, x1, y1);
        bg.drawLine(xa, ya, x1, y1, c);
        if (labels && labels[i / minorsPer]) {
          float lx, ly;
          polar(cx, cy, a, labelR, lx, ly);
          outlined(labels[i / minorsPer], lx, ly, c);
        }
      }
    }
  }

  // Bold label with a dark 1 px outline so it stays readable on bright gradients.
  void outlined(const char *s, int x, int y, uint16_t col) {
    bg.setTextColor(rgb(0, 0, 0));
    for (int dy = -1; dy <= 1; dy++)
      for (int dx = -1; dx <= 1; dx++)
        if (dx || dy) bg.drawString(s, x + dx, y + dy);
    bg.setTextColor(col);
    bg.drawString(s, x, y);
  }

  // Dark "glass" plate behind a live number.
  void plate(const Rect &r, uint16_t fill, uint16_t edge) {
    bg.fillRoundRect(r.x - 2, r.y - 1, r.w + 4, r.h + 2, 8, fill);
    bg.drawRoundRect(r.x - 2, r.y - 1, r.w + 4, r.h + 2, 8, edge);
  }

  void sideDial(int cx, int cy, bool left) {
    using namespace theme;
    radialFace(cx, cy, SR, rgb(6, 12, 40), rgb(40, 105, 230), 2.2f);
    glowRing(cx, cy, SR, rgb(170, 205, 255), 5);
    // darker half where the readout sits
    float a0 = left ? 90 : -90, a1 = left ? 270 : 90;
    bg.setFont(&fonts::FreeSansBold9pt7b);
    bg.setTextDatum(middle_center);
    if (left) {
      static const char *const L[] = {"0", "1", "2", "3", "4", "5", "6", "7", "8"};
      ticks(cx, cy, a0, a1, 8, 2, SR - 4, TEXT, L, SR - 27, 7);
      plate(texts[1].box, rgb(3, 8, 26), rgb(110, 160, 240));
      bg.setFont(&fonts::FreeSansBold9pt7b);
      outlined("RPM", texts[1].x, LY - 28, UNIT);
    } else {
      static const char *const L[] = {"130", nullptr, nullptr, "40"};
      // right dial runs 40 (bottom) -> 130 (top) counter-clockwise; draw ticks top->bottom
      ticks(cx, cy, a0, a1, 3, 3, SR - 4, TEXT, L, SR - 30, -1);
      plate(texts[2].box, rgb(3, 8, 26), rgb(110, 160, 240));
      bg.setFont(&fonts::FreeSansBold9pt7b);
      outlined("TEMP", texts[2].x, RY - 28, UNIT);
      bg.setFont(&fonts::FreeSansBold9pt7b);
      bg.drawCircle(texts[2].x - 8, RY + 23, 2, UNIT);
      bg.drawCircle(texts[2].x - 8, RY + 23, 3, UNIT);
      outlined("C", texts[2].x + 2, RY + 27, UNIT);
    }
  }

  void renderBackground() {
    using namespace theme;
    bg.fillSprite(BG);
    sideDial(LX, LY, true);
    sideDial(RX, RY, false);

    // centre dial: outer glow, hot-pink radial face
    for (int g = 10; g > 0; g--) bg.drawCircle(CX, CY, CR + g, mix(BG, rgb(255, 0, 170), 0.55f * (1 - g / 11.0f)));
    bg.fillCircle(CX, CY, CR + 1, rgb(8, 0, 16));
    radialFace(CX, CY, CR, rgb(255, 40, 190), rgb(70, 0, 95), 1.6f);
    bg.drawCircle(CX, CY, CR, rgb(255, 150, 235));
    bg.drawCircle(CX, CY, CR + 1, rgb(255, 90, 215));

    static const char *const SPD[] = {"0", "40", "80", "120", "160", "200", "240"};
    bg.setFont(&fonts::FreeSansBold9pt7b);
    bg.setTextDatum(middle_center);
    plate(texts[0].box, rgb(22, 0, 32), rgb(255, 120, 220));
    ticks(CX, CY, 150, 390, 6, 4, CR - 3, TEXT, SPD, CR - 24, -1);
    outlined("km/h", CX, CY + 76, rgb(255, 215, 245));

    // corner labels
    bg.setFont(&fonts::FreeSansBold9pt7b);
    bg.setTextColor(UNIT);
    bg.setTextDatum(top_left);
    bg.drawString("IGNITION", 8, 0);
    bg.setTextDatum(top_right);
    bg.drawString("FUEL", W - 8, 0);
  }

  // ---------- dynamic layer ----------
  void needleTri(const Needle &n, float ang, float &x0, float &y0, float &x1, float &y1, float &x2, float &y2) const {
    float a = ang * DEG_TO_RAD, dx = cosf(a), dy = sinf(a);
    x0 = n.cx + dx * n.len;
    y0 = n.cy + dy * n.len;
    x1 = n.cx - dx * n.tail - dy * n.halfW;
    y1 = n.cy - dy * n.tail + dx * n.halfW;
    x2 = n.cx - dx * n.tail + dy * n.halfW;
    y2 = n.cy - dy * n.tail - dx * n.halfW;
  }

  Rect needleBox(const Needle &n, float ang) const {
    float x0, y0, x1, y1, x2, y2;
    needleTri(n, ang, x0, y0, x1, y1, x2, y2);
    int l = min(min(x0, x1), min(x2, (float)n.cx - n.hub)) - 5;
    int t = min(min(y0, y1), min(y2, (float)n.cy - n.hub)) - 5;
    int r = max(max(x0, x1), max(x2, (float)n.cx + n.hub)) + 5;
    int b = max(max(y0, y1), max(y2, (float)n.cy + n.hub)) + 5;
    return {l, t, r - l, b - t};
  }

  void drawNeedle(LGFX_Sprite &g, int ox, int oy, const Needle &n) {
    float x0, y0, x1, y1, x2, y2;
    needleTri(n, n.drawnAngle, x0, y0, x1, y1, x2, y2);
    // dark outline first so the needle reads on any face colour
    Needle o = n;
    o.halfW += 1.8f;
    o.len += 2;
    o.tail += 1;
    float p0, q0, p1, q1, p2, q2;
    needleTri(o, n.drawnAngle, p0, q0, p1, q1, p2, q2);
    g.fillTriangle(p0 + ox, q0 + oy, p1 + ox, q1 + oy, p2 + ox, q2 + oy, rgb(12, 0, 18));
    g.fillTriangle(x0 + ox, y0 + oy, x1 + ox, y1 + oy, x2 + ox, y2 + oy, n.color);
    float a = n.drawnAngle * DEG_TO_RAD;
    g.drawLine(n.cx + ox, n.cy + oy, n.cx + ox + cosf(a) * n.len * 0.8f, n.cy + oy + sinf(a) * n.len * 0.8f, n.core);
    g.fillCircle(n.cx + ox, n.cy + oy, n.hub, rgb(20, 12, 28));
    g.drawCircle(n.cx + ox, n.cy + oy, n.hub, n.color);
  }

  void drawDynamic(LGFX_Sprite &g, int ox, int oy) {
    for (auto &t : texts) {
      g.setFont(t.font);
      g.setTextDatum(middle_center);
      g.setTextColor(t.color);
      g.drawString(t.value, t.x + ox, t.y + oy);
    }
    for (auto &n : needles) if (!isnan(n.drawnAngle)) drawNeedle(g, ox, oy, n);
  }

  void compose(Rect r) {
    int x0 = max(0, r.x), y0 = max(0, r.y);
    int x1 = min(W, r.x + r.w), y1 = min(H, r.y + r.h);
    for (int ty = y0; ty < y1; ty += TILE) {
      for (int tx = x0; tx < x1; tx += TILE) {
        int tw = min(TILE, x1 - tx), th = min(TILE, y1 - ty);
        bg.pushSprite(&tile, -tx, -ty);
        drawDynamic(tile, -tx, -ty);
        lcd.setClipRect(tx, Y0 + ty, tw, th);
        tile.pushSprite(tx, Y0 + ty);
        lcd.clearClipRect();
      }
    }
  }

  void drawInfo(const Input &in) {
    String ig = isnan(in.ignition) ? String("--") : String(in.ignition, 1);
    if (ig != shownIgn) {
      shownIgn = ig;
      info.fillSprite(theme::BG);
      info.setFont(&fonts::FreeSansBold12pt7b);
      info.setTextDatum(middle_left);
      info.setTextColor(theme::TEXT);
      info.drawString(ig, 0, 12);
      int w = info.textWidth(ig);
      info.setFont(&fonts::FreeSansBold9pt7b);
      info.setTextColor(theme::UNIT);
      info.drawCircle(w + 6, 6, 2, theme::UNIT);
      info.drawString("BTDC", w + 11, 14);
      info.pushSprite(8, Y0 + 16);
    }
    String fu = isnan(in.fuel) ? String("--") : String((int)in.fuel);
    if (fu != shownFuel) {
      shownFuel = fu;
      info.fillSprite(theme::BG);
      info.setFont(&fonts::FreeSansBold9pt7b);
      info.setTextDatum(middle_right);
      info.setTextColor(theme::UNIT);
      info.drawString("%", 119, 14);
      int w = info.textWidth("%");
      info.setFont(&fonts::FreeSansBold12pt7b);
      info.setTextColor(!isnan(in.fuel) && in.fuel < 15 ? theme::AMBER : theme::TEXT);
      info.drawString(fu, 117 - w, 12);
      info.pushSprite(W - 128, Y0 + 16);
    }
  }

  // ---------- telltales ----------
  enum { T_ENGINE, T_BATT, T_COOL, T_FUEL, T_OIL, T_SHIFT };

  void drawTelltales(const Input &in) {
    using namespace theme;
    static const uint16_t OFF = rgb(40, 48, 62);
    uint16_t want[6] = {
        in.mil ? AMBER : OFF,
        (!isnan(in.battery) && (in.battery < 12.0f || in.battery > 15.0f)) ? RED : OFF,
        isnan(in.coolant) ? OFF : in.coolant > 105 ? RED : in.coolant < 50 ? CYAN : OFF,
        (!isnan(in.fuel) && in.fuel < 15) ? AMBER : OFF,
        (!isnan(in.oil) && in.oil > 130) ? RED : OFF,
        (!isnan(in.rpm) && in.rpm >= Dashboard::RPM_RED) ? GREEN : OFF,
    };
    static const int XS[6] = {10, 48, 86, 358, 396, 434};
    const int y = Y0 + 212;
    for (int i = 0; i < 6; i++) {
      if (want[i] == tellState[i]) continue;
      tellState[i] = want[i];
      lcd.fillRect(XS[i], y, 34, 28, BG);
      drawIcon(i, XS[i] + 2, y + 3, want[i]);
    }
  }

  void drawIcon(int id, int x, int y, uint16_t c) {
    switch (id) {
      case T_ENGINE:
        lcd.fillRect(x + 6, y + 6, 18, 13, c);
        lcd.fillRect(x + 10, y + 2, 10, 4, c);
        lcd.fillRect(x + 1, y + 9, 5, 6, c);
        lcd.fillRect(x + 24, y + 8, 5, 8, c);
        lcd.fillRect(x + 12, y + 0, 6, 2, c);
        break;
      case T_BATT:
        lcd.drawRect(x + 2, y + 6, 26, 15, c);
        lcd.drawRect(x + 3, y + 7, 24, 13, c);
        lcd.fillRect(x + 6, y + 3, 5, 3, c);
        lcd.fillRect(x + 19, y + 3, 5, 3, c);
        lcd.drawFastHLine(x + 6, y + 13, 6, c);
        lcd.drawFastVLine(x + 9, y + 10, 7, c);
        lcd.drawFastHLine(x + 18, y + 13, 6, c);
        break;
      case T_COOL:
        lcd.fillRect(x + 13, y + 0, 4, 14, c);
        lcd.fillCircle(x + 15, y + 15, 4, c);
        lcd.drawFastHLine(x + 18, y + 3, 4, c);
        lcd.drawFastHLine(x + 18, y + 7, 4, c);
        for (int k = 0; k < 2; k++) {
          int wy = y + 20 + k * 3;
          for (int s = 0; s < 6; s++) lcd.drawLine(x + 2 + s * 4, wy + (s & 1), x + 6 + s * 4, wy + !(s & 1), c);
        }
        break;
      case T_FUEL:
        lcd.drawRect(x + 4, y + 2, 14, 21, c);
        lcd.drawRect(x + 5, y + 3, 12, 19, c);
        lcd.fillRect(x + 7, y + 5, 8, 6, c);
        lcd.fillRect(x + 2, y + 22, 18, 2, c);
        lcd.drawLine(x + 18, y + 8, x + 23, y + 8, c);
        lcd.drawLine(x + 23, y + 8, x + 23, y + 18, c);
        lcd.drawLine(x + 23, y + 18, x + 26, y + 18, c);
        lcd.drawLine(x + 24, y + 5, x + 26, y + 3, c);
        break;
      case T_OIL:
        lcd.fillRect(x + 7, y + 11, 15, 8, c);
        lcd.fillTriangle(x + 22, y + 11, x + 22, y + 15, x + 29, y + 8, c);
        lcd.drawRect(x + 1, y + 11, 6, 6, c);
        lcd.fillRect(x + 12, y + 8, 5, 3, c);
        lcd.fillCircle(x + 28, y + 17, 2, c);
        break;
      case T_SHIFT:
        lcd.fillTriangle(x + 15, y + 0, x + 4, y + 12, x + 26, y + 12, c);
        lcd.fillRect(x + 10, y + 12, 10, 11, c);
        break;
    }
  }
};
