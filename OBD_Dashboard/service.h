// SmartGauge Libre — Copyright (c) 2026 khanatuch — code written by khanatuch & Claude (AI)
// SPDX-License-Identifier: GPL-3.0-or-later
// SERVICE page: guided throttle-body / idle relearn after cleaning, one step at a time.
//
// Read-only like the rest of the firmware: nothing here talks to the ECU. Each step
// shows live mode 01 values (coolant, RPM, throttle, speed, battery) against the
// step's conditions, and runs the timer the procedure needs:
//   T_DOWN : countdown started with START; "auto" steps chain into the next one, so
//            key-cycle and pedal sequences run as one metronome
//   T_HOLD : counts down only while every condition is met (idle timers); PAUSE
//            stops it by hand (e.g. Honda: radiator-fan time does not count)
//   T_UP   : stopwatch (Nissan: hold the pedal until the MIL goes steady)
// Sources and confidence per make: docs/03-architecture.md (SERVICE section).
// Procedures for Toyota, newer Honda, Mazda Skyactiv and the diesels are only partly
// backed by OEM text — every make carries an on-screen tag saying how sure we are.
#pragma once
#include "ui.h"

namespace service {

enum CheckKind : uint8_t { C_NONE, COOL_MIN, COOL_RANGE, RPM_RANGE, ENGINE_OFF, STOPPED, BATT_MIN, TPS_INFO };
enum TimerMode : uint8_t { T_NONE, T_DOWN, T_HOLD, T_UP };

struct Check {
  uint8_t kind;
  float a, b;
};

struct Step {
  const char *title, *l1, *l2;
  uint8_t mode;
  uint16_t sec;
  bool autoNext;
  Check c[3];
};

struct Profile {
  const char *name, *sub, *tag;
  uint16_t tagColor;
  const Step *steps;
  uint8_t n;
};

// Live values handed in by the sketch each frame (NAN = not known).
struct Live {
  float rpm, cool, tps, speed, batt;
};

#define CK_NONE {C_NONE, 0, 0}
#define CK_COOL(a) {COOL_MIN, a, 0}
#define CK_COOL2(a, b) {COOL_RANGE, a, b}
#define CK_RPM(a, b) {RPM_RANGE, a, b}
#define CK_OFF {ENGINE_OFF, 0, 0}
#define CK_STOP {STOPPED, 0, 0}
#define CK_BATT(a) {BATT_MIN, a, 0}
#define CK_TPS {TPS_INFO, 0, 0}

// Nissan Almera N17 (HR12DE): NTB12-035 / NTB12-108 + ESM "Idle Air Volume Learning without CONSULT".
static const Step NISSAN[] = {
  {"Prepare - warm engine", "Drive ~10 min, park: P/N, handbrake, wheels straight.",
   "A/C, lights, defog, radio OFF. Cooling fan not running.", T_NONE, 0, false,
   {CK_COOL2(70, 100), CK_BATT(12.9f), CK_STOP}},
  {"KEY ON (do not start)", "Engine OFF, foot OFF the pedal. Turn key to ON,",
   "then tap START. Keys and pedal now follow the timer.", T_DOWN, 2, true, {CK_OFF, CK_NONE, CK_NONE}},
  {"KEY OFF", "Turn the key OFF now and wait.", "", T_DOWN, 10, true, {CK_NONE, CK_NONE, CK_NONE}},
  {"KEY ON", "Key ON (do not start). Pedal released.", "", T_DOWN, 2, true, {CK_OFF, CK_NONE, CK_NONE}},
  {"KEY OFF", "Key OFF and wait.", "", T_DOWN, 10, true, {CK_NONE, CK_NONE, CK_NONE}},
  {"KEY ON - throttle learn", "Key ON (do not start). Throttle closed-position learn.", "", T_DOWN, 2, true,
   {CK_OFF, CK_NONE, CK_NONE}},
  {"KEY OFF - listen", "Key OFF. You may hear the throttle valve move.", "", T_DOWN, 10, true,
   {CK_NONE, CK_NONE, CK_NONE}},
  {"KEY ON - get ready", "Key ON (do not start). Pedal fully released.", "Foot ready over the pedal.", T_DOWN, 3,
   true, {CK_OFF, CK_NONE, CK_NONE}},
  {"PRESS PEDAL 5 TIMES", "Fully press and release the pedal 5 times,", "quickly, inside this window.", T_DOWN, 5,
   true, {CK_NONE, CK_NONE, CK_NONE}},
  {"WAIT - foot off", "Do not touch the pedal.", "(Some cars need ~10 s - retry if learning fails.)", T_DOWN, 7,
   true, {CK_NONE, CK_NONE, CK_NONE}},
  {"PRESS & HOLD PEDAL", "Hold it down. MIL blinks, then stays ON (~20 s).",
   "Release within 3 s of MIL going steady, then NEXT.", T_UP, 0, false, {CK_NONE, CK_NONE, CK_NONE}},
  {"START ENGINE - idle 20 s", "Pedal released. Start the engine and let it idle.",
   "Timer runs while the engine idles.", T_HOLD, 20, false, {CK_RPM(400, 1500), CK_TPS, CK_NONE}},
  {"Check idle", "Rev 2-3 times; idle should settle back smoothly.",
   "Hunts or stalls: check air leaks, repeat from step 1.", T_NONE, 0, false,
   {CK_RPM(500, 1000), CK_COOL(70), CK_NONE}},
};

// Honda L12A/L13A City/Jazz (about 2002-2013): Honda shop manual "ECM/PCM Idle Learn Procedure".
static const Step HONDA_OLD[] = {
  {"Prepare", "ECM reset by the battery or ECM fuse being removed.",
   "P/N, handbrake ON, outdoors. A/C, lights, defog OFF.", T_NONE, 0, false, {CK_STOP, CK_TPS, CK_NONE}},
  {"Hold 3,000 rpm", "No load. Hold ~3,000 rpm until the radiator fan",
   "comes on, or coolant 80-100 C. Never leave the car.", T_NONE, 0, false,
   {CK_RPM(2700, 3300), CK_COOL(80), CK_STOP}},
  {"Idle 5 minutes", "Release the pedal fully. All loads stay OFF.",
   "Radiator fan on? Tap PAUSE - fan time does not count.", T_HOLD, 300, false,
   {CK_RPM(400, 1200), CK_COOL(80), CK_TPS}},
  {"Done", "Idle learn complete. Drive normally.", "Still rough? Repeat, or check for air leaks.", T_NONE, 0,
   false, {CK_RPM(500, 1000), CK_NONE, CK_NONE}},
};

// Honda 2014+ (City GM6/GN, Jazz GK, Civic FC/FE): bulletins say HDS reset + "see service info".
static const Step HONDA_NEW[] = {
  {"Dealer reset first", "Honda: reset throttle learned values with the dealer",
   "tool (HDS) first. This guide only does the idle part.", T_NONE, 0, false, {CK_NONE, CK_NONE, CK_NONE}},
  {"Prepare & warm up", "P/N, handbrake ON, loads OFF, foot off the pedal.",
   "Idle until the radiator fan comes on.", T_NONE, 0, false, {CK_COOL(80), CK_STOP, CK_TPS}},
  {"Idle 10 minutes", "Keep idling, all loads OFF, foot off the pedal.",
   "Steps from owner forums, not Honda text.", T_HOLD, 600, false, {CK_RPM(400, 1200), CK_COOL(80), CK_TPS}},
  {"Done", "Drive normally. Rough idle or MIL: see the DTC page", "and a Honda dealer.", T_NONE, 0, false,
   {CK_RPM(500, 1000), CK_NONE, CK_NONE}},
};

// Toyota petrol (Vios, Yaris, Revo 2.7): Prius RM (policy) + 2TR-FE 4Runner bulletin (3 min warm idle).
static const Step TOYOTA[] = {
  {"Before you start", "Clean with a sprayed rag - never spray into the bore.",
   "New ECMs keep learned values: may need dealer reset.", T_NONE, 0, false, {CK_NONE, CK_NONE, CK_NONE}},
  {"Warm up fully", "P/N, handbrake ON, A/C and loads OFF.", "Idle until the engine is fully warm.", T_NONE, 0,
   false, {CK_COOL(80), CK_STOP, CK_TPS}},
  {"Idle 3 minutes", "Foot off the pedal, loads OFF.", "Toyota bulletin: idle 3+ min once fully warm.", T_HOLD,
   180, false, {CK_RPM(400, 1200), CK_COOL(80), CK_TPS}},
  {"Drive", "Drive normally with several stops; idle learning",
   "updates from driving-and-stopping history.", T_NONE, 0, false, {CK_NONE, CK_NONE, CK_NONE}},
};

// Mazda: SB 01-025/13 (2010 Mazda5, MZR) — ETB key cycles + 8 idle modes. Not verified on Skyactiv.
#define MZ_IDLE(t, l1) {t, l1, "Hold at least 1 minute, foot off the gas.", T_HOLD, 60, false, \
                        {CK_RPM(400, 1200), CK_STOP, CK_NONE}}
