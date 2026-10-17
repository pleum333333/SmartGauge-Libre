// SmartGauge Libre — Copyright (c) 2026 khanatuch — code written by khanatuch & Claude (AI)
// SPDX-License-Identifier: GPL-3.0-or-later
// ELM327 over BLE: scan, connect, auto-detect the UART-like service, AT/OBD commands.
// Most BLE OBD dongles expose FFF0 (FFF1 notify / FFF2 write), FFE0 (FFE1 both)
// or 18F0 (2AF0 notify / 2AF1 write). Rather than hard-coding one, we pick the first
// non-standard service that has a notify and a write characteristic.
#pragma once
#include <BLEDevice.h>
#include <freertos/stream_buffer.h>
#include "dtc.h"

// Set to the adapter's advertised name (or MAC "aa:bb:cc:dd:ee:ff") to skip auto-detection.
#ifndef OBD_DEVICE_NAME
#define OBD_DEVICE_NAME ""
#endif

static StreamBufferHandle_t g_elmRx = nullptr;
static volatile bool g_elmLinkUp = false;

static void elmNotify(BLERemoteCharacteristic *, uint8_t *data, size_t len, bool) {
  if (g_elmRx) xStreamBufferSend(g_elmRx, data, len, 0);
}

class ElmClientCb : public BLEClientCallbacks {
  void onConnect(BLEClient *) override {}
  void onDisconnect(BLEClient *) override { g_elmLinkUp = false; }
};

class Elm327Ble {
public:
  String deviceName;
  String protocol;
  String version;
  uint32_t pidMask[4] = {0, 0, 0, 0};  // supported PIDs 01-20, 21-40, 41-60, 61-80
  bool isCan = true;

  void begin() {
    g_elmRx = xStreamBufferCreate(2048, 1);
    BLEDevice::init("OBD-Dash");
    // The dongle sits within a metre of the screen; low TX power cuts the current spikes.
    BLEDevice::setPower(ESP_PWR_LVL_N0);
  }

  bool linked() const { return g_elmLinkUp; }

  // Scan ~5 s and connect to the best-looking OBD adapter.
  bool scanAndConnect() {
    BLEScan *scan = BLEDevice::getScan();
    scan->setActiveScan(true);
    scan->setInterval(100);
    scan->setWindow(50);  // 50% radio duty: lower average current on weak USB power
    Serial.println("BLE scan start");
    auto results = scan->start(5, false);

    int best = -1, bestRssi = -999;
    Serial.printf("BLE scan: %d devices\n", results->getCount());
    for (int i = 0; i < results->getCount(); i++) {
      BLEAdvertisedDevice d = results->getDevice(i);
      String name = d.getName().c_str();
      String mac = d.getAddress().toString().c_str();
      bool match = isObd(d, name, mac);
      Serial.printf("  %s  rssi %d  \"%s\"%s\n", mac.c_str(), d.getRSSI(), name.c_str(), match ? "  <- OBD?" : "");
      if (match && d.getRSSI() > bestRssi) { best = i; bestRssi = d.getRSSI(); }
    }
    if (best < 0) { scan->clearResults(); return false; }

    target = results->getDevice(best);
    deviceName = target.getName().c_str();
    if (deviceName.isEmpty()) deviceName = target.getAddress().toString().c_str();
    scan->clearResults();

    Serial.printf("Connecting to %s ...\n", deviceName.c_str());
    if (!client) {
      client = BLEDevice::createClient();
      client->setClientCallbacks(new ElmClientCb());
    }
    if (!client->connect(&target)) { Serial.println("connect failed"); return false; }
    g_elmLinkUp = true;
    if (!findUart()) {
      Serial.println("no notify+write characteristic pair found");
      client->disconnect();
      g_elmLinkUp = false;
      return false;
    }
    return true;
  }

  void disconnect() {
    if (client && client->isConnected()) client->disconnect();
    g_elmLinkUp = false;
  }

  // Send one command, return everything up to the '>' prompt.
  String command(const char *cmd, uint32_t timeoutMs = 1500) {
    if (!g_elmLinkUp || !txChar) return "";
    xStreamBufferReset(g_elmRx);
    String out = String(cmd) + "\r";
    txChar->writeValue((uint8_t *)out.c_str(), out.length(), txWithResponse);

    String resp;
    uint32_t t0 = millis();
    bool gotPrompt = false;
    while (!gotPrompt && millis() - t0 < timeoutMs && g_elmLinkUp) {
      uint8_t buf[64];
      size_t n = xStreamBufferReceive(g_elmRx, buf, sizeof(buf), pdMS_TO_TICKS(10));
      for (size_t i = 0; i < n; i++) {
        if (buf[i] == '>') { gotPrompt = true; break; }
        resp += (char)buf[i];
      }
    }
    resp.trim();
    Serial.printf("> %-6s < %s\n", cmd, oneLine(resp).c_str());
    return resp;
  }

  // Adapter-level setup (does not need the ECU).
  bool initAdapter() {
    version = command("ATZ", 3000);
    if (version.isEmpty()) version = command("ATI", 1500);
    if (version.isEmpty()) return false;
    version = oneLine(version);
    version.replace("ATZ", "");
    version.trim();
    command("ATE0");   // echo off
    command("ATL0");   // linefeeds off
    command("ATS0");   // spaces off
    command("ATH0");   // headers off
    command("ATAT1");  // adaptive timing
    command("ATSP0");  // auto protocol
    return true;
  }

