// SmartGauge Libre — Copyright (c) 2026 khanatuch — code written by khanatuch & Claude (AI)
// SPDX-License-Identifier: GPL-3.0-or-later
// Dashboard UI for 480x272, dark "neon" theme. Three page kinds:
//   GAUGE : segmented RPM arc + speed (left), 6 value cards (right)
//   GRID  : 8 value cards (4x2)
//   DTC   : check-engine status + trouble code list
// Every card is rendered into a sprite and pushed whole, so updates don't flicker.
#pragma once
#include "project.h"
#include "board.h"
#include "obd_pids.h"
#include "dtc.h"
#include "alarms.h"


static constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

static uint16_t mix565(uint16_t a, uint16_t b, float t) {
  t = t < 0 ? 0 : t > 1 ? 1 : t;
  int ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31;
  int br = (b >> 11) & 31, bg = (b >> 5) & 63, bb = b & 31;
  return ((int)(ar + (br - ar) * t) << 11) | ((int)(ag + (bg - ag) * t) << 5) | (int)(ab + (bb - ab) * t);
}

namespace theme {
static constexpr uint16_t BG = rgb(4, 7, 14);
static constexpr uint16_t PANEL = rgb(16, 22, 34);
static constexpr uint16_t EDGE = rgb(38, 50, 70);
static constexpr uint16_t DIM = rgb(26, 34, 48);
static constexpr uint16_t MUTED = rgb(125, 145, 170);
static constexpr uint16_t UNIT = rgb(175, 205, 235);
static constexpr uint16_t TEXT = rgb(240, 245, 250);
static constexpr uint16_t CYAN = rgb(0, 200, 255);
static constexpr uint16_t GREEN = rgb(40, 230, 120);
static constexpr uint16_t AMBER = rgb(255, 180, 0);
static constexpr uint16_t ORANGE = rgb(255, 120, 30);
static constexpr uint16_t RED = rgb(255, 50, 60);
static constexpr uint16_t VIOLET = rgb(170, 100, 255);
}  // namespace theme

// Category stripe on the left of each card.
static uint16_t pidAccent(uint8_t pid) {
  using namespace theme;
  switch (pid) {
    case 0x05: case 0x0F: case 0x46: case 0x5C: case 0x3C: case 0x3D: case 0x3E: case 0x3F:
      return ORANGE;  // temperatures
    case 0x42: case 0x5B: return AMBER;  // electrical
    case 0x0C: case 0x0D: case 0x0E: case 0x5D: case 0x61: case 0x62: case 0x63:
      return VIOLET;  // engine / ignition
    case 0x04: case 0x43: case 0x11: case 0x45: case 0x47: case 0x48: case 0x49:
    case 0x4A: case 0x4B: case 0x4C: case 0x5A:
      return GREEN;  // load / throttle / pedal
  }
  if (pid >= 0x14 && pid <= 0x1B) return AMBER;  // O2 voltage
  return CYAN;                                   // air / fuel / misc
}

class Dashboard {
public:
  static const int BAR_H = 26;
  static const int RPM_MAX = 8000, RPM_RED = 6500;
  static const int GAUGE_CARDS = 6, GRID_CARDS = 8;
  enum Kind { RACE, DIGITAL, GAUGE, TRIP, TIMER, SCOOTER, BLUE_LCD, NAV, GRID, SERVICE, HEALTH, DTC };

  explicit Dashboard(LGFX &l) : lcd(l), gauge(&l), cardS(&l), cardL(&l) {}

  void begin() {
    // Static tach artwork lives in PSRAM and is only pushed on page change;
    // everything redrawn per frame sits in faster internal RAM.
    gauge.setColorDepth(16);
    gauge.setPsram(true);
    gauge.createSprite(GAUGE_W, GAUGE_H);
    renderGaugeBackground();
    makeFast(cardS, SW, SH);
    makeFast(cardL, LW, LH);
    makeFast(speedSpr, 110, 52);
    makeFast(rpmSpr, 110, 26);
  }

  Kind kind() const { return pageKind; }

