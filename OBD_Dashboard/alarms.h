// SmartGauge Libre — Copyright (c) 2026 khanatuch — code written by khanatuch & Claude (AI)
// SPDX-License-Identifier: CC-BY-NC-SA-4.0
// Per-PID low/high alarms. Limits live in NVS (namespace "alarms", one blob per
// PID) so they survive power-off; PIDs without a stored entry use DEFAULTS.
//
// An alarm turns on only after the value has been outside its limits for HOLD_MS
// and turns off only after it has been back inside for HOLD_MS, so a value that
// sits right on the threshold does not make the card strobe.
// Runs on the UI core (update() from render); obdTask only reads `limit` to
// decide which values to keep polling in the background.
#pragma once
#include <Preferences.h>
#include "obd_pids.h"

namespace alarms {
struct Limit {
  float lo, hi;  // NAN = no limit on that side
};

struct Default {
  uint8_t pid;
  float lo, hi;
};
// Match the warning colours the pages already used before alarms existed.
static const Default DEFAULTS[] = {
    {0x05, NAN, 105},      // coolant over 105 °C
    {0x5C, NAN, 130},      // oil over 130 °C
    {0x42, 12.0f, 15.0f},  // battery under 12 V (not charging) / over 15 V (regulator)
    {0x2F, 15, NAN},       // fuel under 15 %
};
static const uint32_t HOLD_MS = 1000;

static Limit limit[NPIDS];
static bool active[NPIDS];
static uint32_t since[NPIDS];  // last time the value agreed with active[]

static bool hasLimit(int i) { return !isnan(limit[i].lo) || !isnan(limit[i].hi); }

static void key(uint8_t pid, char *k) { snprintf(k, 8, "p%02X", pid); }

static void load() {
  for (auto &l : limit) l = {NAN, NAN};
  for (auto &d : DEFAULTS) {
    int i = pidIndex(d.pid);
    if (i >= 0) limit[i] = {d.lo, d.hi};
  }
  Preferences p;
  if (!p.begin("alarms", true)) return;  // namespace does not exist until the first save
  for (int i = 0; i < NPIDS; i++) {
    char k[8];
    key(PIDS[i].pid, k);
    Limit l;
    if (p.isKey(k) && p.getBytes(k, &l, sizeof l) == sizeof l) limit[i] = l;
  }
  p.end();
}

static bool set(uint8_t pid, float lo, float hi) {
  int i = pidIndex(pid);
  if (i < 0) return false;
  limit[i] = {lo, hi};
  char k[8];
  key(pid, k);
  Preferences p;
  p.begin("alarms", false);
  p.putBytes(k, &limit[i], sizeof limit[i]);
  p.end();
  return true;
}

static void resetDefaults() {
  Preferences p;
  p.begin("alarms", false);
  p.clear();
  p.end();
  load();
}

static void update(uint32_t now) {
  for (int i = 0; i < NPIDS; i++) {
    float v = pidValues[i];
    const Limit &l = limit[i];
    bool out = !isnan(v) && ((!isnan(l.lo) && v < l.lo) || (!isnan(l.hi) && v > l.hi));
    if (out == active[i]) since[i] = now;
    else if (now - since[i] >= HOLD_MS) { active[i] = out; since[i] = now; }
  }
}

static bool on(int i) { return i >= 0 && active[i]; }

// Index of the first active alarm, or -1.
static int first() {
  for (int i = 0; i < NPIDS; i++)
    if (active[i]) return i;
  return -1;
}

// Shared blink phase so every alarmed value flashes in step (2 Hz).
static bool flash() { return (millis() / 250) & 1; }
}  // namespace alarms
