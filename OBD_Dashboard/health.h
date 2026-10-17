// SmartGauge Libre — Copyright (c) 2026 khanatuch — code written by khanatuch & Claude (AI)
// SPDX-License-Identifier: GPL-3.0-or-later
// HEALTH page: early-warning indicators built only from standard mode 01 data.
//
// Each drive (ECU answering + engine turning, at least MIN_DRIVE_S long) leaves a
// small summary: average charging voltage, hot long-term fuel trim, cold-start
// warm-up time and how much the rear O2 sensor copies the front one. Summaries go
// into a 32-entry ring in NVS (namespace "health", one ~400-byte blob written once
// per drive, so flash wear is negligible). The first BASE_N valid values of each
// metric are kept as that car's baseline until the user long-presses the page
// (e.g. after replacing a part).
//
// A check's value = median of the current drive (when it has enough data) and the
// latest stored drives, 3 values in all. Status = worst of a fixed limit and the
// drift from baseline. These are indicators, not a time-to-failure prediction.
// Runs on the UI core: tick() samples pidValues once a second; obdTask only keeps
// the needed PIDs polled in the background.
#pragma once
#include <Preferences.h>
#include <algorithm>
#include "ui.h"

namespace health {
enum Status : uint8_t { NA, LEARN, OK, WARN, BAD };
enum Metric { CHARGE, TRIM, WARMUP, CAT, METRICS };
static const int CHECKS = 5;  // the four metrics + trouble codes

static uint16_t statusColor(Status s) {
  using namespace theme;
  switch (s) {
    case OK: return GREEN;
    case WARN: return AMBER;
    case BAD: return RED;
    case LEARN: return CYAN;
    default: return MUTED;
  }
}

static Status worse(Status a, Status b) { return a > b ? a : b; }

static const char *statusWord(Status s) {
  switch (s) {
    case OK: return "OK";
    case WARN: return "CHECK";
    case BAD: return "FAULT";
    case LEARN: return "LEARNING";
    default: return "N/A";
  }
}

// One drive, 12 bytes. NONE marks a metric the drive could not measure.
struct Drive {
  uint16_t minutes;
  int16_t v[METRICS];  // CHARGE mV, TRIM %x10, WARMUP s (WARM_NEVER = never reached 80 °C), CAT ratio x1000
  uint16_t spare;
};
static const int16_t NONE = INT16_MIN;
static const int16_t WARM_NEVER = INT16_MAX;

struct Store {
  uint32_t magic;
  uint8_t head, count;  // ring: next slot, used slots
  uint8_t baseN[METRICS];
  int16_t base[METRICS][8];
  Drive ring[32];
};

// Per-check result shown on the page.
struct Check {
  Status st;
  String value;   // e.g. "13.9 V"
  String detail;  // e.g. "avg while running, last 3 drives"
};

// Live inputs, sampled from pidValues once a second.
struct Inputs {
  float rpm, batt, coolant, ltft1, ltft2, o2front, o2rear;
  bool ecu;        // ECU answering
  bool frontNarrow;  // front sensor is a switching (narrowband) O2 sensor
};

class Monitor {
public:
  static const int RING = 32, BASE_N = 8, RECENT = 3;
  static const uint32_t MIN_DRIVE_S = 300, END_GAP_S = 10;

  explicit Monitor(bool persist) : persist(persist) {}

  void begin() {
    clear();
    if (!persist) return;
    Preferences p;
    if (!p.begin("health", true)) return;
    Store s;
    if (p.getBytes("s", &s, sizeof s) == sizeof s && s.magic == MAGIC) st = s;
    p.end();
  }

  // Fake history for demo mode: a healthy baseline, then a weak alternator,
  // a lean trim and a slow thermostat, so every colour shows up.
  void seedDemo() {
    clear();
    for (int i = 0; i < 12; i++) {
      bool late = i >= 9;
      Drive d = {25, {(int16_t)(late ? 13050 + i * 10 : 14050 - i * 5), (int16_t)(late ? 215 : 25 + i * 3),
                      (int16_t)(late ? 690 : 400 + i * 5), (int16_t)(late ? 260 : 140 + i * 4)},
                 0};
      add(d);
    }
  }