  void showPage(Kind k, const int *defIdx, int n) {
    pageKind = k;
    nCards = n;
    for (int i = 0; i < n; i++) cards[i] = defIdx[i];
    for (auto &s : shown) s = "\x01";
    shownDtc = 0xFFFFFFFF;
    lastStatus = "";
    if (k == RACE || k == DIGITAL || k == TRIP || k == TIMER || k == SCOOTER || k == BLUE_LCD || k == NAV || k == SERVICE || k == HEALTH) {
      // these pages draw themselves (race.h / trip.h / launch.h / moto.h / nav.h / health.h)
    } else if (k == GAUGE) {
      lcd.fillRect(GAUGE_W, BAR_H, lcd.width() - GAUGE_W, lcd.height() - BAR_H, theme::BG);
      gauge.pushSprite(0, GAUGE_Y);
      for (auto &s : segState) s = 0;
      shownSpeed = shownRpmText = "\x01";
    } else {
      lcd.fillRect(0, BAR_H, lcd.width(), lcd.height() - BAR_H, theme::BG);
    }
  }

  // dot: link state colour; page/pages drawn as dots on the right
  void status(const String &msg, uint16_t dot, bool demo, int page, int pages, int fps,
              uint16_t msgColor = theme::TEXT) {
    String key = msg + dot + demo + page + pages + "/" + fps + "/" + msgColor;
    if (key == lastStatus) return;
    lastStatus = key;
    using namespace theme;
    lcd.fillRect(0, 0, lcd.width(), BAR_H, BG);
    lcd.drawFastHLine(0, BAR_H - 1, lcd.width(), EDGE);
    lcd.drawFastHLine(0, BAR_H - 1, 120, CYAN);
    lcd.fillCircle(10, BAR_H / 2, 4, dot);
    lcd.setFont(&fonts::FreeSans9pt7b);
    lcd.setTextDatum(middle_left);
    lcd.setTextColor(TEXT, BG);
    int sp = pages > 10 ? 8 : 11;  // squeeze the page dots when there are many, so the message keeps its room
    int dx = lcd.width() - 84 - pages * sp;
    lcd.setFont(&fonts::FreeSansBold9pt7b);
    lcd.setTextDatum(middle_right);
    lcd.setTextColor(fps >= 50 ? GREEN : fps >= 30 ? AMBER : RED, BG);
    String f = String(fps) + " FPS";
    lcd.drawString(f, dx - 8, BAR_H / 2);
    int room = dx - 8 - lcd.textWidth(f) - 10 - 21;  // message may not run into the FPS readout
    lcd.setFont(&fonts::FreeSans9pt7b);
    lcd.setTextDatum(middle_left);
    lcd.setTextColor(msgColor, BG);
    String m = msg;
    if (lcd.textWidth(m) > room) {
      room -= lcd.textWidth("..");
      while (m.length() > 3 && lcd.textWidth(m) > room) m.remove(m.length() - 1);
      m += "..";
    }
    lcd.drawString(m, 21, BAR_H / 2);

    for (int i = 0; i < pages; i++) {
      if (i == page) lcd.fillRoundRect(dx + i * sp - 1, BAR_H / 2 - 3, 8, 6, 3, CYAN);
      else lcd.fillCircle(dx + i * sp + 3, BAR_H / 2, 2, EDGE);
    }

    int bx = lcd.width() - 72;  // keep clear of the panel edge (the bezel hides a few pixels)
    lcd.setTextDatum(middle_center);
    lcd.setFont(&fonts::FreeSansBold9pt7b);
    if (demo) {
      lcd.fillRoundRect(bx, 3, 58, BAR_H - 7, 9, AMBER);
      lcd.setTextColor(BG);
    } else {
      lcd.drawRoundRect(bx, 3, 58, BAR_H - 7, 9, EDGE);
      lcd.setTextColor(MUTED);
    }
    lcd.drawString("DEMO", bx + 29, BAR_H / 2 - 1);
  }

  void updateGauge(int rpmIdx, int speedIdx) {
    float rpm = rpmIdx >= 0 ? pidValues[rpmIdx] : NAN;
    float speed = speedIdx >= 0 ? pidValues[speedIdx] : NAN;
    // time-based easing so the tach moves at the same speed at any frame rate
    uint32_t now = millis();
    float dt = (now - lastFrameMs) / 1000.0f;
    lastFrameMs = now;
    float target = isnan(rpm) ? 0 : rpm;
    shownRpm += (target - shownRpm) * constrain(dt * 14.0f, 0.0f, 1.0f);
    drawGauge(shownRpm, rpm, speed);
    updateCards();
  }

  void updateCards() {
    for (int i = 0; i < nCards; i++) drawCard(i);
  }

