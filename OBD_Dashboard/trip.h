// SmartGauge Libre — Copyright (c) 2026 khanatuch — code written by khanatuch & Claude (AI)
// SPDX-License-Identifier: CC-BY-NC-SA-4.0
// Trip computer: two trips (A and B) that both accumulate from every speed sample,
// so B can run across several A resets (e.g. A = today, B = since refuel).
//
// Distance integrates speed over time with the trapezoid rule between samples.
// Gaps over MAX_GAP_MS (link lost, adapter busy) are skipped instead of guessed.
// "Drive" time is time rolling, "idle" time is stopped with the engine running,
// average speed = distance / drive time (stops do not drag it down).
// Saved to NVS (namespace "trip") every SAVE_MS while it changes, so a power cut
// loses at most that much. Lives on the UI core; obdTask only produces samples.
#pragma once
#include <Preferences.h>
#include "ui.h"

struct TripData {
  double km;
  double driveS;  // seconds rolling
  double idleS;   // seconds stopped with engine running
  float maxKmh;
  uint32_t magic;
};

class TripComputer {
public:
  static const int N = 2;
  TripData trip[N];

  explicit TripComputer(bool persist) : persist(persist) {}

  void begin() {
    for (int i = 0; i < N; i++) clearOne(i);
    if (!persist) return;
    Preferences p;
    if (!p.begin("trip", true)) return;
    for (int i = 0; i < N; i++) {
      TripData d;
      if (p.isKey(KEYS[i]) && p.getBytes(KEYS[i], &d, sizeof d) == sizeof d && d.magic == MAGIC) trip[i] = d;
    }
    p.end();
  }

  // One speed sample (km/h, NAN = no answer) taken at `ms`.
  void feed(float kmh, uint32_t ms, bool engineOn) {
    if (isnan(kmh)) {
      havePrev = false;
      if (dirty) saveNow();  // ignition off: keep what we have
      return;
    }
    if (havePrev) {
      uint32_t dt = ms - prevMs;
      if (dt > 0 && dt < MAX_GAP_MS) {
        double s = dt / 1000.0;
        double km = (prevKmh + kmh) * 0.5 * s / 3600.0;
        bool rolling = kmh > 0 || prevKmh > 0;
        for (auto &t : trip) {
          t.km += km;
          if (rolling) t.driveS += s;
          else if (engineOn) t.idleS += s;
          if (kmh > t.maxKmh) t.maxKmh = kmh;
        }
        dirty = true;
      }
    }
    prevKmh = kmh;
    prevMs = ms;
    havePrev = true;
  }

  void reset(int i) {
    clearOne(i);
    saveNow();
  }

  // Call every frame; writes NVS at most every SAVE_MS.
  void loop(uint32_t now) {
    if (dirty && now - lastSave >= SAVE_MS) saveNow();
  }

  float avgKmh(int i) const { return trip[i].driveS >= 1 ? trip[i].km / (trip[i].driveS / 3600.0) : NAN; }

private:
  static const uint32_t MAX_GAP_MS = 3000, SAVE_MS = 30000;
  static const uint32_t MAGIC = 0x54525031;  // "TRP1"
  static constexpr const char *KEYS[N] = {"A", "B"};
  bool persist;
  bool havePrev = false, dirty = false;
  float prevKmh = 0;
  uint32_t prevMs = 0, lastSave = 0;

  void clearOne(int i) { trip[i] = {0, 0, 0, 0, MAGIC}; }

  void saveNow() {
    dirty = false;
    lastSave = millis();
    if (!persist) return;
    Preferences p;
    p.begin("trip", false);
    for (int i = 0; i < N; i++) p.putBytes(KEYS[i], &trip[i], sizeof trip[i]);
    p.end();
  }
};

// Two panels side by side: TRIP A | TRIP B. Static frame drawn on show(), each
// number redrawn through a small sprite only when its text changes.
class TripPage {
public:
  explicit TripPage(LGFX &l) : lcd(l), big(&l), val(&l) {}

  void begin() {
    makeSprite(big, 150, 40);
    makeSprite(val, 100, 24);
  }

  // Long-press on a panel resets that trip; returns 0 / 1.
  static int panelAt(int x) { return x < 240 ? 0 : 1; }

