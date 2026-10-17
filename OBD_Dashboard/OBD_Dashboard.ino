// SmartGauge Libre — Copyright (c) 2026 khanatuch — code written by khanatuch & Claude (AI)
// SPDX-License-Identifier: GPL-3.0-or-later
// OBD-II dashboard for Sunton ESP32-4827S043 + ELM327 BLE adapter.
// Uses the ESP32-S3's built-in BLE; no extra hardware.
//
// Pages: race, digital, tach + 6 main values, trip computer,
// 0-100 / 100-0 timer, then every PID the car supports (8 per page), last =
// trouble codes. Tap the right side of the screen for the next page, the left
// edge for the previous one; long-press (0.7 s) resets the thing under the finger
// (boost peak, trip A/B, timer results). DEMO button (top right) shows simulated
// values without a car.
//
// Threads: the UI runs in loop() on core 1 at up to 60 fps; everything that
// talks to the adapter (scan, connect, polling, DTC reads) runs in obdTask on
// core 0, so slow OBD replies never stall the screen.
// Serial log at 115200 shows the BLE scan and every ELM327 command/response.
//
// Optional: lock onto one adapter by name or MAC instead of auto-detect:
// #define OBD_DEVICE_NAME "IOS-Vlink"

#include "project.h"
#include "board.h"
#include "elm327_ble.h"
#include "obd_pids.h"
#include "ui.h"
#include "race.h"
#include "trip.h"
#include "launch.h"
#include "health.h"

static const int BACKLIGHT = 90;  // 0..255; full brightness draws a lot more current

static LGFX lcd;
static Dashboard dash(lcd);
static RacePage racePage(lcd);
static DigitalPage digitalPage(lcd);
static TripPage tripPage(lcd);
static TimerPage timerPage(lcd);
static TripComputer realTrip(true), demoTrip(false);  // demo km never touch the saved trips
static LaunchTimer launch;
static health::Monitor realHealth(true), demoHealth(false);  // demo shows a canned history
static health::Page healthView(lcd);
static uint32_t healthNoticeUntil = 0;
static float boostPeak = NAN;
static Elm327Ble elm;

// ---- shared between UI (core 1) and obdTask (core 0) ----
static SemaphoreHandle_t lock;  // guards statusMsg/statusDot, dtc, pollList
enum class Link { Scan, Init, Ecu, Live };
static volatile Link linkState = Link::Scan;
static volatile bool demo = false;
static volatile bool wantDisconnect = false, pagesDirty = false;
static volatile bool dtcWanted = false, dtcBusy = false;
static String statusMsg = "Starting...";
static uint16_t statusDot = theme::MUTED;
static DtcReport dtc;
static const int MAX_POLL = 12;
static int pollList[MAX_POLL];
static int pollN = 0;
static bool pollFast = false;  // also read RPM + speed every cycle
static bool pollBackground = true;  // also read speed (trip) + alarmed values about once a second
// Latest speed sample for the trip computer and the launch timer (guarded by `lock`).
static float speedSample = NAN;
static uint32_t speedSampleMs = 0, speedSeq = 0;
static volatile uint32_t rpmSampleMs = 0;

struct Locked {
  Locked() { xSemaphoreTake(lock, portMAX_DELAY); }
  ~Locked() { xSemaphoreGive(lock); }
};

// ---- page layout (UI side) ----
static const uint8_t MAIN_PIDS[Dashboard::GAUGE_CARDS] = {0x05, 0x0E, 0x04, 0x0F, 0x42, 0x11};
static int mainIdx[Dashboard::GAUGE_CARDS];
static int rpmIdx, speedIdx, coolIdx, ignIdx, fuelIdx, battIdx, oilIdx;
static int mapIdx, baroIdx, intakeIdx, loadIdx, throttleIdx, lambdaIdx, lambdaCmdIdx;
static int ltft1Idx, ltft2Idx, o2FrontIdx, o2RearIdx;

// Fixed pages before the per-PID grid pages.
enum { PAGE_RACE, PAGE_DIGITAL, PAGE_GAUGE, PAGE_TRIP, PAGE_TIMER, FIXED_PAGES };

