// SmartGauge Libre — Copyright (c) 2026 khanatuch — code written by khanatuch & Claude (AI)
// SPDX-License-Identifier: CC-BY-NC-SA-4.0
// SAE J1979 mode 01 PIDs that decode to one number: value = raw * mul + add,
// where raw is A (1 byte), A*256+B (2 bytes) or that as int16 (sgn).
// Units starting with '*' get a drawn degree sign ("*C" -> °C).
#pragma once
#include <Arduino.h>

struct PidDef {
  uint8_t pid;
  const char *label;
  const char *unit;
  uint8_t bytes;
  bool sgn;
  float mul, add;
  uint8_t decimals;
  float lo, hi;  // typical range, used for demo values
};

static const float PCT = 100.0f / 255.0f;
static const float TRIM = 100.0f / 128.0f;
static const float LAMBDA = 2.0f / 65536.0f;

static const PidDef PIDS[] = {
    {0x0C, "Engine RPM", "rpm", 2, 0, 0.25f, 0, 0, 750, 4500},
    {0x0D, "Speed", "km/h", 1, 0, 1, 0, 0, 0, 120},
    {0x0E, "Ign. timing", "*BTDC", 1, 0, 0.5f, -64, 1, 5, 35},
    {0x05, "Coolant", "*C", 1, 0, 1, -40, 0, 82, 95},
    {0x04, "Engine load", "%", 1, 0, PCT, 0, 0, 15, 80},
    {0x0F, "Intake air", "*C", 1, 0, 1, -40, 0, 30, 45},
    {0x42, "Battery", "V", 2, 0, 0.001f, 0, 1, 13.6f, 14.4f},
    {0x11, "Throttle", "%", 1, 0, PCT, 0, 0, 12, 70},
    {0x0B, "Intake press", "kPa", 1, 0, 1, 0, 0, 30, 100},
    {0x10, "Air flow", "g/s", 2, 0, 0.01f, 0, 1, 3, 40},
    {0x06, "Short trim 1", "%", 1, 0, TRIM, -100, 1, -5, 5},
    {0x07, "Long trim 1", "%", 1, 0, TRIM, -100, 1, -4, 4},
    {0x08, "Short trim 2", "%", 1, 0, TRIM, -100, 1, -5, 5},
    {0x09, "Long trim 2", "%", 1, 0, TRIM, -100, 1, -4, 4},
    {0x0A, "Fuel press", "kPa", 1, 0, 3, 0, 0, 250, 350},
    {0x2F, "Fuel level", "%", 1, 0, PCT, 0, 0, 40, 70},
    {0x5E, "Fuel rate", "L/h", 2, 0, 0.05f, 0, 1, 0.6f, 8},
    {0x5C, "Oil temp", "*C", 1, 0, 1, -40, 0, 85, 105},
    {0x46, "Outside air", "*C", 1, 0, 1, -40, 0, 30, 34},
    {0x33, "Air pressure", "kPa", 1, 0, 1, 0, 0, 100, 101},
    {0x14, "O2 sensor 1", "V", 1, 0, 0.005f, 0, 2, 0.1f, 0.9f},
    {0x15, "O2 sensor 2", "V", 1, 0, 0.005f, 0, 2, 0.6f, 0.75f},
    {0x16, "O2 sensor 3", "V", 1, 0, 0.005f, 0, 2, 0.1f, 0.9f},
    {0x17, "O2 sensor 4", "V", 1, 0, 0.005f, 0, 2, 0.1f, 0.9f},
    {0x18, "O2 sensor 5", "V", 1, 0, 0.005f, 0, 2, 0.1f, 0.9f},
    {0x19, "O2 sensor 6", "V", 1, 0, 0.005f, 0, 2, 0.6f, 0.75f},
    {0x1A, "O2 sensor 7", "V", 1, 0, 0.005f, 0, 2, 0.1f, 0.9f},
    {0x1B, "O2 sensor 8", "V", 1, 0, 0.005f, 0, 2, 0.1f, 0.9f},
    {0x24, "Lambda 1", "", 2, 0, LAMBDA, 0, 3, 0.97f, 1.03f},
    {0x25, "Lambda 2", "", 2, 0, LAMBDA, 0, 3, 0.97f, 1.03f},
    {0x26, "Lambda 3", "", 2, 0, LAMBDA, 0, 3, 0.97f, 1.03f},
    {0x27, "Lambda 4", "", 2, 0, LAMBDA, 0, 3, 0.97f, 1.03f},
    {0x28, "Lambda 5", "", 2, 0, LAMBDA, 0, 3, 0.97f, 1.03f},
    {0x29, "Lambda 6", "", 2, 0, LAMBDA, 0, 3, 0.97f, 1.03f},
    {0x2A, "Lambda 7", "", 2, 0, LAMBDA, 0, 3, 0.97f, 1.03f},
    {0x2B, "Lambda 8", "", 2, 0, LAMBDA, 0, 3, 0.97f, 1.03f},
    {0x34, "Lambda 1", "", 2, 0, LAMBDA, 0, 3, 0.97f, 1.03f},
    {0x35, "Lambda 2", "", 2, 0, LAMBDA, 0, 3, 0.97f, 1.03f},
    {0x36, "Lambda 3", "", 2, 0, LAMBDA, 0, 3, 0.97f, 1.03f},
    {0x37, "Lambda 4", "", 2, 0, LAMBDA, 0, 3, 0.97f, 1.03f},
    {0x38, "Lambda 5", "", 2, 0, LAMBDA, 0, 3, 0.97f, 1.03f},
    {0x39, "Lambda 6", "", 2, 0, LAMBDA, 0, 3, 0.97f, 1.03f},
    {0x3A, "Lambda 7", "", 2, 0, LAMBDA, 0, 3, 0.97f, 1.03f},
    {0x3B, "Lambda 8", "", 2, 0, LAMBDA, 0, 3, 0.97f, 1.03f},
    {0x44, "Lambda cmd", "", 2, 0, LAMBDA, 0, 3, 0.98f, 1.02f},
    {0x3C, "Cat temp 1", "*C", 2, 0, 0.1f, -40, 0, 450, 700},
    {0x3D, "Cat temp 2", "*C", 2, 0, 0.1f, -40, 0, 450, 700},
    {0x3E, "Cat temp 3", "*C", 2, 0, 0.1f, -40, 0, 400, 600},
    {0x3F, "Cat temp 4", "*C", 2, 0, 0.1f, -40, 0, 400, 600},
    {0x43, "Total load", "%", 2, 0, PCT, 0, 0, 15, 80},
    {0x45, "Rel throttle", "%", 1, 0, PCT, 0, 0, 0, 60},
    {0x47, "Throttle B", "%", 1, 0, PCT, 0, 0, 12, 70},
    {0x48, "Throttle C", "%", 1, 0, PCT, 0, 0, 12, 70},
    {0x49, "Pedal D", "%", 1, 0, PCT, 0, 0, 15, 70},
    {0x4A, "Pedal E", "%", 1, 0, PCT, 0, 0, 7, 35},
    {0x4B, "Pedal F", "%", 1, 0, PCT, 0, 0, 7, 35},
    {0x4C, "Throttle cmd", "%", 1, 0, PCT, 0, 0, 5, 60},
    {0x5A, "Rel pedal", "%", 1, 0, PCT, 0, 0, 0, 60},
    {0x22, "Rail press", "kPa", 2, 0, 0.079f, 0, 0, 250, 350},
    {0x23, "Rail press", "kPa", 2, 0, 10, 0, 0, 3000, 12000},
    {0x59, "Rail press", "kPa", 2, 0, 10, 0, 0, 3000, 12000},
    {0x5D, "Injection", "*", 2, 0, 1.0f / 128, -210, 1, -10, 10},
    {0x2C, "EGR cmd", "%", 1, 0, PCT, 0, 0, 0, 30},
    {0x2D, "EGR error", "%", 1, 0, TRIM, -100, 1, -5, 5},
    {0x2E, "Evap purge", "%", 1, 0, PCT, 0, 0, 0, 40},
    {0x32, "Evap press", "Pa", 2, 1, 0.25f, 0, 1, -200, 200},
    {0x53, "Evap press", "kPa", 2, 0, 0.005f, 0, 2, 100, 101},
    {0x54, "Evap press", "Pa", 2, 1, 1, 0, 0, -200, 200},
    {0x55, "O2 short 1", "%", 1, 0, TRIM, -100, 1, -5, 5},
    {0x56, "O2 long 1", "%", 1, 0, TRIM, -100, 1, -5, 5},
    {0x57, "O2 short 2", "%", 1, 0, TRIM, -100, 1, -5, 5},
    {0x58, "O2 long 2", "%", 1, 0, TRIM, -100, 1, -5, 5},
    {0x52, "Ethanol", "%", 1, 0, PCT, 0, 0, 0, 20},
    {0x5B, "Hybrid batt", "%", 1, 0, PCT, 0, 0, 40, 80},
    {0x61, "Torque req", "%", 1, 0, 1, -125, 0, 0, 60},
    {0x62, "Torque", "%", 1, 0, 1, -125, 0, 0, 60},
    {0x63, "Max torque", "Nm", 2, 0, 1, 0, 0, 250, 250},
    {0x1F, "Run time", "s", 2, 0, 1, 0, 0, 0, 3600},
    {0x30, "Warm-ups", "", 1, 0, 1, 0, 0, 5, 5},
    {0x31, "Since clear", "km", 2, 0, 1, 0, 0, 1200, 1200},
    {0x21, "MIL distance", "km", 2, 0, 1, 0, 0, 0, 0},
    {0x4D, "MIL time", "min", 2, 0, 1, 0, 0, 0, 0},
    {0x4E, "Since clear", "min", 2, 0, 1, 0, 0, 900, 900},
};
static const int NPIDS = sizeof(PIDS) / sizeof(PIDS[0]);