#define MZ_IDLE_D(t) {t, "Foot hard on the brake. Manual gearbox: skip (NEXT).", \
                      "Hold at least 1 minute.", T_HOLD, 60, false, {CK_RPM(400, 1200), CK_STOP, CK_NONE}}
static const Step MAZDA[] = {
  {"KEY ON (do not start)", "Engine OFF. Turn key ON, then tap START.",
   "Throttle learn: 3 cycles of ON 15 s / OFF 15 s.", T_DOWN, 15, true, {CK_OFF, CK_NONE, CK_NONE}},
  {"KEY OFF", "Key OFF and wait.", "", T_DOWN, 15, true, {CK_NONE, CK_NONE, CK_NONE}},
  {"KEY ON", "Key ON (do not start).", "", T_DOWN, 15, true, {CK_OFF, CK_NONE, CK_NONE}},
  {"KEY OFF", "Key OFF and wait.", "", T_DOWN, 15, true, {CK_NONE, CK_NONE, CK_NONE}},
  {"KEY ON", "Key ON (do not start).", "", T_DOWN, 15, true, {CK_OFF, CK_NONE, CK_NONE}},
  {"KEY OFF", "Key OFF and wait, then NEXT.", "", T_DOWN, 15, false, {CK_NONE, CK_NONE, CK_NONE}},
  {"Warm up until fan runs", "Start the engine. N/P, handbrake ON.", "Idle until the cooling fan starts (~90 C).",
   T_NONE, 0, false, {CK_COOL(90), CK_STOP, CK_TPS}},
  MZ_IDLE("Idle 1/8: N, no loads", "Neutral. A/C, lights, defog OFF."),
  MZ_IDLE("Idle 2/8: N + lights + defog", "Neutral. Headlights and rear defog ON."),
  MZ_IDLE("Idle 3/8: N + A/C", "Neutral. A/C ON, lights and defog OFF."),
  MZ_IDLE("Idle 4/8: N + A/C + lights", "Neutral. A/C, headlights and rear defog ON."),
  MZ_IDLE_D("Idle 5/8: D, no loads"),
  MZ_IDLE_D("Idle 6/8: D + lights + defog"),
  MZ_IDLE_D("Idle 7/8: D + A/C"),
  MZ_IDLE_D("Idle 8/8: D + A/C + lights"),
  {"Done", "Mazda5 idle spec 650-750 rpm. Loads OFF, back to P/N.",
   "Skyactiv not confirmed by Mazda text - watch the MIL.", T_NONE, 0, false,
   {CK_RPM(500, 1000), CK_NONE, CK_NONE}},
};
#undef MZ_IDLE
#undef MZ_IDLE_D