static int gridIdx[NPIDS];
static int gridCount = 0;
static int page = 0;
static int fps = 0;
static TaskHandle_t obdTaskHandle = nullptr;

static bool live() { return linkState == Link::Live; }

static bool available(uint8_t pid) {
  if (demo || !live()) return true;          // show the full list until we know better
  return elm.supports(pid) || pid == 0x42;   // battery falls back to ATRV
}

static int gridPages() { return (gridCount + Dashboard::GRID_CARDS - 1) / Dashboard::GRID_CARDS; }
static int pageCount() { return FIXED_PAGES + gridPages() + 2; }
static int healthPage() { return pageCount() - 2; }
static int dtcPage() { return pageCount() - 1; }

static void setPoll(const int *idx, int n, bool fast, bool background = true) {
  Locked l;
  pollN = n;
  for (int i = 0; i < n; i++) pollList[i] = idx[i];
  pollFast = fast;
  pollBackground = background;
}

static TripComputer &trip() { return demo ? demoTrip : realTrip; }
static health::Monitor &healthMon() { return demo ? demoHealth : realHealth; }

// While the timer is armed only speed is read, so samples come as fast as the adapter allows.
static void setTimerPoll() {
  if (launch.armed) setPoll(&speedIdx, 1, false, false);
  else setPoll(nullptr, 0, true);
}

static void showPage(int p) {
  page = (p + pageCount()) % pageCount();
  if (page == PAGE_RACE) {
    dash.showPage(Dashboard::RACE, nullptr, 0);
    racePage.show();
    int idx[] = {mapIdx, ignIdx, intakeIdx, loadIdx, throttleIdx, coolIdx, oilIdx, battIdx, fuelIdx, baroIdx};
    setPoll(idx, 10, true);
  } else if (page == PAGE_DIGITAL) {
    dash.showPage(Dashboard::DIGITAL, nullptr, 0);
    digitalPage.show();
    int idx[] = {mapIdx, coolIdx, lambdaIdx, lambdaCmdIdx, battIdx, baroIdx};
    setPoll(idx, 6, true);
  } else if (page == PAGE_GAUGE) {
    dash.showPage(Dashboard::GAUGE, mainIdx, Dashboard::GAUGE_CARDS);
    setPoll(mainIdx, Dashboard::GAUGE_CARDS, true);
  } else if (page == PAGE_TRIP) {
    dash.showPage(Dashboard::TRIP, nullptr, 0);
    tripPage.show();
    setPoll(nullptr, 0, true);
  } else if (page == PAGE_TIMER) {
    dash.showPage(Dashboard::TIMER, nullptr, 0);
    timerPage.show();
    setTimerPoll();
  } else if (page == healthPage()) {
    dash.showPage(Dashboard::HEALTH, nullptr, 0);
    healthView.show();
    int idx[] = {battIdx, coolIdx, ltft1Idx, ltft2Idx, o2FrontIdx, o2RearIdx, rpmIdx};
    setPoll(idx, 7, false);
  } else if (page < dtcPage()) {
    int first = (page - FIXED_PAGES) * Dashboard::GRID_CARDS;
    int n = min(Dashboard::GRID_CARDS, gridCount - first);
    dash.showPage(Dashboard::GRID, gridIdx + first, n);
    setPoll(gridIdx + first, n, false);
  } else {
    dash.showPage(Dashboard::DTC, nullptr, 0);
    setPoll(nullptr, 0, false);
    dtcWanted = true;
  }
}

static void buildPages() {
  gridCount = 0;
  for (int i = 0; i < NPIDS; i++)
    if (available(PIDS[i].pid)) gridIdx[gridCount++] = i;
  Serial.printf("%d values available, %d pages\n", gridCount, pageCount());
  showPage(min(page, pageCount() - 1));
}

// ---- OBD side (core 0) ----
static void setStatus(const String &s, uint16_t dot) {
  Locked l;
  statusMsg = s;
  statusDot = dot;
}

static void publishSpeed(float v, uint32_t ms) {
  Locked l;
  speedSample = v;
  speedSampleMs = ms;
  speedSeq++;
}