  void show() {
    using namespace theme;
    lcd.fillRect(0, Y0, lcd.width(), lcd.height() - Y0, BG);
    static const char *const NAME[2] = {"TRIP A", "TRIP B"};
    static const char *const ROW[4] = {"DRIVE", "AVERAGE", "MAX", "IDLE"};
    for (int p = 0; p < 2; p++) {
      int px = PX[p];
      lcd.fillRoundRect(px, PY, PW, PH, 10, PANEL);
      lcd.drawRoundRect(px, PY, PW, PH, 10, EDGE);
      lcd.fillRoundRect(px, PY + 10, 4, 28, 2, p ? VIOLET : CYAN);
      lcd.setFont(&fonts::FreeSansBold12pt7b);
      lcd.setTextDatum(top_left);
      lcd.setTextColor(p ? VIOLET : CYAN);
      lcd.drawString(NAME[p], px + 14, PY + 10);
      lcd.setFont(&fonts::FreeSans9pt7b);
      lcd.setTextDatum(top_right);
      lcd.setTextColor(MUTED);
      lcd.drawString("hold = reset", px + PW - 12, PY + 14);
      lcd.drawFastHLine(px + 10, PY + 40, PW - 20, EDGE);
      lcd.setFont(&fonts::FreeSansBold12pt7b);
      lcd.setTextDatum(middle_left);
      lcd.setTextColor(UNIT);
      lcd.drawString("km", px + PW - 46, PY + 70);
      for (int r = 0; r < 4; r++) {
        int cy = PY + ROW_Y0 + r * ROW_H;
        lcd.drawFastHLine(px + 10, cy - ROW_H / 2, PW - 20, DIM);
        lcd.setFont(&fonts::FreeSansBold9pt7b);
        lcd.setTextDatum(middle_left);
        lcd.setTextColor(MUTED);
        lcd.drawString(ROW[r], px + 14, cy);
      }
    }
    for (auto &s : shown) s = "\x01";
  }

  void update(const TripComputer &tc) {
    for (int p = 0; p < 2; p++) {
      const TripData &t = tc.trip[p];
      int px = PX[p];
      draw(p * 5, big, String(t.km, t.km < 1000 ? 1 : 0), &fonts::FreeSansBold24pt7b, px + PW - 52 - big.width(),
           PY + 70 - big.height() / 2);
      float avg = tc.avgKmh(p);
      String row[4] = {hms(t.driveS), isnan(avg) ? String("--") : String((int)(avg + 0.5f)) + " km/h",
                       String((int)t.maxKmh) + " km/h", hms(t.idleS)};
      for (int r = 0; r < 4; r++)
        draw(p * 5 + 1 + r, val, row[r], &fonts::FreeSansBold12pt7b, px + PW - 14 - val.width(),
             PY + ROW_Y0 + r * ROW_H - val.height() / 2);
    }
  }

private:
  static const int Y0 = Dashboard::BAR_H;
  static const int PY = Y0 + 6, PW = 228, PH = 232;
  static constexpr int PX[2] = {8, 244};
  static const int ROW_Y0 = 112, ROW_H = 32;

  LGFX &lcd;
  LGFX_Sprite big, val;
  String shown[10];

  static void makeSprite(LGFX_Sprite &s, int w, int h) {
    s.setColorDepth(16);
    s.setPsram(false);
    if (!s.createSprite(w, h)) { s.setPsram(true); s.createSprite(w, h); }
  }

  static String hms(double sec) {
    uint32_t s = (uint32_t)sec;
    char b[16];
    snprintf(b, sizeof b, "%u:%02u:%02u", (unsigned)(s / 3600), (unsigned)(s / 60 % 60), (unsigned)(s % 60));
    return b;
  }

  void draw(int slot, LGFX_Sprite &s, const String &text, const lgfx::IFont *f, int x, int y) {
    if (text == shown[slot]) return;
    shown[slot] = text;
    s.fillSprite(theme::PANEL);
    s.setFont(f);
    s.setTextDatum(middle_right);
    s.setTextColor(theme::TEXT);
    s.drawString(text, s.width() - 1, s.height() / 2);
    s.pushSprite(x, y);
  }
};