// Common-rail diesels: idle is fuel-controlled; no OEM idle relearn found.
static const Step DIESEL[] = {
  {"No idle relearn needed", "Diesel idle is set by fuel, not air: cleaning the",
   "throttle needs no relearn. Dealer may reset EGR/DPF.", T_NONE, 0, false, {CK_NONE, CK_NONE, CK_NONE}},
  {"Warm up", "P/N, handbrake ON. Idle until warm.", "", T_NONE, 0, false, {CK_COOL(75), CK_STOP, CK_NONE}},
  {"Idle 3 minutes", "Idle should stay steady, MIL off.", "Hunting idle or MIL? Check the DTC page.", T_HOLD, 180,
   false, {CK_RPM(500, 1100), CK_COOL(75), CK_NONE}},
};

#define N_OF(a) (uint8_t)(sizeof(a) / sizeof(a[0]))
static const Profile PROFILES[] = {
  {"NISSAN", "Almera N17", "OEM + ESM COPY", theme::AMBER, NISSAN, N_OF(NISSAN)},
  {"HONDA", "City/Jazz -2013", "OEM TEXT", theme::GREEN, HONDA_OLD, N_OF(HONDA_OLD)},
  {"HONDA 2014+", "City Jazz Civic", "NEEDS DEALER", theme::ORANGE, HONDA_NEW, N_OF(HONDA_NEW)},
  {"TOYOTA", "Vios Yaris Revo", "PARTIAL", theme::ORANGE, TOYOTA, N_OF(TOYOTA)},
  {"MAZDA", "Mazda 2 / 3", "UNVERIFIED", theme::AMBER, MAZDA, N_OF(MAZDA)},
  {"DIESEL", "D-Max Triton", "INFO ONLY", theme::CYAN, DIESEL, N_OF(DIESEL)},
};
static const int N_PROFILES = N_OF(PROFILES);
#undef N_OF