static void readOne(int idx) {
  const PidDef &d = PIDS[idx];
  uint8_t b[2];
  if (d.pid == 0x42 && !elm.supports(0x42)) pidValues[idx] = elm.readAdapterVoltage();
  else if (!elm.supports(d.pid)) pidValues[idx] = NAN;
  else pidValues[idx] = elm.queryPid(d.pid, b, d.bytes) ? pidDecode(d, b) : NAN;
  if (idx == speedIdx) publishSpeed(pidValues[idx], millis());
  if (idx == rpmIdx && !isnan(pidValues[idx])) rpmSampleMs = millis();
}

// Next PID with an alarm limit that the car supports (round robin), or -1.
static int nextWatched() {
  static int pos = 0;
  for (int k = 0; k < NPIDS; k++) {
    int i = pos++ % NPIDS;
    if (alarms::hasLimit(i) && (PIDS[i].pid == 0x42 || elm.supports(PIDS[i].pid))) return i;
  }
  return -1;
}

// HEALTH keeps its own inputs fresh on every page: the O2 pair gets every other
// slot (the catalyst check needs their swing), the rest share the remaining ones.
static int nextHealthPid() {
  static int pos = 0;
  const int order[] = {o2FrontIdx, o2RearIdx, battIdx, o2FrontIdx, o2RearIdx, coolIdx, o2FrontIdx, o2RearIdx,
                       ltft1Idx, o2FrontIdx, o2RearIdx, rpmIdx, o2FrontIdx, o2RearIdx, ltft2Idx};
  const int n = sizeof order / sizeof order[0];
  for (int k = 0; k < n; k++) {
    int i = order[pos++ % n];
    if (i >= 0 && (PIDS[i].pid == 0x42 || elm.supports(PIDS[i].pid))) return i;
  }
  return -1;
}

static void readDtcNow() {
  dtcBusy = true;
  DtcReport r;
  {
    Locked l;
    r.version = dtc.version;
  }
  elm.readDtcs(r);
  {
    Locked l;
    dtc = r;
  }
  dtcBusy = false;
  Serial.printf("DTC: MIL=%d stored=%d pending=%d\n", r.mil, r.stored.size(), r.pending.size());
  for (auto &c : r.stored) Serial.printf("  stored  %s  %s\n", c.c_str(), dtcDescription(c));
  for (auto &c : r.pending) Serial.printf("  pending %s  %s\n", c.c_str(), dtcDescription(c));
}