  bool isRescanButton(int x, int y) const {
    return pageKind == DTC && x >= 360 && y >= BAR_H + 4 && y <= BAR_H + 50;
  }

  void updateDtc(const DtcReport &r, bool connected, bool busy) {
    uint32_t key = r.version * 4 + connected * 2 + busy;
    if (key == shownDtc) return;
    shownDtc = key;
    using namespace theme;
    lcd.fillRect(0, BAR_H, lcd.width(), lcd.height() - BAR_H, BG);

    // check-engine pill
    bool mil = r.valid && r.mil;
    int y0 = BAR_H + 6;
    lcd.fillRoundRect(6, y0, 190, 42, 10, mil ? AMBER : PANEL);
    lcd.drawRoundRect(6, y0, 190, 42, 10, mil ? AMBER : EDGE);
    drawEngineIcon(16, y0 + 9, mil ? BG : (r.valid ? GREEN : MUTED));
    lcd.setFont(&fonts::FreeSansBold9pt7b);
    lcd.setTextDatum(middle_left);
    lcd.setTextColor(mil ? BG : TEXT);
    lcd.drawString(!r.valid ? "CHECK ENGINE" : mil ? "CHECK ENGINE ON" : "CHECK ENGINE OFF", 54, y0 + 21);

    lcd.setFont(&fonts::FreeSans9pt7b);
    if (r.valid) {
      lcd.setTextColor(RED);
      lcd.drawString("Stored  " + String(r.stored.size()), 208, y0 + 11);
      lcd.setTextColor(AMBER);
      lcd.drawString("Pending  " + String(r.pending.size()), 208, y0 + 31);
    }

    // rescan button
    lcd.fillRoundRect(366, y0, 108, 42, 10, PANEL);
    lcd.drawRoundRect(366, y0, 108, 42, 10, CYAN);
    lcd.setFont(&fonts::FreeSansBold9pt7b);
    lcd.setTextDatum(middle_center);
    lcd.setTextColor(CYAN);
    lcd.drawString(busy ? "READING" : "RESCAN", 420, y0 + 21);

    int y = y0 + 54;
    if (!connected && !r.valid) {
      centerMsg("Connect to the car to read trouble codes", MUTED);
      return;
    }
    if (busy && !r.valid) { centerMsg("Reading trouble codes...", CYAN); return; }
    if (r.error.length()) { centerMsg(r.error, ORANGE); return; }
    if (r.stored.empty() && r.pending.empty()) {
      lcd.setFont(&fonts::FreeSansBold18pt7b);
      lcd.setTextDatum(middle_center);
      lcd.setTextColor(GREEN);
      lcd.drawString("No trouble codes", lcd.width() / 2, 170);
      return;
    }

    int row = 0;
    const int ROWS = 6, RH = 31;
    auto list = [&](const std::vector<String> &codes, bool stored) {
      for (auto &c : codes) {
        if (row >= ROWS) return;
        int ry = y + row * RH;
        uint16_t chip = stored ? RED : AMBER;
        lcd.fillRoundRect(6, ry, 468, RH - 4, 6, PANEL);
        lcd.fillRoundRect(6, ry, 86, RH - 4, 6, chip);
        lcd.setFont(&fonts::FreeSansBold12pt7b);
        lcd.setTextDatum(middle_center);
        lcd.setTextColor(BG);
        lcd.drawString(c, 49, ry + (RH - 4) / 2);
        lcd.setFont(&fonts::FreeSans9pt7b);
        lcd.setTextDatum(middle_left);
        lcd.setTextColor(TEXT);
        lcd.drawString(dtcDescription(c), 102, ry + (RH - 4) / 2);
        lcd.setTextDatum(middle_right);
        lcd.setTextColor(stored ? RED : AMBER);
        lcd.drawString(stored ? "STORED" : "PENDING", 466, ry + (RH - 4) / 2);
        row++;
      }
    };
    list(r.stored, true);
    list(r.pending, false);
    int total = r.stored.size() + r.pending.size();
    if (total > ROWS) {
      lcd.setTextDatum(bottom_right);
      lcd.setTextColor(MUTED);
      lcd.drawString("+" + String(total - ROWS) + " more (see serial log)", 474, lcd.height() - 2);
    }
  }

private:
  static const int SW = 108, SH = 76;   // gauge-page card
  static const int LW = 115, LH = 116;  // grid-page card
  static constexpr uint16_t ALARM_BG = rgb(150, 0, 12);