// Is check c met? 1 = yes, 0 = no, -1 = value unknown (or info only).
static int checkState(const Check &c, const Live &v) {
  auto has = [](float f) { return !isnan(f); };
  switch (c.kind) {
    case COOL_MIN: return has(v.cool) ? v.cool >= c.a : -1;
    case COOL_RANGE: return has(v.cool) ? v.cool >= c.a && v.cool <= c.b : -1;
    case RPM_RANGE: return has(v.rpm) ? v.rpm >= c.a && v.rpm <= c.b : -1;
    case ENGINE_OFF: return has(v.rpm) ? v.rpm < 100 : -1;
    case STOPPED: return has(v.speed) ? v.speed < 0.5f : -1;
    case BATT_MIN: return has(v.batt) ? v.batt >= c.a : -1;
  }
  return -1;
}

class Page {
public:
  explicit Page(LGFX &l) : lcd(l), chip(&l), timerS(&l), stateS(&l), okS(&l) {}

  void begin() {
    makeSprite(chip, CHIP_W, CHIP_H);
    makeSprite(timerS, 204, 56);
    makeSprite(stateS, 204, 22);
    makeSprite(okS, 248, 22);
    checkWidths();
  }

  void show() {
    lastMs = millis();  // time spent on other pages must not count
    lcd.fillRect(0, Y0, lcd.width(), lcd.height() - Y0, theme::BG);
    if (prof < 0) drawMenu();
    else drawStepStatic();
  }

  // Returns true when the tap hit one of this page's buttons.
  bool tap(int x, int y, uint32_t now) {
    if (prof < 0) {
      for (int i = 0; i < N_PROFILES; i++) {
        int bx, by;
        menuPos(i, bx, by);
        if (x >= bx && x < bx + MB_W && y >= by && y < by + MB_H) {
          prof = i;
          enterStep(0, false, now);
          return true;
        }
      }
      return false;
    }
    if (y < BTN_Y - 4) return false;
    if (x >= BACK_X - 4 && x < BACK_X + BACK_W + 3) {
      if (step == 0) {
        prof = -1;
        show();
      } else {
        enterStep(step - 1, false, now);
      }
      return true;
    }
    if (hasStart() && x >= START_X - 3 && x < START_X + START_W + 3) {
      pressStart(now);
      return true;
    }
    if (x >= NEXT_X - 3) {
      if (step + 1 >= cur().n) {
        prof = -1;
        show();
      } else {
        enterStep(step + 1, false, now);
      }
      return true;
    }
    return false;
  }