  void resetBaseline() {
    for (int m = 0; m < METRICS; m++) st.baseN[m] = 0;
    // re-learn from the drives already in the ring, newest first, so the new
    // baseline reflects the car as it is now
    for (int k = 0; k < st.count && k < BASE_N; k++) learn(st.ring[slot(k)]);
    save();
    version++;
  }

  int drives() const { return st.count; }
  int baseCount() const {
    int n = BASE_N;
    for (int m = 0; m < METRICS; m++) if (m != CAT) n = min(n, (int)st.baseN[m]);
    return n;
  }
  bool driving() const { return inDrive; }
  uint32_t version = 0;  // bumps when a drive is stored or the baseline reset

  // Call about once a second (UI core).
  void tick(const Inputs &in, uint32_t now) {
    bool turning = in.ecu && !isnan(in.rpm) && in.rpm > 300;
    if (!inDrive) {
      if (!turning) return;
      startDrive(now);
    }
    if (turning) lastTurning = now;
    else if (now - lastTurning >= END_GAP_S * 1000) { endDrive(now); return; }
    if (!turning) return;
    runS++;
    sample(in);
  }

  // Ignition off / link lost / demo toggled: close the drive now.
  void endDrive(uint32_t now) {
    if (!inDrive) return;
    inDrive = false;
    Drive d = current();
    d.minutes = (now - driveStart) / 60000;
    if (runS >= MIN_DRIVE_S) {
      add(d);
      save();
      version++;
    }
  }

  // Thermostat stuck open: the warm-up never got there in a long drive.
  bool stuckOpenNow() const { return inDrive && coldStart && warmS < 0 && runS > 20 * 60 && !isnan(lastCool) && lastCool < 75; }

  Check check(Metric m) const {
    int16_t vals[RECENT];
    int n = recent(m, vals);
    Check c{NA, "--", ""};
    float base = baseline(m);
    switch (m) {
      case CHARGE: {
        if (!n) { c.detail = "Needs a 5 min drive"; return c; }
        float v = median(vals, n) / 1000.0f;
        c.value = String(v, 1) + " V";
        c.st = v < 12.8f || v > 15.0f ? BAD : v < 13.2f || v > 14.8f ? WARN : OK;
        if (v < 13.2f) c.detail = "Low: alternator or belt";
        else if (v > 14.8f) c.detail = "High: voltage regulator";
        else c.detail = "Average while running";
        if (c.st == OK && !isnan(base) && base / 1000.0f - v >= 0.3f) {
          c.st = worse(c.st, WARN);
          c.detail = "Dropped " + String(base / 1000.0f - v, 1) + " V from baseline";
        }
        break;
      }
      case TRIM: {
        if (!n) { c.detail = "Needs a warm 5 min drive"; return c; }
        float v = median(vals, n) / 10.0f;
        c.value = (v >= 0 ? "+" : "") + String(v, 0) + " %";
        float a = fabsf(v);
        c.st = a > 20 ? BAD : a > 10 ? WARN : OK;
        c.detail = a > 10 ? (v > 0 ? "Lean: vacuum leak, MAF, fuel" : "Rich: injectors, O2, pressure")
                          : "Long-term trim, engine warm";
        if (c.st == OK && !isnan(base) && fabsf(v - base / 10.0f) >= 5) {
          c.st = worse(c.st, WARN);
          c.detail = String(v > base / 10.0f ? "Leaner" : "Richer") + " by " + String(fabsf(v - base / 10.0f), 0) + " % vs baseline";
        }
        break;
      }
      case WARMUP: {
        if (stuckOpenNow()) {
          c = {BAD, "> 20 min", "Not warming: stuck open?"};
          return c;
        }
        if (!n) { c.detail = "Needs a cold start"; return c; }
        if (vals[0] == WARM_NEVER) {  // newest cold start never reached 80 °C
          c = {BAD, "never", "Did not reach 80 C: stuck open?"};
          return c;
        }
        float v = median(vals, n);
        c.value = String(v / 60.0f, 1) + " min";
        c.st = v > 15 * 60 ? WARN : OK;
        c.detail = "Cold start to 80 C";
        if (!isnan(base) && v >= base * 1.5f) {
          c.st = worse(c.st, WARN);
          c.detail = "+" + String((int)((v / base - 1) * 100)) + "% slower than baseline";
        }
        break;
      }
      case CAT: {
        if (!frontNarrow) { c.detail = "Wideband front sensor: n/a"; return c; }
        if (!n) { c.detail = "Needs steady warm driving"; return c; }
        float v = median(vals, n) / 1000.0f;
        c.value = String((int)(v * 100 + 0.5f)) + " %";
        c.st = v >= 0.8f ? BAD : v >= 0.5f ? WARN : OK;
        c.detail = v >= 0.5f ? "Rear O2 copies front: cat wear" : "Rear O2 vs front swing";
        if (c.st == OK && !isnan(base) && v - base / 1000.0f >= 0.2f) {
          c.st = worse(c.st, WARN);
          c.detail = "Rear O2 swing up from baseline";
        }
        break;
      }
      default: break;
    }
    // trend needs a baseline; until then show the plain limit result as learning
    if (isnan(base) && c.st == OK) {
      c.st = LEARN;
      c.detail += "  (" + String(st.baseN[m]) + "/" + String(BASE_N) + ")";
    }
    return c;
  }