  LGFX &lcd;
  LGFX_Sprite gauge, cardS, cardL;
  Kind pageKind = GAUGE;
  int cards[GRID_CARDS];
  int nCards = 0;
  String shown[GRID_CARDS];
  uint32_t shownDtc = 0xFFFFFFFF;
  float shownRpm = 0;
  String lastStatus;

  void centerMsg(const String &s, uint16_t c) {
    lcd.setFont(&fonts::FreeSans12pt7b);
    lcd.setTextDatum(middle_center);
    lcd.setTextColor(c);
    lcd.drawString(s, lcd.width() / 2, 170);
  }

  // Minimal engine-block glyph, 30x22.
  void drawEngineIcon(int x, int y, uint16_t c) {
    lcd.fillRect(x + 6, y + 6, 18, 14, c);
    lcd.fillRect(x + 10, y + 2, 10, 4, c);
    lcd.fillRect(x + 1, y + 10, 5, 6, c);
    lcd.fillRect(x + 24, y + 9, 5, 8, c);
  }

  void cardPos(int i, int &x, int &y) const {
    if (pageKind == GAUGE) {
      x = 256 + (i % 2) * (SW + 6);
      y = BAR_H + 4 + (i / 2) * (SH + 5);
    } else {
      x = 4 + (i % 4) * (LW + 4);
      y = BAR_H + 4 + (i / 4) * (LH + 4);
    }
  }

  // Unit text; a leading '*' is drawn as a real degree ring ("*C" -> °C).
  static void drawUnit(LGFX_Sprite &s, int cx, int cy, const char *unit) {
    s.setFont(&fonts::FreeSansBold9pt7b);
    s.setTextColor(theme::UNIT);
    bool deg = unit[0] == '*';
    const char *rest = deg ? unit + 1 : unit;
    int w = s.textWidth(rest) + (deg ? 9 : 0);
    int x = cx - w / 2;
    if (deg) {
      s.drawCircle(x + 3, cy - 4, 3, theme::UNIT);
      s.drawCircle(x + 3, cy - 4, 2, theme::UNIT);
      x += 9;
    }
    s.setTextDatum(middle_left);
    s.drawString(rest, x, cy);
  }

  void drawCard(int i) {
    const PidDef &d = PIDS[cards[i]];
    float v = pidValues[cards[i]];
    String s = isnan(v) ? String("--") : String(v, fabsf(v) >= 1000 ? 0 : d.decimals);
    bool alarm = alarms::on(cards[i]);
    bool lit = alarm && alarms::flash();  // flash phase: whole card red
    String key = s + (alarm ? (lit ? "!" : "?") : "");
    if (key == shown[i]) return;
    shown[i] = key;

    bool big = pageKind == GRID;
    LGFX_Sprite &c = big ? cardL : cardS;
    int w = c.width(), h = c.height();
    uint16_t accent = pidAccent(d.pid);
    c.fillSprite(theme::BG);
    c.fillRoundRect(0, 0, w, h, 9, lit ? ALARM_BG : theme::PANEL);
    c.drawRoundRect(0, 0, w, h, 9, alarm ? theme::RED : theme::EDGE);
    if (alarm) c.drawRoundRect(1, 1, w - 2, h - 2, 8, theme::RED);
    c.fillRoundRect(0, 10, 4, h - 20, 2, alarm ? theme::RED : accent);

    c.setFont(&fonts::FreeSans9pt7b);
    c.setTextDatum(top_left);
    c.setTextColor(lit ? theme::TEXT : theme::MUTED);
    c.drawString(d.label, 11, 6);

    // largest font that fits the card
    const lgfx::IFont *fontsBig[] = {&fonts::FreeSansBold24pt7b, &fonts::FreeSansBold18pt7b, &fonts::FreeSansBold12pt7b};
    int first = big ? 0 : 1;
    for (int f = first; f < 3; f++) {
      c.setFont(fontsBig[f]);
      if (c.textWidth(s) <= w - 16) break;
    }
    c.setTextDatum(middle_center);
    c.setTextColor(isnan(v) ? theme::EDGE : lit ? theme::TEXT : alarm ? theme::RED : pidColor(d.pid, v));
    int vy = big ? h / 2 + 2 : h / 2 + 3;
    c.drawString(s, w / 2 + 2, vy);

    if (d.unit[0]) drawUnit(c, w / 2 + 2, h - (big ? 18 : 12), d.unit);
    if (big && !isnan(v) && d.hi > d.lo) {
      // thin level bar, scaled to the typical range
      float f = constrain((v - d.lo) / (d.hi - d.lo), 0.0f, 1.0f);
      c.fillRoundRect(11, h - 6, w - 22, 3, 1, theme::DIM);
      c.fillRoundRect(11, h - 6, (int)((w - 22) * f), 3, 1, accent);
    }

    int x, y;
    cardPos(i, x, y);
    c.pushSprite(x, y);
  }