  void update(const Live &v, uint32_t now) {
    if (prof < 0) return;
    const Step &s = st();
    bool ok = allOk(s, v);
    uint32_t dt = now - lastMs;
    lastMs = now;
    if (running && !done) {
      if (s.mode == T_UP) {
        elapsed += dt;
      } else if (s.mode == T_DOWN || (s.mode == T_HOLD && ok && !paused)) {
        remain = dt >= remain ? 0 : remain - dt;
        if (remain == 0) {
          done = true;
          if (s.autoNext && step + 1 < cur().n) {
            enterStep(step + 1, true, now);
            return;
          }
        }
      }
    }
    drawChips(s, v);
    drawTimer(s, ok);
    drawOk(s, v, ok);
    drawButtons();
  }

private:
  static const int Y0 = Dashboard::BAR_H;
  static const int MB_W = 148, MB_H = 62;
  static const int CHIP_W = 148, CHIP_H = 44, CHIP_Y = Y0 + 102;
  static const int TIMER_Y = Y0 + 152;
  static const int BTN_Y = 214, BTN_H = 50;
  static const int BACK_X = 224, BACK_W = 76, START_X = 306, START_W = 80, NEXT_X = 392, NEXT_W = 80;

  LGFX &lcd;
  LGFX_Sprite chip, timerS, stateS, okS;
  int prof = -1, step = 0;
  uint32_t remain = 0, elapsed = 0, lastMs = 0;
  bool running = false, paused = false, done = false;
  String shownChip[3], shownTimer, shownState, shownOk, shownBtn;

  const Profile &cur() const { return PROFILES[prof]; }
  const Step &st() const { return cur().steps[step]; }
  bool hasStart() const { return st().mode != T_NONE; }

  static void makeSprite(LGFX_Sprite &s, int w, int h) {
    s.setColorDepth(16);
    s.setPsram(false);
    if (!s.createSprite(w, h)) { s.setPsram(true); s.createSprite(w, h); }
  }

  // Boot-time self check: log any text that would run past the 8 px screen margin.
  void checkWidths() {
    auto warn = [&](const char *s, int room) {
      if (s && lcd.textWidth(s) > room) Serial.printf("service: text too wide (%d > %d): %s\n", lcd.textWidth(s), room, s);
    };
    for (auto &p : PROFILES) {
      lcd.setFont(&fonts::FreeSans9pt7b);
      warn(p.sub, MB_W - 18);
      for (int i = 0; i < p.n; i++) {
        lcd.setFont(&fonts::FreeSans9pt7b);
        warn(p.steps[i].l1, 456);
        warn(p.steps[i].l2, 456);
        lcd.setFont(&fonts::FreeSansBold12pt7b);
        warn(p.steps[i].title, 456);
      }
    }
  }

  static bool allOk(const Step &s, const Live &v) {
    for (auto &c : s.c)
      if (c.kind != C_NONE && c.kind != TPS_INFO && checkState(c, v) != 1) return false;
    return true;
  }

  void enterStep(int i, bool chained, uint32_t now) {
    step = i;
    const Step &s = st();
    remain = s.sec * 1000UL;
    elapsed = 0;
    lastMs = now;
    done = false;
    paused = false;
    running = chained || s.mode == T_HOLD;  // idle timers run whenever their conditions hold
    show();
  }

  void pressStart(uint32_t now) {
    const Step &s = st();
    lastMs = now;
    if (s.mode == T_HOLD) {
      if (done) { remain = s.sec * 1000UL; done = false; }
      else paused = !paused;
    } else if (running && !done) {
      running = false;  // STOP
    } else {
      remain = s.sec * 1000UL;
      elapsed = 0;
      done = false;
      running = true;
    }
  }