static void pollLive() {
  static int pos = 0;
  static uint32_t lastBackground = 0;
  int list[MAX_POLL], n;
  bool fast, background;
  {
    Locked l;
    n = pollN;
    fast = pollFast;
    background = pollBackground;
    for (int i = 0; i < n; i++) list[i] = pollList[i];
  }
  auto inList = [&](int idx) {
    for (int i = 0; i < n; i++) if (list[i] == idx) return true;
    return false;
  };
  // Pages that don't show speed or the alarmed values still need them: the trip
  // keeps counting and alarms keep watching whatever page is open.
  if (background && millis() - lastBackground >= 1000) {
    lastBackground = millis();
    if (!fast && !inList(speedIdx)) readOne(speedIdx);
    int w = nextWatched();
    if (w >= 0 && !inList(w)) readOne(w);
  }
  static uint32_t lastHealth = 0;
  if (background && millis() - lastHealth >= 400) {
    lastHealth = millis();
    int h = nextHealthPid();
    if (h >= 0 && !inList(h) && !(fast && h == rpmIdx)) readOne(h);
  }
  if (dtcWanted) {
    dtcWanted = false;
    readDtcNow();
    return;
  }
  if (fast) {
    readOne(rpmIdx);
    readOne(speedIdx);
    if (n) readOne(list[pos++ % n]);
  } else if (n) {
    readOne(list[pos++ % n]);
  } else {
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

static void obdTask(void *) {
  uint32_t lastEcuTry = 0;
  for (;;) {
    if (wantDisconnect) {
      wantDisconnect = false;
      elm.disconnect();
      linkState = Link::Scan;
    }
    if (demo) {
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }
    if (linkState != Link::Scan && !elm.linked()) {
      Serial.println("BLE link lost");
      clearPidValues();
      linkState = Link::Scan;
      pagesDirty = true;
    }

    switch (linkState) {
      case Link::Scan:
        setStatus("Searching for OBD adapter...", theme::ORANGE);
        if (elm.scanAndConnect()) linkState = Link::Init;
        break;

      case Link::Init:
        setStatus("Connected " + elm.deviceName + ", init...", theme::CYAN);
        if (elm.initAdapter()) linkState = Link::Ecu;
        else { elm.disconnect(); linkState = Link::Scan; }
        break;

      case Link::Ecu:
        if (millis() - lastEcuTry < 3000) { vTaskDelay(pdMS_TO_TICKS(50)); break; }
        setStatus("Connecting to ECU...", theme::CYAN);
        if (elm.connectEcu()) {
          linkState = Link::Live;
          pagesDirty = true;
          dtcWanted = true;  // fetch the check-engine state once
          setStatus(elm.deviceName + "  " + elm.protocol, theme::GREEN);
        } else {
          pidValues[battIdx] = elm.readAdapterVoltage();
          setStatus("No ECU reply - ignition ON?", theme::ORANGE);
        }
        lastEcuTry = millis();
        break;

      case Link::Live:
        pollLive();
        break;
    }
    vTaskDelay(1);
  }
}

// ---- UI side (core 1) ----
static void simulate() {
  float t = millis() / 1000.0f;
  float k = 0.5f + 0.5f * sinf(t * 0.6f);
  for (int i = 0; i < NPIDS; i++) {
    const PidDef &d = PIDS[i];
    float w = 0.5f + 0.5f * sinf(t * (0.15f + 0.03f * (i % 7)) + i);
    pidValues[i] = d.lo + (d.hi - d.lo) * w;
  }
  pidValues[rpmIdx] = 800 + 6200 * k * k;
  rpmSampleMs = millis();
  // like a real ECU: whole km/h, ~10 samples a second
  static uint32_t lastSpeed = 0;
  if (millis() - lastSpeed >= 100) {
    lastSpeed = millis();
    publishSpeed(floorf(180 * k), lastSpeed);
  }
  pidValues[speedIdx] = speedSample;
  pidValues[ignIdx] = 10 + 22 * k;
  pidValues[pidIndex(0x11)] = 12 + 70 * k * k;
  pidValues[pidIndex(0x04)] = 18 + 65 * k;
  pidValues[coolIdx] = 85 + 25 * (0.5f + 0.5f * sinf(t * 0.25f));
  pidValues[fuelIdx] = 8 + 50 * (0.5f + 0.5f * sinf(t * 0.1f));
  pidValues[mapIdx] = 95 + 110 * k * k;  // ~1.0 bar boost at the top of each pull
  pidValues[baroIdx] = 101;
  pidValues[lambdaIdx] = 1.0f - 0.18f * k * k;
  if (dtcWanted) {
    dtcWanted = false;
    Locked l;
    dtc.valid = true;
    dtc.mil = true;
    dtc.error = "";
    dtc.stored = {"P0420", "P0171"};
    dtc.pending = {"P0302"};
    dtc.version++;
  }
}

static void setDemo(bool on) {
  realHealth.endDrive(millis());  // a demo session is not a drive
  demo = on;
  wantDisconnect = true;
  clearPidValues();
  boostPeak = NAN;
  demoTrip.begin();
  demoHealth.seedDemo();
  launch.clear();
  {
    Locked l;
    dtc = DtcReport();
  }
  if (on) dtcWanted = true;
  buildPages();
}

static void toggleArm() {
  launch.arm(!launch.armed);
  setTimerPoll();
}

// Long-press: reset whatever value sits under the finger.
static void onLongPress(int x, int y) {
  if (page == PAGE_RACE && RacePage::isPeakArea(x, y)) boostPeak = NAN;
  else if (page == PAGE_DIGITAL && DigitalPage::isPeakArea(x, y)) boostPeak = NAN;
  else if (page == PAGE_TRIP && y > Dashboard::BAR_H + 6) trip().reset(TripPage::panelAt(x));
  else if (page == PAGE_TIMER && TimerPage::isResultArea(x, y)) launch.clear();
  else if (page == healthPage() && health::Page::isResetArea(x, y)) healthMon().resetBaseline();
}

static void onTap(int x, int y) {
  if (y < Dashboard::BAR_H + 6) {
    if (x > lcd.width() - 80) setDemo(!demo);
  } else if (dash.isRescanButton(x, y)) {
    dtcWanted = true;
  } else if (page == PAGE_TIMER && TimerPage::isArmButton(x, y)) {
    toggleArm();
  } else {
    showPage(x < 120 ? page - 1 : page + 1);
  }
}

// Taps act on release so a long-press (LONG_MS) can be told apart from a tap.
static void handleTouch() {
  static const uint32_t LONG_MS = 700;
  static bool wasDown = false, longDone = false;
  static int x0, y0;
  static uint32_t downAt;
  int x, y;
  bool down = touch::read(x, y);
  uint32_t now = millis();
  if (down && !wasDown) {
    x0 = x;
    y0 = y;
    downAt = now;
    longDone = false;
  } else if (down && !longDone && now - downAt >= LONG_MS) {
    longDone = true;
    onLongPress(x0, y0);
  } else if (!down && wasDown && !longDone) {
    onTap(x0, y0);
  }
  wasDown = down;
}

// Hands each new speed sample (from obdTask or simulate()) to the trip and the timer.
static void consumeSpeed() {
  static uint32_t seen = 0;
  float v;
  uint32_t t, seq;
  {
    Locked l;
    v = speedSample;
    t = speedSampleMs;
    seq = speedSeq;
  }
  if (seq == seen) return;
  seen = seq;
  // RPM is only polled on some pages; when it is stale assume the engine runs (ECU answers)
  float rpm = pidValues[rpmIdx];
  bool engineOn = millis() - rpmSampleMs > 3000 || isnan(rpm) || rpm > 300;
  trip().feed(v, t, engineOn);
  launch.feed(v, t);
}

// Feeds the health monitor once a second; when a drive starts or ends with a
// check at CHECK/FAULT, the status line says so for a while (once, no blinking).
static void tickHealth(uint32_t now) {
  static uint32_t last = 0, seenVersion = 0;
  static bool wasDriving = false;
  if (now - last < 1000) return;
  last = now;
  health::Monitor &m = healthMon();
  if (!demo) {
    health::Inputs in = {pidValues[rpmIdx], pidValues[battIdx], pidValues[coolIdx], pidValues[ltft1Idx],
                         ltft2Idx >= 0 ? pidValues[ltft2Idx] : NAN, pidValues[o2FrontIdx], pidValues[o2RearIdx],
                         live(), live() && elm.supports(0x14) && elm.supports(0x15)};
    m.tick(in, now);
  }
  bool started = m.driving() && !wasDriving;
  wasDriving = m.driving();
  if ((started || m.version != seenVersion) && m.worst() >= health::WARN) healthNoticeUntil = now + 20000;
  seenVersion = m.version;
}

// Cap the UI at ~60 fps (the panel refreshes at ~57 Hz, faster is wasted work).
static void paceFrame() {
  static uint32_t next = 0, fpsStart = 0, frames = 0;
  uint32_t now = millis();
  if ((int32_t)(next - now) > 0) delay(next - now);
  now = millis();
  next = max(now, next) + 16;
  frames++;
  if (now - fpsStart >= 1000) {
    fps = frames * 1000 / (now - fpsStart);
    frames = 0;
    fpsStart = now;
  }
}

static void render() {
  uint32_t now = millis();
  alarms::update(now);
  String msg;
  uint16_t dot, msgColor = theme::TEXT;
  {
    Locked l;
    msg = demo ? String("Simulated data") : statusMsg;
    dot = demo ? theme::AMBER : statusDot;
  }
  int al = alarms::first();
  if (al >= 0) {  // an alarm wins the status line on every page
    const PidDef &d = PIDS[al];
    msg = String(d.label) + " " + String(pidValues[al], (unsigned int)d.decimals) + " " + (d.unit[0] == '*' ? d.unit + 1 : d.unit);
    dot = theme::RED;
    msgColor = theme::RED;
  } else if ((int32_t)(healthNoticeUntil - now) > 0 && page != healthPage()) {
    msg = "HEALTH: see page";
    dot = msgColor = healthMon().worst() == health::BAD ? theme::RED : theme::AMBER;
  }
  dash.status(msg, dot, demo, page, pageCount(), fps, msgColor);

  // derived values shared by the racing pages
  float mapK = pidValues[mapIdx], baroK = pidValues[baroIdx];
  float boostBar = isnan(mapK) ? NAN : (mapK - (isnan(baroK) ? 101.3f : baroK)) / 100.0f;
  if (!isnan(boostBar)) boostPeak = max(isnan(boostPeak) ? 0.0f : boostPeak, boostBar);
  float lambda = !isnan(pidValues[lambdaIdx]) ? pidValues[lambdaIdx] : pidValues[lambdaCmdIdx];
  float afr = isnan(lambda) ? NAN : lambda * 14.7f;
  auto abit = [](int idx, int b) { return alarms::on(idx) ? 1u << b : 0u; };

  switch (dash.kind()) {
    case Dashboard::RACE: {
      uint16_t am = abit(rpmIdx, RacePage::F_RPM) | abit(speedIdx, RacePage::F_SPEED) | abit(ignIdx, RacePage::F_IGN) |
                    abit(intakeIdx, RacePage::F_INTAKE) | abit(coolIdx, RacePage::F_COOLANT) |
                    abit(oilIdx, RacePage::F_OIL) | abit(battIdx, RacePage::F_BATTERY) | abit(fuelIdx, RacePage::F_FUEL);
      racePage.update({pidValues[rpmIdx], pidValues[speedIdx], boostBar, pidValues[ignIdx], pidValues[intakeIdx],
                       pidValues[loadIdx], pidValues[throttleIdx], pidValues[coolIdx], pidValues[oilIdx],
                       pidValues[battIdx], pidValues[fuelIdx], boostPeak, am});
      break;
    }
    case Dashboard::DIGITAL: {
      uint8_t am = abit(coolIdx, 1) | abit(lambdaIdx, 2) | abit(battIdx, 3);
      digitalPage.update({pidValues[rpmIdx], boostBar, pidValues[coolIdx], afr, pidValues[battIdx], boostPeak, am});
      break;
    }
    case Dashboard::TRIP: tripPage.update(trip()); break;
    case Dashboard::TIMER: timerPage.update(launch, pidValues[speedIdx], now); break;
    case Dashboard::GAUGE: dash.updateGauge(rpmIdx, speedIdx); break;
    case Dashboard::GRID: dash.updateCards(); break;
    case Dashboard::HEALTH: {
      Locked l;
      healthView.update(healthMon(), dtc, demo || live());
      break;
    }
    case Dashboard::DTC: {
      Locked l;
      dash.updateDtc(dtc, demo || live(), dtcBusy);
      break;
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial.setTimeout(200);  // "L..." lines arrive whole; never stall the UI long
  delay(200);
  Serial.println("\n== " PROJECT_NAME " " PROJECT_VERSION " - " PROJECT_CREDIT " - " PROJECT_URL " ==");
  lock = xSemaphoreCreateMutex();
  // Bring the radio up before the backlight: BLE calibration + full backlight
  // at the same moment browns out the board on a weak USB port.
  elm.begin();
  Serial.println("BLE ok");

  rpmIdx = pidIndex(0x0C);
  speedIdx = pidIndex(0x0D);
  coolIdx = pidIndex(0x05);
  ignIdx = pidIndex(0x0E);
  fuelIdx = pidIndex(0x2F);
  battIdx = pidIndex(0x42);
  oilIdx = pidIndex(0x5C);
  mapIdx = pidIndex(0x0B);
  baroIdx = pidIndex(0x33);
  intakeIdx = pidIndex(0x0F);
  loadIdx = pidIndex(0x04);
  throttleIdx = pidIndex(0x11);
  lambdaIdx = pidIndex(0x24);
  lambdaCmdIdx = pidIndex(0x44);
  ltft1Idx = pidIndex(0x07);
  ltft2Idx = pidIndex(0x09);
  o2FrontIdx = pidIndex(0x14);
  o2RearIdx = pidIndex(0x15);
  for (int i = 0; i < Dashboard::GAUGE_CARDS; i++) mainIdx[i] = pidIndex(MAIN_PIDS[i]);
  clearPidValues();
  alarms::load();
  realTrip.begin();
  demoTrip.begin();
  realHealth.begin();
  demoHealth.seedDemo();

  lcd.init();
  lcd.setBrightness(0);
  drawSplash(lcd);
  for (int b = 0; b <= BACKLIGHT; b += 10) { lcd.setBrightness(b); delay(10); }
  uint32_t splashAt = millis();
  touch::begin();
  dash.begin();
  racePage.begin();
  digitalPage.begin();
  tripPage.begin();
  timerPage.begin();
  healthView.begin();
  while (millis() - splashAt < 1800) delay(10);
  buildPages();
  render();
  Serial.printf("display ok, free heap %u, free PSRAM %u\n", ESP.getFreeHeap(), ESP.getFreePsram());

  xTaskCreatePinnedToCore(obdTask, "obd", 8192, nullptr, 1, &obdTaskHandle, 0);
}

// Same as a long-press on the page: boost peak, trip A (which = 0) / B (1), timer results.
static void resetCurrent(int which) {
  if (page == PAGE_RACE || page == PAGE_DIGITAL) boostPeak = NAN;
  else if (page == PAGE_TRIP) trip().reset(which);
  else if (page == PAGE_TIMER) launch.clear();
  else if (page == healthPage()) healthMon().resetBaseline();
}

// "L"        list limits      "L05 - 110"  coolant: no low limit, high 110
// "L!"       factory defaults "L2F 10 -"   fuel: low 10 %, no high limit
static void limitCommand(String a) {
  a.trim();
  if (a == "!") alarms::resetDefaults();
  else if (a.length()) {
    char lo[16] = "-", hi[16] = "-";
    unsigned pid;
    if (sscanf(a.c_str(), "%x %15s %15s", &pid, lo, hi) < 1) return;
    auto num = [](const char *s) { return s[0] == '-' && s[1] == 0 ? NAN : (float)atof(s); };
    if (!alarms::set(pid, num(lo), num(hi))) Serial.printf("unknown PID %02X\n", pid);
  }
  for (int i = 0; i < NPIDS; i++)
    if (alarms::hasLimit(i))
      Serial.printf("L%02X %-14s low %-8s high %s\n", PIDS[i].pid, PIDS[i].label,
                    isnan(alarms::limit[i].lo) ? "-" : String(alarms::limit[i].lo, 1).c_str(),
                    isnan(alarms::limit[i].hi) ? "-" : String(alarms::limit[i].hi, 1).c_str());
}

// Serial debug commands (115200):  S = screenshot, D = toggle demo, N / P = next / previous page,
// R = reset (boost peak / trip A / timer results), B = reset trip B, A = arm timer, L... = alarm limits.
// Screenshot format: line "SHOT <w> <h>\n" then w*h RGB565 pixels, high byte first, row by row.
// tools/screenshot.py turns it into a PNG.
static void handleSerial() {
  if (!Serial.available()) return;
  switch (Serial.read()) {
    case 'S': {
      // pause the OBD task so its log lines cannot land inside the pixel stream
      if (obdTaskHandle) vTaskSuspend(obdTaskHandle);
      static uint16_t row[480];
      Serial.printf("SHOT %d %d\n", lcd.width(), lcd.height());
      for (int y = 0; y < lcd.height(); y++) {
        lcd.readRect(0, y, lcd.width(), 1, row);
        Serial.write((const uint8_t *)row, lcd.width() * 2);
      }
      Serial.flush();
      if (obdTaskHandle) vTaskResume(obdTaskHandle);
      break;
    }
    case 'D': setDemo(!demo); break;
    case 'N': showPage(page + 1); break;
    case 'P': showPage(page - 1); break;
    case 'R': resetCurrent(0); break;
    case 'B': resetCurrent(1); break;
    case 'A': if (page == PAGE_TIMER) toggleArm(); break;
    case 'L': limitCommand(Serial.readStringUntil('\n')); break;
  }
}

void loop() {
  handleSerial();
  handleTouch();
  if (pagesDirty) {
    pagesDirty = false;
    buildPages();
  }
  if (demo) simulate();
  consumeSpeed();
  trip().loop(millis());
  tickHealth(millis());
  render();
  paceFrame();
}