  // Worst status of the four metrics (trouble codes are judged separately).
  Status worst() const {
    Status w = NA;
    for (int m = 0; m < METRICS; m++) w = worse(w, check((Metric)m).st);
    return w;
  }

  bool frontNarrow = true;

private:
  static const uint32_t MAGIC = 0x484C5431;  // "HLT1"
  static const uint32_t CAT_WINDOW_S = 30;
  bool persist;
  Store st;

  // current drive
  bool inDrive = false, coldStart = false, coolKnown = false;
  uint32_t driveStart = 0, lastTurning = 0, runS = 0;
  int32_t warmS = -1;
  float lastCool = NAN;
  double battSum = 0, trimSum[2] = {0, 0};
  uint32_t battN = 0, trimN[2] = {0, 0};
  // catalyst window
  uint32_t winS = 0;
  float fMin, fMax, rMin, rMax;
  int fN = 0, rN = 0;
  double catSum = 0;
  uint32_t catN = 0;

  void clear() {
    memset(&st, 0, sizeof st);
    st.magic = MAGIC;
    version++;
  }

  int slot(int k) const { return (st.head - 1 - k + RING * 2) % RING; }  // k = 0 newest

  void learn(const Drive &d) {
    for (int m = 0; m < METRICS; m++)
      if (st.baseN[m] < BASE_N && d.v[m] != NONE && d.v[m] != WARM_NEVER) st.base[m][st.baseN[m]++] = d.v[m];
  }

  void add(const Drive &d) {
    st.ring[st.head] = d;
    st.head = (st.head + 1) % RING;
    if (st.count < RING) st.count++;
    learn(d);
  }

  void save() {
    if (!persist) return;
    Preferences p;
    p.begin("health", false);
    p.putBytes("s", &st, sizeof st);
    p.end();
  }

  void startDrive(uint32_t now) {
    inDrive = true;
    driveStart = lastTurning = now;
    runS = 0;
    coolKnown = coldStart = false;
    warmS = -1;
    battSum = trimSum[0] = trimSum[1] = 0;
    battN = trimN[0] = trimN[1] = 0;
    winS = 0;
    fN = rN = 0;
    catSum = 0;
    catN = 0;
  }

