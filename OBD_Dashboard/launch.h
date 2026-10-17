// SmartGauge Libre — Copyright (c) 2026 khanatuch — code written by khanatuch & Claude (AI)
// SPDX-License-Identifier: GPL-3.0-or-later
// 0-100 km/h and 100-0 km/h (braking) timer, driven by OBD speed samples.
//
// OBD speed comes in whole km/h at whatever rate the adapter manages (~5-15 Hz
// with only PID 0D polled), so every time here is an estimate between two samples:
//   start of 0-100 : midway between the last 0 km/h sample and the first moving one
//   end of 0-100   : linear interpolation of the 100 km/h crossing
//   start of 100-0 : linear interpolation of the 100 km/h crossing
//   end of 100-0   : midway between the last moving sample and the first 0 km/h one
// The "+/-" shown is half of each end's sample gap added up — the honest worst case.
// The ECU also filters its speed signal, so expect it to differ from a GPS timer.
#pragma once
#include "ui.h"

class LaunchTimer {
public:
  struct Result {
    float sec = NAN, pm = NAN, meters = NAN;
  };
  enum Acc { A_WAIT, A_READY, A_RUN };
  enum Brk { B_WAIT, B_READY, B_RUN };

  bool armed = false;
  Acc acc = A_WAIT;
  Brk brk = B_WAIT;
  Result accLast, accBest, brkLast, brkBest;
  uint32_t version = 0;  // bumps when a result changes

  void arm(bool on) {
    armed = on;
    acc = A_WAIT;
    brk = B_WAIT;
  }

  void clear() {
    accLast = accBest = brkLast = brkBest = Result();
    version++;
  }

  // Elapsed seconds of a run in progress (NAN when not running).
  float accRunning(uint32_t now) const { return acc == A_RUN ? (now - accStart) / 1000.0f : NAN; }
  float brkRunning(uint32_t now) const { return brk == B_RUN ? (now - brkStart) / 1000.0f : NAN; }

  void feed(float v, uint32_t t) {
    if (isnan(v) || !armed) {
      acc = A_WAIT;
      brk = B_WAIT;
      have = false;
      return;
    }
    uint32_t gap = t - pt;
    if (have && gap > MAX_GAP_MS) {  // missed samples: this run cannot be timed fairly
      acc = A_WAIT;
      brk = B_WAIT;
    }
    if (have) {
      stepAcc(v, t, gap);
      stepBrk(v, t, gap);
    } else if (v < STOP) {
      acc = A_READY;
      zeroT = t;
    }
    pv = v;
    pt = t;
    have = true;
  }

private:
  static constexpr float STOP = 0.5f, TARGET = 100;
  static const uint32_t MAX_GAP_MS = 1500, ACC_TIMEOUT = 30000, BRK_TIMEOUT = 20000;

  bool have = false;
  float pv = 0;
  uint32_t pt = 0, zeroT = 0;
  uint32_t accStart = 0, accGap0 = 0, brkStart = 0, brkGap0 = 0;
  float brkMeters = 0, brkMin = 0;

  void finish(Result &last, Result &best, float sec, float pm, float meters) {
    last = {sec, pm, meters};
    if (isnan(best.sec) || sec < best.sec) best = last;
    version++;
  }

  void stepAcc(float v, uint32_t t, uint32_t gap) {
    switch (acc) {
      case A_WAIT:
      case A_READY:
        if (v < STOP) {
          acc = A_READY;
          zeroT = t;
        } else if (acc == A_READY) {
          accGap0 = t - zeroT;
          accStart = zeroT + accGap0 / 2;
          acc = A_RUN;
        }
        break;
      case A_RUN:
        if (v >= TARGET) {
          float end = pt + gap * (TARGET - pv) / (v - pv);
          finish(accLast, accBest, (end - accStart) / 1000.0f, (accGap0 + gap) / 2000.0f, NAN);
          acc = A_WAIT;
        } else if (v < STOP) {
          acc = A_READY;  // stopped again: wait for the next launch
          zeroT = t;
        } else if (t - accStart > ACC_TIMEOUT) {
          acc = A_WAIT;
        }
        break;
    }
  }