  void menuPos(int i, int &x, int &y) const {
    x = 12 + (i % 3) * (MB_W + 6);
    y = Y0 + 34 + (i / 3) * (MB_H + 6);
  }

  void drawMenu() {
    using namespace theme;
    lcd.setFont(&fonts::FreeSansBold12pt7b);
    lcd.setTextDatum(top_left);
    lcd.setTextColor(TEXT);
    lcd.drawString("THROTTLE / IDLE RELEARN", 12, Y0 + 8);
    lcd.setFont(&fonts::FreeSans9pt7b);
    lcd.setTextDatum(top_right);
    lcd.setTextColor(MUTED);
    lcd.drawString("pick your car", 468, Y0 + 12);
    for (int i = 0; i < N_PROFILES; i++) {
      const Profile &p = PROFILES[i];
      int x, y;
      menuPos(i, x, y);
      lcd.fillRoundRect(x, y, MB_W, MB_H, 10, PANEL);
      lcd.drawRoundRect(x, y, MB_W, MB_H, 10, EDGE);
      lcd.fillRoundRect(x, y + 10, 4, MB_H - 20, 2, p.tagColor);
      lcd.setFont(&fonts::FreeSansBold12pt7b);
      if (lcd.textWidth(p.name) > MB_W - 20) lcd.setFont(&fonts::FreeSansBold9pt7b);
      lcd.setTextDatum(top_left);
      lcd.setTextColor(TEXT);
      lcd.drawString(p.name, x + 12, y + 9);
      lcd.setFont(&fonts::FreeSans9pt7b);
      lcd.setTextColor(UNIT);
      lcd.drawString(p.sub, x + 12, y + 37);
    }
    int by = Y0 + 34 + 2 * (MB_H + 6) + 2;
    lcd.drawRoundRect(12, by, 456, lcd.height() - 8 - by, 8, AMBER);
    lcd.setFont(&fonts::FreeSansBold9pt7b);
    lcd.setTextColor(AMBER);
    lcd.setTextDatum(top_left);
    lcd.drawString("GUIDE ONLY - your own risk. Never writes to ECU.", 20, by + 5);
    lcd.setFont(&fonts::FreeSans9pt7b);
    lcd.setTextColor(UNIT);
    lcd.drawString("Engine on: P/N + handbrake, outdoors, stay with it.", 20, by + 24);
    lcd.drawString("Sources vary - see the confidence tag per make.", 20, by + 42);
  }

  void drawStepStatic() {
    using namespace theme;
    const Profile &p = cur();
    const Step &s = st();
    lcd.setTextDatum(top_left);
    lcd.setFont(&fonts::FreeSansBold9pt7b);
    lcd.setTextColor(CYAN);
    lcd.drawString(p.name, 12, Y0 + 8);
    int w = lcd.textWidth(p.name);
    lcd.setTextColor(MUTED);
    lcd.drawString("  STEP " + String(step + 1) + "/" + String(p.n), 12 + w, Y0 + 8);
    // confidence tag
    int tw = lcd.textWidth(p.tag) + 16;
    lcd.fillRoundRect(468 - tw, Y0 + 4, tw, 22, 8, p.tagColor);
    lcd.setTextDatum(middle_center);
    lcd.setTextColor(BG);
    lcd.drawString(p.tag, 468 - tw / 2, Y0 + 15);

    lcd.setTextDatum(top_left);
    lcd.setFont(&fonts::FreeSansBold12pt7b);
    lcd.setTextColor(TEXT);
    lcd.drawString(s.title, 12, Y0 + 32);
    lcd.setFont(&fonts::FreeSans9pt7b);
    lcd.setTextColor(UNIT);
    lcd.drawString(s.l1, 12, Y0 + 60);
    lcd.drawString(s.l2, 12, Y0 + 80);
    for (auto &c : shownChip) c = "\x01";
    shownTimer = shownState = shownOk = shownBtn = "\x01";
  }