  // Talk to the car's ECU; reads the supported-PID bitmasks. Needs ignition on.
  bool connectEcu() {
    uint8_t b[4];
    if (!queryPid(0x00, b, 4, 10000)) return false;
    pidMask[0] = toU32(b);
    for (int k = 1; k < 4; k++) {
      pidMask[k] = 0;
      if ((pidMask[k - 1] & 1) && queryPid(k * 0x20, b, 4)) pidMask[k] = toU32(b);
    }
    protocol = oneLine(command("ATDP"));
    protocol.replace("AUTO, ", "");
    // ATDPN: "6".."C" (optionally prefixed "A") are the CAN protocols
    String pn = oneLine(command("ATDPN"));
    char p = pn.length() ? pn[pn.length() - 1] : '6';
    isCan = (p >= '6' && p <= '9') || (p >= 'A' && p <= 'C');
    Serial.printf("PID masks: %08lX %08lX %08lX %08lX  CAN=%d\n", (unsigned long)pidMask[0],
                  (unsigned long)pidMask[1], (unsigned long)pidMask[2], (unsigned long)pidMask[3], isCan);
    return true;
  }

  // Check-engine lamp + stored (mode 03) and pending (mode 07) trouble codes.
  bool readDtcs(DtcReport &rep) {
    rep.stored.clear();
    rep.pending.clear();
    rep.error = "";
    uint8_t b[4];
    if (queryPid(0x01, b, 4)) {
      rep.mil = b[0] & 0x80;
      rep.milCount = b[0] & 0x7F;
    }
    bool ok = parseDtcResponse(command("03", 5000), "43", isCan, rep.stored);
    parseDtcResponse(command("07", 5000), "47", isCan, rep.pending);
    if (!ok) rep.error = "ECU did not answer mode 03";
    rep.valid = true;
    rep.version++;
    return ok;
  }

  bool supports(uint8_t pid) const {
    if (pid == 0 || pid > 0x80) return false;
    int bank = (pid - 1) / 32;
    int bit = 31 - ((pid - 1) % 32);
    return pidMask[bank] & (1u << bit);
  }

  // Mode 01 request; copies `n` data bytes after "41 <pid>".
  bool queryPid(uint8_t pid, uint8_t *out, int n, uint32_t timeoutMs = 1000) {
    char cmd[6], key[6];
    snprintf(cmd, sizeof(cmd), "01%02X", pid);
    snprintf(key, sizeof(key), "41%02X", pid);
    String r = command(cmd, timeoutMs);
    r.toUpperCase();
    r.replace("SEARCHING...", "");
    String hex;
    for (char c : r) if (isxdigit(c)) hex += c;
    int i = hex.indexOf(key);
    if (i < 0) return false;
    i += 4;
    for (int k = 0; k < n; k++, i += 2) {
      if (i + 2 > (int)hex.length()) return false;
      out[k] = strtoul(hex.substring(i, i + 2).c_str(), nullptr, 16);
    }
    return true;
  }

  // Battery voltage measured by the adapter itself (works without ECU).
  float readAdapterVoltage() {
    String r = command("ATRV");
    float v = r.toFloat();
    return v > 1 ? v : NAN;
  }

private:
  BLEClient *client = nullptr;
  BLEAdvertisedDevice target;
  BLERemoteCharacteristic *txChar = nullptr;
  bool txWithResponse = true;

  static uint32_t toU32(const uint8_t *b) {
    return (uint32_t)b[0] << 24 | (uint32_t)b[1] << 16 | (uint32_t)b[2] << 8 | b[3];
  }

  static String oneLine(String s) {
    s.replace("\r", " ");
    s.replace("\n", " ");
    s.trim();
    return s;
  }

  static bool isObd(BLEAdvertisedDevice &d, String name, String mac) {
    String want = OBD_DEVICE_NAME;
    if (want.length()) return name.equalsIgnoreCase(want) || mac.equalsIgnoreCase(want);
    name.toLowerCase();
    const char *keys[] = {"obd", "elm", "vlink", "v-link", "vlinker", "vgate", "icar", "konnwei", "veepeak", "carista"};
    for (auto k : keys) if (name.indexOf(k) >= 0) return true;
    if (d.haveServiceUUID()) {
      const uint16_t svcs[] = {0xFFF0, 0xFFE0, 0x18F0};
      for (auto s : svcs) if (d.isAdvertisingService(BLEUUID(s))) return true;
    }
    return false;
  }

  bool findUart() {
    txChar = nullptr;
    auto *services = client->getServices();
    if (!services) return false;
    for (auto &sv : *services) {
      BLERemoteService *svc = sv.second;
      BLEUUID u = svc->getUUID();
      if (u.equals(BLEUUID((uint16_t)0x1800)) || u.equals(BLEUUID((uint16_t)0x1801)) ||
          u.equals(BLEUUID((uint16_t)0x180A)))
        continue;
      BLERemoteCharacteristic *rx = nullptr, *tx = nullptr;
      auto *chars = svc->getCharacteristics();
      if (!chars) continue;
      for (auto &cv : *chars) {
        BLERemoteCharacteristic *c = cv.second;
        if (!rx && (c->canNotify() || c->canIndicate())) rx = c;
        if (!tx && (c->canWrite() || c->canWriteNoResponse())) tx = c;
      }
      if (rx && tx) {
        Serial.printf("UART service %s  rx %s  tx %s\n", u.toString().c_str(),
                      rx->getUUID().toString().c_str(), tx->getUUID().toString().c_str());
        rx->registerForNotify(elmNotify, rx->canNotify());
        txChar = tx;
        txWithResponse = !tx->canWriteNoResponse();
        return true;
      }
    }
    return false;
  }
};
