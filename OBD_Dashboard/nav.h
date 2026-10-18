// SmartGauge Libre — Copyright (c) 2026 khanatuch — code written by khanatuch & Claude (AI)
// SPDX-License-Identifier: GPL-3.0-or-later
// Turn-by-turn hints from a phone. The gauge stays a BLE client of the ELM327
// and is at the same time a small GATT server the phone writes to (NimBLE in
// core 3.3.x allows 3 links). The phone side (a companion app that reads the
// Google Maps navigation notification, or tools/nav_send.py for testing) writes:
//
//   NAV  characteristic, UTF-8 text:  N|<turn>|<metres>|<eta>|<street>   (update)
//                                     X                                  (navigation ended)
//        <turn>: straight left right slight-left slight-right sharp-left sharp-right
//                uturn roundabout arrive icon, or empty = guess from <street> text
//                ("Turn left onto ..." - also the path for iPhone ANCS text)
//   ICON characteristic: 48x48 1-bit bitmap (288 bytes, rows MSB first) in chunks,
//        each write = [offset lo][offset hi][bytes...]; used when <turn> is "icon".
//
// The screen has no Thai font: non-ASCII characters are dropped from the street name.
// BLE callbacks (NimBLE host task) only copy into `g` under a spinlock; drawing stays on core 1.
#pragma once
#include <BLEDevice.h>
#include <BLEServer.h>
#include "ui.h"

namespace nav {

static const char *const SERVICE_UUID = "5a4e0001-8f6b-4c1e-9a43-6d0c5e7a1b20";
static const char *const NAV_UUID = "5a4e0002-8f6b-4c1e-9a43-6d0c5e7a1b20";
static const char *const ICON_UUID = "5a4e0003-8f6b-4c1e-9a43-6d0c5e7a1b20";
static const int ICON_W = 48, ICON_BYTES = ICON_W * ICON_W / 8;
static const uint32_t STALE_MS = 30000;  // no update for this long = phone stopped navigating

enum Turn : uint8_t { NONE, STRAIGHT, LEFT, RIGHT, SLIGHT_LEFT, SLIGHT_RIGHT, SHARP_LEFT, SHARP_RIGHT, UTURN,
                      ROUNDABOUT, ARRIVE, ICON };

struct State {
  bool active = false;
  Turn turn = NONE;
  int32_t metres = -1;  // -1 = unknown
  char eta[12] = "";
  char street[48] = "";
  uint32_t rxMs = 0;      // when this update arrived (distance is counted down from here)
  uint32_t iconVersion = 0;
  bool hasIcon = false;
  uint8_t icon[ICON_BYTES] = {};
};

static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static State g;                         // guarded by mux
static volatile bool phoneLinked = false;
static volatile uint32_t lastPhoneRx = 0;  // last BLE write (demo yields to a real phone)

static Turn turnFromToken(const String &t) {
  static const char *const NAMES[] = {"", "straight", "left", "right", "slight-left", "slight-right",
                                      "sharp-left", "sharp-right", "uturn", "roundabout", "arrive", "icon"};
  for (int i = 0; i < (int)(sizeof NAMES / sizeof NAMES[0]); i++)
    if (t.equalsIgnoreCase(NAMES[i])) return (Turn)i;
  return NONE;
}

// Best guess from an instruction line ("Turn left onto Rama 4", "Keep right", "At the roundabout ...").
static Turn turnFromText(String s) {
  s.toLowerCase();
  if (s.indexOf("u-turn") >= 0 || s.indexOf("uturn") >= 0 || s.indexOf("make a u") >= 0) return UTURN;
  if (s.indexOf("roundabout") >= 0) return ROUNDABOUT;
  if (s.indexOf("arriv") >= 0 || s.indexOf("destination") >= 0) return ARRIVE;
  bool l = s.indexOf("left") >= 0, r = s.indexOf("right") >= 0;
  if (l != r) {
    if (s.indexOf("sharp") >= 0) return l ? SHARP_LEFT : SHARP_RIGHT;
    if (s.indexOf("slight") >= 0 || s.indexOf("keep") >= 0 || s.indexOf("exit") >= 0 || s.indexOf("ramp") >= 0)
      return l ? SLIGHT_LEFT : SLIGHT_RIGHT;
    return l ? LEFT : RIGHT;
  }
  if (s.indexOf("straight") >= 0 || s.indexOf("continue") >= 0 || s.indexOf("head") >= 0) return STRAIGHT;
  return NONE;
}

static void copyAscii(char *dst, size_t n, const String &src) {
  size_t k = 0;
  for (size_t i = 0; i < src.length() && k + 1 < n; i++) {
    unsigned char c = src[i];
    if (c >= 32 && c < 127) dst[k++] = c;
  }
  while (k && dst[k - 1] == ' ') k--;
  dst[k] = 0;
}

static String field(const String &s, int n) {
  int from = 0;
  for (int i = 0; i < n; i++) {
    from = s.indexOf('|', from);
    if (from < 0) return "";
    from++;
  }
  int to = s.indexOf('|', from);
  return to < 0 ? s.substring(from) : s.substring(from, to);
}

// Parses one NAV write. Also used by the serial 'V' command and the demo route.
static void apply(const String &msg, uint32_t now) {
  if (msg.startsWith("X")) {
    portENTER_CRITICAL(&mux);
    g.active = false;
    portEXIT_CRITICAL(&mux);
    return;
  }
  if (!msg.startsWith("N|")) return;
  String tok = field(msg, 1), dist = field(msg, 2), eta = field(msg, 3), street = field(msg, 4);
  Turn t = tok.length() ? turnFromToken(tok) : turnFromText(street);
  // "Turn left onto X" -> keep just the road name when there is one
  int onto = street.indexOf(" onto ");
  if (onto < 0) onto = street.indexOf(" on ");
  if (onto >= 0 && !tok.length()) street = street.substring(street.indexOf(' ', onto + 1) + 1);
  portENTER_CRITICAL(&mux);
  g.active = true;
  g.turn = t;
  g.metres = dist.length() ? dist.toInt() : -1;
  copyAscii(g.eta, sizeof g.eta, eta);
  copyAscii(g.street, sizeof g.street, street);
  g.rxMs = now;
  portEXIT_CRITICAL(&mux);
}

static State snapshot() {
  portENTER_CRITICAL(&mux);
  State s = g;
  portEXIT_CRITICAL(&mux);
  return s;
}

class NavWriteCb : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) override {
    lastPhoneRx = millis();
    apply(String(c->getValue().c_str()), millis());
  }
};