static float pidValues[NPIDS];

static int pidIndex(uint8_t pid) {
  for (int i = 0; i < NPIDS; i++) if (PIDS[i].pid == pid) return i;
  return -1;
}

static float pidDecode(const PidDef &d, const uint8_t *b) {
  float raw = d.bytes == 1 ? b[0] : d.sgn ? (float)(int16_t)((b[0] << 8) | b[1]) : (float)((b[0] << 8) | b[1]);
  return raw * d.mul + d.add;
}

static void clearPidValues() {
  for (auto &v : pidValues) v = NAN;
}

// Highlight values that need attention.
static uint16_t pidColor(uint8_t pid, float v) {
  switch (pid) {
    case 0x05: return v > 105 ? TFT_RED : v < 60 ? TFT_SKYBLUE : TFT_WHITE;
    case 0x5C: return v > 130 ? TFT_RED : TFT_WHITE;
    case 0x42: return v < 12.0f ? TFT_ORANGE : v > 14.8f ? TFT_RED : TFT_GREEN;
    case 0x2F: return v < 15 ? TFT_RED : TFT_WHITE;
    case 0x06: case 0x07: case 0x08: case 0x09: return fabsf(v) > 10 ? TFT_ORANGE : TFT_WHITE;
  }
  return TFT_WHITE;
}