  static String fmt(float v, int dec) { return isnan(v) ? String("--") : String(v, (unsigned int)dec); }

  void drawChips(const Step &s, const Live &v) {
    using namespace theme;
    for (int i = 0; i < 3; i++) {
      const Check &c = s.c[i];
      if (c.kind == C_NONE) continue;
      String label, val;
      bool deg = false;
      switch (c.kind) {
        case COOL_MIN: label = "COOLANT " + String((int)c.a) + "+"; val = fmt(v.cool, 0); deg = true; break;
        case COOL_RANGE: label = "COOL " + String((int)c.a) + "-" + String((int)c.b); val = fmt(v.cool, 0); deg = true; break;
        case RPM_RANGE: label = "RPM " + String((int)c.a) + "-" + String((int)c.b); val = fmt(v.rpm, 0) + " rpm"; break;
        case ENGINE_OFF: label = "ENGINE OFF"; val = fmt(v.rpm, 0) + " rpm"; break;
        case STOPPED: label = "STOPPED"; val = fmt(v.speed, 0) + " km/h"; break;
        case BATT_MIN: label = "BATT " + String(c.a, 1) + "+ V"; val = fmt(v.batt, 1) + " V"; break;
        case TPS_INFO: label = "THROTTLE"; val = fmt(v.tps, 1) + " %"; break;
      }
      int state = c.kind == TPS_INFO ? 2 : checkState(c, v);
      String key = label + val + state;
      if (key == shownChip[i]) continue;
      shownChip[i] = key;
      uint16_t col = state == 1 ? GREEN : state == 0 ? AMBER : state == 2 ? CYAN : MUTED;
      chip.fillSprite(BG);
      chip.fillRoundRect(0, 0, CHIP_W, CHIP_H, 8, PANEL);
      chip.drawRoundRect(0, 0, CHIP_W, CHIP_H, 8, state == 1 ? GREEN : state == 0 ? AMBER : EDGE);
      chip.setFont(&fonts::FreeSans9pt7b);
      chip.setTextDatum(top_left);
      chip.setTextColor(MUTED);
      chip.drawString(label, 8, 4);
      chip.setFont(&fonts::FreeSansBold12pt7b);
      chip.setTextColor(col);
      chip.drawString(val, 8, 21);
      if (deg && !isnan(v.cool)) {  // fonts have no degree sign: draw a ring + C
        int x = 8 + chip.textWidth(val) + 4;
        chip.drawCircle(x + 3, 25, 3, col);
        chip.drawCircle(x + 3, 25, 2, col);
        chip.drawString("C", x + 9, 21);
      }
      if (state == 1) {  // tick
        chip.drawLine(CHIP_W - 22, 30, CHIP_W - 18, 35, GREEN);
        chip.drawLine(CHIP_W - 22, 29, CHIP_W - 18, 34, GREEN);
        chip.drawLine(CHIP_W - 18, 35, CHIP_W - 9, 23, GREEN);
        chip.drawLine(CHIP_W - 18, 34, CHIP_W - 9, 22, GREEN);
      } else if (state == 0) {  // cross
        chip.drawLine(CHIP_W - 21, 24, CHIP_W - 11, 34, AMBER);
        chip.drawLine(CHIP_W - 20, 24, CHIP_W - 10, 34, AMBER);
        chip.drawLine(CHIP_W - 11, 24, CHIP_W - 21, 34, AMBER);
        chip.drawLine(CHIP_W - 10, 24, CHIP_W - 20, 34, AMBER);
      }
      chip.pushSprite(12 + i * (CHIP_W + 6), CHIP_Y);
    }
  }