  static uint16_t mix(uint16_t a, uint16_t b, float t) {
    int ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31;
    int br = (b >> 11) & 31, bg = (b >> 5) & 63, bb = b & 31;
    return ((int)(ar + (br - ar) * t) << 11) | ((int)(ag + (bg - ag) * t) << 5) | (int)(ab + (bb - ab) * t);
  }

  // cyan -> green -> amber -> red along the tach
  static uint16_t tachColor(float f) {
    using namespace theme;
    if (f < 0.45f) return mix(CYAN, GREEN, f / 0.45f);
    if (f < 0.7f) return mix(GREEN, AMBER, (f - 0.45f) / 0.25f);
    if (f < 0.8125f) return mix(AMBER, ORANGE, (f - 0.7f) / 0.1125f);
    return RED;
  }

  static void makeFast(LGFX_Sprite &s, int w, int h) {
    s.setColorDepth(16);
    s.setPsram(false);
    if (!s.createSprite(w, h)) {  // internal RAM full -> fall back to PSRAM
      s.setPsram(true);
      s.createSprite(w, h);
    }
  }

  // Tach geometry (sprite coordinates; the sprite sits at (0, GAUGE_Y) on screen).
  static const int GAUGE_W = 250, GAUGE_H = 244, GAUGE_Y = BAR_H + 2;
  static const int GCX = 125, GCY = 124;
  static const int SEG = 40;
  static constexpr float SPAN = 270.0f / SEG;

  LGFX_Sprite speedSpr{&lcd}, rpmSpr{&lcd};
  uint8_t segState[SEG] = {};  // 0 off, 1 lit, 2 leading segment
  String shownSpeed, shownRpmText;
  uint32_t lastFrameMs = 0;

  template <typename G>
  static void arcOn(G &g, int cx, int cy, int r0, int r1, float a0, float a1, uint16_t col) {
    if (a1 <= a0) return;
    if (a0 >= 360) { a0 -= 360; a1 -= 360; }
    if (a1 > 360) {
      g.fillArc(cx, cy, r0, r1, a0, 360, col);
      g.fillArc(cx, cy, r0, r1, 0, a1 - 360, col);
    } else {
      g.fillArc(cx, cy, r0, r1, a0, a1, col);
    }
  }

  static uint16_t segColor(int i, uint8_t state) {
    float f = (i + 0.5f) / SEG;
    if (state == 0) return f * RPM_MAX >= RPM_RED ? rgb(70, 16, 20) : theme::DIM;
    uint16_t c = tachColor(f);
    return state == 2 ? mix(c, theme::TEXT, 0.45f) : c;
  }

  template <typename G>
  void drawSegment(G &g, int cx, int cy, int i, uint8_t state) {
    float a0 = 135 + i * SPAN + 0.8f, a1 = 135 + (i + 1) * SPAN - 0.8f;
    arcOn(g, cx, cy, 100, 119, a0, a1, segColor(i, state));
  }

  // Everything on the tach that never changes, rendered once at boot.
  void renderGaugeBackground() {
    using namespace theme;
    gauge.fillSprite(BG);
    for (int i = 0; i < SEG; i++) drawSegment(gauge, GCX, GCY, i, 0);
    arcOn(gauge, GCX, GCY, 92, 93, 135, 405, EDGE);
    gauge.setFont(&fonts::FreeSansBold9pt7b);
    gauge.setTextDatum(middle_center);
    for (int k = 0; k <= RPM_MAX / 1000; k++) {
      float a = (135 + k * 1000 * 270.0f / RPM_MAX) * DEG_TO_RAD;
      gauge.setTextColor(k * 1000 >= RPM_RED ? RED : TEXT);
      gauge.drawNumber(k, GCX + cosf(a) * 79, GCY + sinf(a) * 79);
    }
    gauge.setFont(&fonts::FreeSansBold9pt7b);
    gauge.setTextColor(CYAN);
    gauge.drawString("km/h", GCX, GCY + 34);
    gauge.setFont(&fonts::FreeSansBold9pt7b);
    gauge.setTextColor(UNIT);
    gauge.drawString("x1000 rpm", GCX, GCY + 60);
  }