  void stepBrk(float v, uint32_t t, uint32_t gap) {
    switch (brk) {
      case B_WAIT:
        if (v >= TARGET) brk = B_READY;
        break;
      case B_READY:
        if (v < TARGET) {
          brkStart = pt + gap * (pv - TARGET) / (pv - v);
          brkGap0 = gap;
          brkMin = v;
          brkMeters = (TARGET + v) / 2 / 3.6f * (t - brkStart) / 1000.0f;
          brk = B_RUN;
          if (v < STOP) endBrake(t, t - brkStart);  // full stop inside one sample gap
        }
        break;
      case B_RUN:
        if (v >= TARGET) {
          brk = B_READY;  // back on the throttle
        } else if (v < STOP) {
          endBrake(t, gap);
        } else if (v > brkMin + 5 || t - brkStart > BRK_TIMEOUT) {
          brk = B_WAIT;  // not a braking run
        } else {
          brkMeters += (pv + v) / 2 / 3.6f * gap / 1000.0f;
          brkMin = min(brkMin, v);
        }
        break;
    }
  }

  void endBrake(uint32_t t, uint32_t gap) {
    float end = t - gap / 2.0f;
    // last stretch: decelerating from pv to 0 somewhere in the gap
    if (brk == B_RUN && pv < TARGET) brkMeters += pv / 2 / 3.6f * (end - pt) / 1000.0f;
    finish(brkLast, brkBest, (end - brkStart) / 1000.0f, (brkGap0 + gap) / 2000.0f, brkMeters);
    brk = B_WAIT;
  }
};

// Two result panels (0-100 | 100-0), a status line, live speed and an ARM button.
class TimerPage {
public:
  explicit TimerPage(LGFX &l) : lcd(l), big(&l), small(&l), statusS(&l), speedS(&l) {}

  void begin() {
    makeSprite(big, 150, 52);
    makeSprite(small, 200, 20);
    makeSprite(statusS, 464, 24);
    makeSprite(speedS, 96, 40);
  }

  static bool isArmButton(int x, int y) { return x >= BTN_X - 8 && y >= BTN_Y - 8; }
  static bool isResultArea(int x, int y) { return y >= PY && y < PY + PH; }

  void show() {
    using namespace theme;
    lcd.fillRect(0, Y0, lcd.width(), lcd.height() - Y0, BG);
    static const char *const NAME[2] = {"0-100 km/h", "100-0 km/h"};
    for (int p = 0; p < 2; p++) {
      int px = PX[p];
      uint16_t acc = p ? ORANGE : GREEN;
      lcd.fillRoundRect(px, PY, PW, PH, 10, PANEL);
      lcd.drawRoundRect(px, PY, PW, PH, 10, EDGE);
      lcd.fillRoundRect(px, PY + 10, 4, 28, 2, acc);
      lcd.setFont(&fonts::FreeSansBold12pt7b);
      lcd.setTextDatum(top_left);
      lcd.setTextColor(acc);
      lcd.drawString(NAME[p], px + 14, PY + 10);
      lcd.setFont(&fonts::FreeSans9pt7b);
      lcd.setTextDatum(top_right);
      lcd.setTextColor(MUTED);
      lcd.drawString(p ? "braking" : "launch", px + PW - 12, PY + 14);
      lcd.setFont(&fonts::FreeSansBold12pt7b);
      lcd.setTextDatum(middle_left);
      lcd.setTextColor(UNIT);
      lcd.drawString("s", px + PW - 32, PY + 74);
    }
    lcd.setFont(&fonts::FreeSansBold9pt7b);
    lcd.setTextDatum(middle_left);
    lcd.setTextColor(UNIT);
    lcd.drawString("km/h", 8 + speedS.width() + 4, BTN_Y + BTN_H / 2 + 6);
    lcd.setFont(&fonts::FreeSans9pt7b);
    lcd.setTextDatum(middle_center);
    lcd.setTextColor(MUTED);
    lcd.drawString("OBD speed", 240, BTN_Y + BTN_H / 2 - 9);
    lcd.drawString("not GPS-exact", 240, BTN_Y + BTN_H / 2 + 11);
    for (auto &s : shown) s = "\x01";
    shownArmed = -1;
  }