  static String clock(uint32_t ms, bool up) {
    uint32_t s = up ? ms / 1000 : (ms + 999) / 1000;  // countdowns round up: "1" until it hits 0
    if (s < 60) return String(s);
    char b[8];
    snprintf(b, sizeof b, "%u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
    return b;
  }

  void drawTimer(const Step &s, bool ok) {
    using namespace theme;
    String t, state;
    uint16_t tc = TEXT, sc = MUTED;
    if (s.mode == T_NONE) {
      t = "";
      state = "";
    } else if (s.mode == T_UP) {
      t = clock(elapsed, true);
      tc = running ? AMBER : MUTED;
      state = running ? "holding..." : "tap START when you press";
    } else {
      t = clock(remain, false);
      if (done) { tc = GREEN; state = "DONE - tap NEXT"; sc = GREEN; }
      else if (s.mode == T_HOLD) {
        if (paused) { tc = MUTED; state = "PAUSED"; sc = AMBER; }
        else if (!ok) { tc = MUTED; state = "waiting for conditions"; sc = AMBER; }
        else { tc = TEXT; state = "counting"; sc = GREEN; }
      } else if (!running) { tc = MUTED; state = "tap START"; }
      else { tc = remain <= 3000 ? AMBER : TEXT; state = s.autoNext ? "next step follows" : "running"; }
    }
    String key = t + tc;
    if (key != shownTimer) {
      shownTimer = key;
      timerS.fillSprite(BG);
      timerS.setFont(&fonts::Font7);
      timerS.setTextDatum(middle_left);
      timerS.setTextColor(tc);
      timerS.drawString(t, 0, timerS.height() / 2);
      timerS.pushSprite(12, TIMER_Y);
    }
    key = state + sc;
    if (key != shownState) {
      shownState = key;
      stateS.fillSprite(BG);
      stateS.setFont(&fonts::FreeSansBold9pt7b);
      stateS.setTextDatum(middle_left);
      stateS.setTextColor(sc);
      stateS.drawString(state, 0, stateS.height() / 2);
      stateS.pushSprite(12, lcd.height() - 8 - stateS.height());
    }
  }

  void drawOk(const Step &s, const Live &v, bool ok) {
    using namespace theme;
    bool any = false;
    for (auto &c : s.c) any |= c.kind != C_NONE && c.kind != TPS_INFO;
    String msg = !any ? String("") : ok ? String("CONDITIONS OK") : String("CHECK CONDITIONS");
    uint16_t col = ok ? GREEN : AMBER;
    if (msg + col == shownOk) return;
    shownOk = msg + col;
    okS.fillSprite(BG);
    okS.setFont(&fonts::FreeSansBold9pt7b);
    okS.setTextDatum(middle_right);
    okS.setTextColor(col);
    okS.drawString(msg, okS.width() - 1, okS.height() / 2);
    okS.pushSprite(472 - okS.width(), BTN_Y - 4 - okS.height());
  }

  void button(int x, int w, const char *label, bool primary, bool enabled) {
    using namespace theme;
    lcd.fillRoundRect(x, BTN_Y, w, BTN_H, 10, !enabled ? BG : primary ? CYAN : PANEL);
    if (!enabled) return;
    lcd.drawRoundRect(x, BTN_Y, w, BTN_H, 10, CYAN);
    lcd.setFont(&fonts::FreeSansBold9pt7b);
    lcd.setTextDatum(middle_center);
    lcd.setTextColor(primary ? BG : CYAN);
    lcd.drawString(label, x + w / 2, BTN_Y + BTN_H / 2);
  }

  void drawButtons() {
    const Step &s = st();
    const char *start = "START";
    if (s.mode == T_HOLD) start = done ? "AGAIN" : paused ? "RESUME" : "PAUSE";
    else if (running && !done) start = "STOP";
    else if (done) start = "AGAIN";
    const char *next = step + 1 >= cur().n ? "DONE" : "NEXT";
    String key = String(start) + next + hasStart() + done;
    if (key == shownBtn) return;
    shownBtn = key;
    bool startPrimary = hasStart() && !done && !running;
    button(BACK_X, BACK_W, step == 0 ? "MENU" : "BACK", false, true);
    button(START_X, START_W, start, startPrimary, hasStart());
    button(NEXT_X, NEXT_W, next, !startPrimary, true);
  }
};

}  // namespace service