  void sample(const Inputs &in) {
    frontNarrow = in.frontNarrow;
    lastCool = in.coolant;
    if (!coolKnown && !isnan(in.coolant)) {
      coolKnown = true;
      coldStart = in.coolant < 50;
    }
    if (coldStart && warmS < 0 && !isnan(in.coolant) && in.coolant >= 80) warmS = runS;
    if (runS > 30 && in.rpm > 500 && !isnan(in.batt)) { battSum += in.batt; battN++; }
    bool hot = !isnan(in.coolant) && in.coolant > 75;
    if (hot) {
      if (!isnan(in.ltft1)) { trimSum[0] += in.ltft1; trimN[0]++; }
      if (!isnan(in.ltft2)) { trimSum[1] += in.ltft2; trimN[1]++; }
    }
    // catalyst: in 30 s windows of steady warm driving compare the swing of both sensors
    bool steady = hot && in.rpm > 1200 && in.rpm < 3500 && !isnan(in.o2front) && !isnan(in.o2rear);
    if (!steady) { winS = 0; fN = rN = 0; return; }
    if (!fN) { fMin = fMax = in.o2front; rMin = rMax = in.o2rear; }
    fMin = min(fMin, in.o2front); fMax = max(fMax, in.o2front);
    rMin = min(rMin, in.o2rear); rMax = max(rMax, in.o2rear);
    fN++; rN++;
    if (++winS >= CAT_WINDOW_S) {
      if (fMax - fMin > 0.4f) {  // front is switching, so the window means something
        catSum += min(1.0f, (rMax - rMin) / (fMax - fMin));
        catN++;
      }
      winS = 0;
      fN = rN = 0;
    }
  }

  // Summary of the drive so far; NONE where there is not enough data yet.
  Drive current() const {
    Drive d = {0, {NONE, NONE, NONE, NONE}, 0};
    if (battN >= 60) d.v[CHARGE] = (int16_t)(battSum / battN * 1000);
    int b = -1;
    for (int k = 0; k < 2; k++)
      if (trimN[k] >= 60 && (b < 0 || fabs(trimSum[k] / trimN[k]) > fabs(trimSum[b] / trimN[b]))) b = k;
    if (b >= 0) d.v[TRIM] = (int16_t)(trimSum[b] / trimN[b] * 10);
    if (coldStart) {
      if (warmS >= 0) d.v[WARMUP] = (int16_t)min<int32_t>(warmS, 30000);
      else if (runS > 20 * 60) d.v[WARMUP] = WARM_NEVER;
    }
    if (catN >= 3) d.v[CAT] = (int16_t)(catSum / catN * 1000);
    return d;
  }

  // Up to RECENT values, newest first: the running drive, then stored drives.
  int recent(Metric m, int16_t *out) const {
    int n = 0;
    if (inDrive) {
      int16_t v = current().v[m];
      if (v != NONE) out[n++] = v;
    }
    for (int k = 0; k < st.count && n < RECENT; k++) {
      int16_t v = st.ring[slot(k)].v[m];
      if (v != NONE) out[n++] = v;
    }
    return n;
  }

  float baseline(Metric m) const {
    if (st.baseN[m] < BASE_N) return NAN;
    return median(st.base[m], BASE_N);
  }

  static float median(const int16_t *v, int n) {
    int16_t s[BASE_N];
    for (int i = 0; i < n; i++) s[i] = v[i];
    std::sort(s, s + n);
    return n % 2 ? s[n / 2] : (s[n / 2 - 1] + s[n / 2]) / 2.0f;
  }
};

// Five rows (charging, fuel trim, thermostat, catalyst, trouble codes) + a footer.
// Each row is drawn into one sprite and pushed only when its text changes.
class Page {
public:
  explicit Page(LGFX &l) : lcd(l), row(&l) {}

  void begin() {
    row.setColorDepth(16);
    row.setPsram(false);
    if (!row.createSprite(RW, RH)) { row.setPsram(true); row.createSprite(RW, RH); }
  }

  // Long-press anywhere below the status bar re-learns the baseline.
  static bool isResetArea(int, int y) { return y > Dashboard::BAR_H + 6; }

