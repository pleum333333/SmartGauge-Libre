// SmartGauge Libre — Copyright (c) 2026 khanatuch — code written by khanatuch & Claude (AI)
// SPDX-License-Identifier: GPL-3.0-or-later
// Sunton ESP32-4827S043 (ESP32-S3) board support: RGB panel + GT911 touch.
#pragma once
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <Wire.h>

class LGFX : public lgfx::LGFX_Device {
  lgfx::Bus_RGB _bus;
  lgfx::Panel_RGB _panel;
  lgfx::Light_PWM _light;

public:
  LGFX() {
    {
      auto cfg = _panel.config();
      cfg.memory_width = cfg.panel_width = 480;
      cfg.memory_height = cfg.panel_height = 272;
      cfg.offset_x = cfg.offset_y = 0;
      _panel.config(cfg);
    }
    {
      auto cfg = _panel.config_detail();
      cfg.use_psram = 1;
      _panel.config_detail(cfg);
    }
    {
      auto cfg = _bus.config();
      cfg.panel = &_panel;
      // B0..B4
      cfg.pin_d0 = 8;  cfg.pin_d1 = 3;  cfg.pin_d2 = 46; cfg.pin_d3 = 9;  cfg.pin_d4 = 1;
      // G0..G5
      cfg.pin_d5 = 5;  cfg.pin_d6 = 6;  cfg.pin_d7 = 7;  cfg.pin_d8 = 15; cfg.pin_d9 = 16; cfg.pin_d10 = 4;
      // R0..R4
      cfg.pin_d11 = 45; cfg.pin_d12 = 48; cfg.pin_d13 = 47; cfg.pin_d14 = 21; cfg.pin_d15 = 14;
      cfg.pin_henable = 40;
      cfg.pin_vsync = 41;
      cfg.pin_hsync = 39;
      cfg.pin_pclk = 42;
      cfg.freq_write = 9000000;
      cfg.hsync_polarity = 0; cfg.hsync_front_porch = 8; cfg.hsync_pulse_width = 4; cfg.hsync_back_porch = 43;
      cfg.vsync_polarity = 0; cfg.vsync_front_porch = 8; cfg.vsync_pulse_width = 4; cfg.vsync_back_porch = 12;
      cfg.pclk_active_neg = 1;
      cfg.de_idle_high = 0;
      cfg.pclk_idle_high = 0;
      _bus.config(cfg);
    }
    _panel.setBus(&_bus);
    {
      auto cfg = _light.config();
      cfg.pin_bl = 2;
      cfg.freq = 12000;
      cfg.pwm_channel = 7;
      _light.config(cfg);
    }
    _panel.light(&_light);
    setPanel(&_panel);
  }
};

// ---- GT911 capacitive touch (raw I2C, address auto-detected) ----
namespace touch {
static const int SDA_PIN = 19, SCL_PIN = 20, RST_PIN = 38;
static uint8_t addr = 0;

static bool probe(uint8_t a) {
  Wire.beginTransmission(a);
  return Wire.endTransmission() == 0;
}

static bool readReg(uint16_t reg, uint8_t *buf, size_t len) {
  Wire.beginTransmission(addr);
  Wire.write(reg >> 8);
  Wire.write(reg & 0xFF);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(addr, (uint8_t)len) != len) return false;
  for (size_t i = 0; i < len; i++) buf[i] = Wire.read();
  return true;
}

static void writeReg(uint16_t reg, uint8_t v) {
  Wire.beginTransmission(addr);
  Wire.write(reg >> 8);
  Wire.write(reg & 0xFF);
  Wire.write(v);
  Wire.endTransmission();
}

static bool begin() {
  pinMode(RST_PIN, OUTPUT);
  digitalWrite(RST_PIN, LOW);
  delay(10);
  digitalWrite(RST_PIN, HIGH);
  delay(60);
  Wire.begin(SDA_PIN, SCL_PIN, 400000);
  if (probe(0x5D)) addr = 0x5D;
  else if (probe(0x14)) addr = 0x14;
  return addr != 0;
}

// Returns true while a finger is down; x/y get the first point.
static bool read(int &x, int &y) {
  static bool down = false;
  static int lx = 0, ly = 0;
  if (!addr) return false;
  uint8_t st;
  if (!readReg(0x814E, &st, 1) || !(st & 0x80)) { x = lx; y = ly; return down; }
  int n = st & 0x0F;
  if (n > 0 && n <= 5) {
    uint8_t p[4];
    readReg(0x8150, p, 4);
    lx = p[0] | (p[1] << 8);
    ly = p[2] | (p[3] << 8);
  }
  down = n > 0 && n <= 5;
  writeReg(0x814E, 0);
  x = lx; y = ly;
  return down;
}
}  // namespace touch