  void update(const LaunchTimer &lt, float speed, uint32_t now) {
    using namespace theme;
    const LaunchTimer::Result *last[2] = {&lt.accLast, &lt.brkLast};
    const LaunchTimer::Result *best[2] = {&lt.accBest, &lt.brkBest};
    float run[2] = {lt.accRunning(now), lt.brkRunning(now)};
    for (int p = 0; p < 2; p++) {
      int px = PX[p];
      bool running = !isnan(run[p]);
      float sec = running ? run[p] : last[p]->sec;
      String s = isnan(sec) ? String("--.--") : String(sec, running ? 1 : 2);
      drawBig(p, s, running ? AMBER : TEXT, px + PW - 36 - big.width(), PY + 74 - big.height() / 2);
      String acc = isnan(last[p]->pm) ? String("") : "+/- " + String(last[p]->pm, 2) + " s";
      if (p && !isnan(last[p]->meters)) acc += "   " + String(last[p]->meters, 1) + " m";
      drawSmall(2 + p, acc, MUTED, px + 14, PY + 112);
      String b = isnan(best[p]->sec) ? String("BEST  --") : "BEST  " + String(best[p]->sec, 2) + " s";
      drawSmall(4 + p, b, TEXT, px + 14, PY + 136);
    }

    String st;
    uint16_t sc = MUTED;
    if (!lt.armed) st = "Tap ARM to start timing";
    else if (lt.acc == LaunchTimer::A_RUN) st = "0-100 running...", sc = AMBER;
    else if (lt.brk == LaunchTimer::B_RUN) st = "100-0 running...", sc = AMBER;
    else if (lt.acc == LaunchTimer::A_READY) st = "READY - launch when clear", sc = GREEN;
    else if (lt.brk == LaunchTimer::B_READY) st = "Over 100 - brake hard to time 100-0", sc = GREEN;
    else st = "Stop for 0-100, or pass 100 for 100-0", sc = CYAN;
    if (st + sc != shown[6]) {
      shown[6] = st + sc;
      statusS.fillSprite(BG);
      statusS.setFont(&fonts::FreeSansBold12pt7b);
      if (statusS.textWidth(st) > statusS.width()) statusS.setFont(&fonts::FreeSansBold9pt7b);
      statusS.setTextDatum(middle_center);
      statusS.setTextColor(sc);
      statusS.drawString(st, statusS.width() / 2, statusS.height() / 2);
      statusS.pushSprite(8, STATUS_Y);
    }

    String sp = isnan(speed) ? String("--") : String((int)speed);
    if (sp != shown[7]) {
      shown[7] = sp;
      speedS.fillSprite(BG);
      speedS.setFont(&fonts::FreeSansBold24pt7b);
      speedS.setTextDatum(middle_right);
      speedS.setTextColor(TEXT);
      speedS.drawString(sp, speedS.width() - 1, speedS.height() / 2);
      speedS.pushSprite(8, BTN_Y + BTN_H / 2 - speedS.height() / 2);
    }

    if (shownArmed != lt.armed) {
      shownArmed = lt.armed;
      lcd.fillRoundRect(BTN_X, BTN_Y, BTN_W, BTN_H, 12, lt.armed ? GREEN : PANEL);
      lcd.drawRoundRect(BTN_X, BTN_Y, BTN_W, BTN_H, 12, lt.armed ? GREEN : CYAN);
      lcd.setFont(&fonts::FreeSansBold12pt7b);
      lcd.setTextDatum(middle_center);
      lcd.setTextColor(lt.armed ? BG : CYAN);
      lcd.drawString(lt.armed ? "ARMED" : "ARM", BTN_X + BTN_W / 2, BTN_Y + BTN_H / 2);
    }
  }

private:
  static const int Y0 = Dashboard::BAR_H;
  static const int PY = Y0 + 6, PW = 228, PH = 150;
  static constexpr int PX[2] = {8, 244};
  static const int STATUS_Y = PY + PH + 4;
  static const int BTN_X = 344, BTN_Y = 214, BTN_W = 128, BTN_H = 48;

  LGFX &lcd;
  LGFX_Sprite big, small, statusS, speedS;
  String shown[8];
  int shownArmed = -1;

  static void makeSprite(LGFX_Sprite &s, int w, int h) {
    s.setColorDepth(16);
    s.setPsram(false);
    if (!s.createSprite(w, h)) { s.setPsram(true); s.createSprite(w, h); }
  }

  void drawBig(int slot, const String &s, uint16_t c, int x, int y) {
    String key = s + c;
    if (key == shown[slot]) return;
    shown[slot] = key;
    big.fillSprite(theme::PANEL);
    big.setFont(&fonts::Font7);
    big.setTextDatum(middle_right);
    big.setTextColor(c);
    big.drawString(s, big.width() - 1, big.height() / 2);
    big.pushSprite(x, y);
  }

  void drawSmall(int slot, const String &s, uint16_t c, int x, int cy) {
    if (s == shown[slot]) return;
    shown[slot] = s;
    small.fillSprite(theme::PANEL);
    small.setFont(&fonts::FreeSansBold9pt7b);
    small.setTextDatum(middle_left);
    small.setTextColor(c);
    small.drawString(s, 0, small.height() / 2);
    small.pushSprite(x, cy - small.height() / 2);
  }
};