  void show() {
    lcd.fillRect(0, Y0, lcd.width(), lcd.height() - Y0, theme::BG);
    for (auto &s : shown) s = "\x01";
  }

  void update(const Monitor &mon, const DtcReport &dtc, bool connected) {
    static const char *const NAME[CHECKS] = {"CHARGING", "FUEL TRIM", "THERMOSTAT", "CATALYST", "TROUBLE CODES"};
    static const Metric ORDER[METRICS] = {CHARGE, TRIM, WARMUP, CAT};
    for (int i = 0; i < CHECKS; i++) {
      Check c = i < METRICS ? mon.check(ORDER[i]) : codes(dtc, connected);
      drawRow(i, NAME[i], c);
    }
    String foot = String(mon.drives()) + " drives  base " + String(mon.baseCount()) + "/" + String(Monitor::BASE_N);
    if (foot != shown[CHECKS]) {
      shown[CHECKS] = foot;
      int y = Y0 + 4 + CHECKS * (RH + GAP) + 4;
      lcd.fillRect(0, y - 2, lcd.width(), lcd.height() - y + 2, theme::BG);
      lcd.setFont(&fonts::FreeSans9pt7b);
      lcd.setTextColor(theme::MUTED);
      lcd.setTextDatum(top_left);
      lcd.drawString("Indicators only - not a failure date", X0 + 2, y);
      lcd.setTextDatum(top_right);
      lcd.drawString(foot, X0 + RW - 2, y);
    }
  }

private:
  static const int Y0 = Dashboard::BAR_H;
  static const int X0 = 8, RW = 464, RH = 40, GAP = 3;

  LGFX &lcd;
  LGFX_Sprite row;
  String shown[CHECKS + 1];

  static Check codes(const DtcReport &r, bool connected) {
    if (!r.valid) return {NA, "--", connected ? "Open the DTC page to read" : "Connect to read codes"};
    int s = r.stored.size(), p = r.pending.size();
    if (r.mil || s) return {BAD, String(s) + " / " + String(p), r.mil ? "Check engine lamp ON" : "Stored codes: see DTC page"};
    if (p) return {WARN, "0 / " + String(p), "Pending: fault seen, not confirmed"};
    return {OK, "0 / 0", "Stored / pending"};
  }

  void drawRow(int i, const char *name, const Check &c) {
    String key = String((int)c.st) + c.value + "|" + c.detail;
    if (key == shown[i]) return;
    shown[i] = key;
    using namespace theme;
    uint16_t col = statusColor(c.st);
    row.fillSprite(BG);
    row.fillRoundRect(0, 0, RW, RH, 8, PANEL);
    row.drawRoundRect(0, 0, RW, RH, 8, c.st >= WARN ? col : EDGE);
    row.fillRoundRect(0, 8, 4, RH - 16, 2, col);
    row.fillCircle(22, RH / 2, 7, col);
    row.setFont(&fonts::FreeSansBold9pt7b);
    row.setTextDatum(middle_left);
    row.setTextColor(TEXT);
    row.drawString(name, 40, 12);
    row.setFont(&fonts::FreeSans9pt7b);
    row.setTextColor(c.st >= WARN ? col : MUTED);
    row.drawString(c.detail, 40, 29);
    row.setFont(&fonts::FreeSansBold12pt7b);
    row.setTextDatum(middle_right);
    row.setTextColor(c.st == NA ? EDGE : TEXT);
    row.drawString(c.value, RW - 104, RH / 2);
    // status pill
    int px = RW - 96;
    if (c.st >= WARN) row.fillRoundRect(px, 9, 86, RH - 18, 9, col);
    else row.drawRoundRect(px, 9, 86, RH - 18, 9, col);
    row.setFont(&fonts::FreeSansBold9pt7b);
    row.setTextDatum(middle_center);
    row.setTextColor(c.st >= WARN ? BG : col);
    row.drawString(statusWord(c.st), px + 43, RH / 2);
    row.pushSprite(X0, Y0 + 4 + i * (RH + GAP));
  }
};
}  // namespace health