  // Per frame: only segments whose state changed, plus the two numbers when they change.
  void drawGauge(float needleRpm, float rpm, float speed) {
    using namespace theme;
    int lit = (int)(constrain(needleRpm / RPM_MAX, 0.0f, 1.0f) * SEG + 0.5f);
    for (int i = 0; i < SEG; i++) {
      uint8_t want = i < lit - 1 ? 1 : i == lit - 1 ? 2 : 0;
      if (want != segState[i]) {
        drawSegment(lcd, GCX, GCY + GAUGE_Y, i, want);
        segState[i] = want;
      }
    }

    String sp = isnan(speed) ? String("--") : String((int)speed);
    if (sp != shownSpeed) {
      shownSpeed = sp;
      speedSpr.fillSprite(BG);
      speedSpr.setFont(&fonts::Font7);
      speedSpr.setTextDatum(middle_center);
      speedSpr.setTextColor(TEXT);
      speedSpr.drawString(sp, speedSpr.width() / 2, speedSpr.height() / 2);
      speedSpr.pushSprite(GCX - speedSpr.width() / 2, GAUGE_Y + GCY - 4 - speedSpr.height() / 2);
    }

    String rt = isnan(rpm) ? String("----") : String((int)rpm);
    if (rt != shownRpmText) {
      shownRpmText = rt;
      rpmSpr.fillSprite(BG);
      rpmSpr.setFont(&fonts::FreeSansBold12pt7b);
      rpmSpr.setTextDatum(middle_center);
      rpmSpr.setTextColor(isnan(rpm) ? EDGE : tachColor(constrain(rpm / RPM_MAX, 0.0f, 1.0f)));
      rpmSpr.drawString(rt, rpmSpr.width() / 2, rpmSpr.height() / 2);
      rpmSpr.pushSprite(GCX - rpmSpr.width() / 2, GAUGE_Y + GCY + 92 - rpmSpr.height() / 2);
    }
  }
};

// Boot screen: project name + author credit. Keep the credit line in forks (see NOTICE).
static void drawSplash(LGFX &lcd) {
  using namespace theme;
  const int cx = lcd.width() / 2;
  lcd.fillScreen(BG);
  // 240-degree arc, drawn as two pieces so no angle crosses 360
  for (int r = 118; r > 108; r--) {
    uint16_t c = mix565(BG, rgb(255, 40, 190), (118 - r) / 10.0f);
    lcd.fillArc(cx, 136, r, r, 150, 360, c);
    lcd.fillArc(cx, 136, r, r, 0, 30, c);
  }
  lcd.fillArc(cx, 136, 104, 107, 150, 360, rgb(255, 60, 200));
  lcd.fillArc(cx, 136, 104, 107, 0, 30, rgb(255, 60, 200));
  lcd.setTextDatum(middle_center);
  lcd.setFont(&fonts::FreeSansBold24pt7b);
  int w1 = lcd.textWidth("SmartGauge"), w2 = lcd.textWidth(" Libre");
  lcd.setTextDatum(middle_left);
  lcd.setTextColor(TEXT);
  lcd.drawString("SmartGauge", cx - (w1 + w2) / 2, 112);
  lcd.setTextColor(rgb(255, 80, 200));
  lcd.drawString(" Libre", cx - (w1 + w2) / 2 + w1, 112);
  lcd.setTextDatum(middle_center);
  lcd.setFont(&fonts::FreeSans9pt7b);
  lcd.setTextColor(UNIT);
  lcd.drawString("Free OBD-II cluster for ESP32-S3", cx, 150);
  lcd.setFont(&fonts::FreeSansBold9pt7b);
  lcd.setTextColor(TEXT);
  lcd.drawString(PROJECT_CREDIT, cx, 182);
  lcd.setFont(&fonts::FreeSans9pt7b);
  lcd.setTextColor(MUTED);
  lcd.drawString(PROJECT_URL "   v" PROJECT_VERSION, cx, 206);
  lcd.setTextColor(CYAN);
  lcd.drawString("GPL-3.0 - free & open source", cx, 250);
}