class IconWriteCb : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) override {
    String v = c->getValue();
    if (v.length() < 3) return;
    int off = (uint8_t)v[0] | ((uint8_t)v[1] << 8);
    int n = min((int)v.length() - 2, ICON_BYTES - off);
    if (off < 0 || n <= 0) return;
    lastPhoneRx = millis();
    portENTER_CRITICAL(&mux);
    memcpy(g.icon + off, v.c_str() + 2, n);
    if (off + n == ICON_BYTES) {
      g.hasIcon = true;
      g.iconVersion++;
    }
    portEXIT_CRITICAL(&mux);
  }
};

class ServerCb : public BLEServerCallbacks {
  void onConnect(BLEServer *) override {
    phoneLinked = true;  // no Serial here: this task is not paused during a screenshot
  }
  void onDisconnect(BLEServer *) override {
    phoneLinked = false;
    BLEDevice::startAdvertising();
  }
};

// Call after BLEDevice::init() (elm.begin()).
static void beginServer() {
  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(new ServerCb());
  BLEService *svc = server->createService(SERVICE_UUID);
  BLECharacteristic *nc = svc->createCharacteristic(
      NAV_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  nc->setCallbacks(new NavWriteCb());
  BLECharacteristic *ic = svc->createCharacteristic(ICON_UUID, BLECharacteristic::PROPERTY_WRITE_NR);
  ic->setCallbacks(new IconWriteCb());
  svc->start();
  BLEAdvertising *adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(SERVICE_UUID);
  adv->setScanResponse(true);
  BLEDevice::startAdvertising();
  Serial.println("NAV: GATT server advertising");
}

// Demo route: distance counts down with the simulated speed. Yields to a real phone.
static void simulate(float kmh, uint32_t now) {
  struct Step { const char *turn, *street; int metres; };
  static const Step ROUTE[] = {
      {"straight", "Rama IV Rd", 1800}, {"left", "Sukhumvit Rd", 900}, {"slight-right", "Expressway ramp", 600},
      {"straight", "Chalerm Maha Nakhon Expy", 2500}, {"slight-left", "Exit 2-04", 400}, {"roundabout", "3rd exit", 300},
      {"right", "Soi Sukhumvit 24", 450}, {"uturn", "Soi Sukhumvit 24", 200}, {"arrive", "Destination on left", 60}};
  static const int N = sizeof ROUTE / sizeof ROUTE[0];
  static int step = -1;
  static float left = 0;
  static uint32_t last = 0, lastSend = 0;
  if (lastPhoneRx && (int32_t)(now - lastPhoneRx) < 10000) return;  // signed: the write may land after `now`
  float dt = last ? (now - last) / 1000.0f : 0;
  last = now;
  if (step < 0 || left <= 0) {
    step = (step + 1) % N;
    left = ROUTE[step].metres;
  }
  if (!isnan(kmh)) left -= kmh / 3.6f * dt;
  if (now - lastSend < 500) return;
  lastSend = now;
  int rest = (int)max(left, 0.0f);
  for (int i = step + 1; i < N; i++) rest += ROUTE[i].metres;
  char msg[96];
  snprintf(msg, sizeof msg, "N|%s|%d|%d min|%s", ROUTE[step].turn, (int)max(left, 0.0f), rest / 600 + 1,
           ROUTE[step].street);
  apply(msg, now);
}

// Distance shown right now: the phone's figure minus what OBD speed says we drove since.
static int32_t liveMetres(const State &s, float travelled) {
  if (s.metres < 0) return -1;
  return max<int32_t>(0, s.metres - (int32_t)travelled);
}

static String distText(int32_t m) {
  if (m < 0) return "";
  if (m >= 1000) return String(m / 1000.0f, m >= 10000 ? 0 : 1) + " km";
  if (m >= 100) return String((m + 25) / 50 * 50) + " m";
  return String((m + 5) / 10 * 10) + " m";
}

static const char *turnWord(Turn t) {
  static const char *const W[] = {"", "AHEAD", "LEFT", "RIGHT", "KEEP LEFT", "KEEP RIGHT", "SHARP LEFT",
                                  "SHARP RIGHT", "U-TURN", "ROUNDABOUT", "ARRIVE", "TURN"};
  return W[t];
}

static bool isLive(const State &s, bool demo, uint32_t now) { return s.active && (demo || now - s.rxMs < STALE_MS); }

// One-line hint for the status bar on the other pages when a turn is close, e.g. "LEFT in 150 m" ("" = none).
static String hint(const State &s, float travelled, bool demo, uint32_t now) {
  int32_t m = liveMetres(s, travelled);
  if (!isLive(s, demo, now) || s.turn == NONE || s.turn == STRAIGHT || m < 0 || m > 1000) return "";
  return String(turnWord(s.turn)) + " in " + distText(m);  // short: the bar also holds FPS and page dots
}

// Integrates OBD speed between phone updates (UI side, core 1).
class Tracker {
public:
  void tick(float kmh, uint32_t now, uint32_t stepRxMs) {
    if (stepRxMs != rx) { rx = stepRxMs; travelled = 0; }
    if (last && !isnan(kmh)) travelled += kmh / 3.6f * (now - last) / 1000.0f;
    last = now;
  }
  float travelled = 0;

private:
  uint32_t rx = 0, last = 0;
};

class Page {
public:
  explicit Page(LGFX &l) : lcd(l), arrow(&l), dist(&l), side(&l) {}

  void begin() {
    make(arrow, AW, AH);
    make(dist, PW - 16, 44);
    make(side, RW - 24, 30);
  }

  void show() {
    using namespace theme;
    lcd.fillRect(0, Y0, lcd.width(), lcd.height() - Y0, BG);
    lcd.fillRoundRect(PX, PY, PW, PH, 10, PANEL);
    lcd.drawRoundRect(PX, PY, PW, PH, 10, EDGE);
    lcd.fillRoundRect(RX, PY, RW, PH, 10, PANEL);
    lcd.drawRoundRect(RX, PY, RW, PH, 10, EDGE);
    lcd.fillRoundRect(RX, PY + 10, 4, 28, 2, CYAN);
    lcd.setFont(&fonts::FreeSansBold9pt7b);
    lcd.setTextDatum(top_left);
    lcd.setTextColor(MUTED);
    lcd.drawString("ETA", RX + 14, PY + 116);
    lcd.drawString("SPEED", RX + 14, PY + 176);
    lcd.drawFastHLine(RX + 10, PY + 108, RW - 20, DIM);
    lcd.drawFastHLine(RX + 10, PY + 168, RW - 20, DIM);
    shownTurn = shownWordTurn = ICON;  // forces a redraw of arrow and street
    shownIcon = 0xFFFFFFFF;
    shownStreet = String((char)1);
    for (auto &s : shown) s = "\x01";
  }

  void update(const State &s, float travelled, float kmh, bool phone, bool demo) {
    bool live = isLive(s, demo, millis());
    Turn t = live ? s.turn : NONE;
    if (t != shownTurn || (t == ICON && s.iconVersion != shownIcon)) {
      shownTurn = t;
      shownIcon = s.iconVersion;
      State a = s;
      a.turn = t;
      drawArrow(a);
    }
    String street = live ? String(s.street) : String("");
    if (street != shownStreet || t != shownWordTurn) {
      shownStreet = street;
      shownWordTurn = t;
      drawStreet(street.c_str(), turnWord(t));
    }
    text(0, dist, live ? distText(liveMetres(s, travelled)) : String("--"), &fonts::FreeSansBold24pt7b,
         theme::TEXT, PX + 8, PY + PH - 30, middle_center);
    text(1, side, live && s.eta[0] ? String(s.eta) : String("--"), &fonts::FreeSansBold12pt7b, theme::TEXT,
         RX + 12, PY + 148, middle_left);
    text(2, side, isnan(kmh) ? String("--") : String((int)kmh) + " km/h", &fonts::FreeSansBold12pt7b, theme::TEXT,
         RX + 12, PY + 208, middle_left);
    // header doubles as the link state
    String link = phone ? "PHONE LINKED" : demo ? "DEMO ROUTE" : "NO PHONE";
    text(3, side, link, &fonts::FreeSansBold9pt7b, phone ? theme::GREEN : demo ? theme::AMBER : theme::MUTED,
         RX + 12, PY + 22, middle_left);
  }

private:
  static const int Y0 = Dashboard::BAR_H;
  static const int PX = 8, PY = Y0 + 6, PW = 220, PH = 272 - Y0 - 14;
  static const int RX = PX + PW + 8, RW = 480 - 8 - RX;
  static const int AW = 160, AH = 160;

  LGFX &lcd;
  LGFX_Sprite arrow, dist, side;
  Turn shownTurn = NONE, shownWordTurn = NONE;
  uint32_t shownIcon = 0;
  String shownStreet, shown[4];

  // PSRAM: these redraw only when the turn or a number changes, so internal RAM
  // (scarce once BLE runs two links) is not worth spending on them.
  static void make(LGFX_Sprite &s, int w, int h) {
    s.setColorDepth(16);
    s.setPsram(true);
    s.createSprite(w, h);
  }

  void text(int slot, LGFX_Sprite &spr, const String &t, const lgfx::IFont *f, uint16_t color, int x, int y,
            textdatum_t datum) {
    String key = t + "/" + color;
    if (key == shown[slot]) return;
    shown[slot] = key;
    spr.fillSprite(theme::PANEL);
    spr.setFont(f);
    spr.setTextColor(color);
    spr.setTextDatum(datum);
    spr.drawString(t, datum == middle_center ? spr.width() / 2 : 0, spr.height() / 2);
    spr.pushSprite(x, y - spr.height() / 2);
  }

  // Street name: up to two lines of 12 pt under the "NAVIGATION" header, word-wrapped.
  void drawStreet(const char *street, const char *word) {
    using namespace theme;
    int x = RX + 12, w = RW - 24;
    lcd.fillRect(x, PY + 34, w, 70, PANEL);  // rows PY+34 .. PY+104
    lcd.setFont(&fonts::FreeSansBold9pt7b);
    lcd.setTextDatum(top_left);
    lcd.setTextColor(CYAN);
    lcd.drawString(word, x, PY + 36);
    lcd.setFont(&fonts::FreeSansBold12pt7b);
    lcd.setTextColor(TEXT);
    String s = street, line;
    int y = PY + 56;
    while (s.length() && y < PY + 100) {
      int cut = s.length();
      while (lcd.textWidth(s.substring(0, cut)) > w) {
        int sp = s.lastIndexOf(' ', cut - 1);
        cut = sp > 0 ? sp : cut - 1;
      }
      lcd.drawString(s.substring(0, cut), x, y);
      s = s.substring(cut);
      s.trim();
      y += 24;
    }
  }

  // Arrow in a sprite; heading 0 = up, negative = left. Stem from the bottom, bend at mid height.
  void drawArrow(const State &s) {
    using namespace theme;
    LGFX_Sprite &a = arrow;
    a.fillSprite(PANEL);
    int cx = AW / 2;  // turns move it against the turn so the whole arrow sits centred
    const int base = AH - 8, bend = AH / 2 + 10, w = 9;
    const uint16_t col = s.turn == ARRIVE ? GREEN : TEXT;
    auto head = [&](float x, float y, float deg) {
      float r = deg * DEG_TO_RAD, dx = sinf(r), dy = -cosf(r);
      a.fillTriangle(x + dx * 26, y + dy * 26, x - dy * 22, y + dx * 22, x + dy * 22, y - dx * 22, col);
    };
    auto leg = [&](float deg, float len) {
      float r = deg * DEG_TO_RAD, ex = cx + sinf(r) * len, ey = bend - cosf(r) * len;
      a.drawWideLine(cx, bend, ex, ey, w, col);
      head(ex, ey, deg);
    };
    switch (s.turn) {
      case NONE: break;
      case ICON:
        if (s.hasIcon)
          for (int y = 0; y < ICON_W; y++)
            for (int x = 0; x < ICON_W; x++)
              if (s.icon[y * 6 + x / 8] & (0x80 >> (x & 7))) a.fillRect(cx - 72 + x * 3, 8 + y * 3, 3, 3, col);
        break;
      case STRAIGHT:
        a.drawWideLine(cx, base, cx, 40, w, col);
        head(cx, 40, 0);
        break;
      case UTURN:
        a.drawWideLine(cx + 22, base, cx + 22, 60, w, col);
        a.fillArc(cx, 60, 22 + w, 22 - w, 180, 360, col);
        a.drawWideLine(cx - 22, 60, cx - 22, 100, w, col);
        head(cx - 22, 100, 180);
        break;
      case ROUNDABOUT:
        a.fillArc(cx, bend, 34, 34 - 2 * w, 0, 360, col);
        a.drawWideLine(cx, base, cx, bend + 34, w, col);
        a.drawWideLine(cx + 24, bend - 24, cx + 50, bend - 50, w, col);
        head(cx + 50, bend - 50, 45);
        break;
      case ARRIVE:
        a.fillCircle(cx, 60, 34, col);
        a.fillTriangle(cx - 30, 76, cx + 30, 76, cx, base, col);
        a.fillCircle(cx, 60, 14, PANEL);
        break;
      default: {
        static const int16_t DEG[] = {0, 0, -90, 90, -45, 45, -135, 135};
        float deg = DEG[s.turn];
        cx -= (int)(sinf(deg * DEG_TO_RAD) * 28);
        a.drawWideLine(cx, base, cx, bend, w, col);
        a.fillCircle(cx, bend, w, col);
        leg(deg, fabsf(deg) > 100 ? 40 : 50);
      }
    }
    a.pushSprite(PX + (PW - AW) / 2, PY + 6);
  }
};

}  // namespace nav
