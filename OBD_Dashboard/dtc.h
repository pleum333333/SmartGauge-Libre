// SmartGauge Libre — Copyright (c) 2026 khanatuch — code written by khanatuch & Claude (AI)
// SPDX-License-Identifier: CC-BY-NC-SA-4.0
// Diagnostic trouble codes: decode mode 03/07 responses + short descriptions.
#pragma once
#include <Arduino.h>
#include <vector>

struct DtcReport {
  bool valid = false;  // a read has completed
  bool mil = false;    // check-engine lamp
  int milCount = 0;    // count reported by PID 01
  std::vector<String> stored, pending;
  String error;
  uint32_t version = 0;  // bumped on every change, so the UI knows to redraw
};

static String dtcName(uint8_t a, uint8_t b) {
  static const char L[] = "PCBU";
  char s[8];
  snprintf(s, sizeof(s), "%c%X%X%02X", L[a >> 6], (a >> 4) & 3, a & 0xF, b);
  return s;
}

static bool allHex(const String &s) {
  for (char c : s) if (!isxdigit(c)) return false;
  return s.length() > 0;
}

// `data` is hex pairs A B A B ...; reads at most `max` codes (-1 = all).
static void dtcAddCodes(const String &data, int max, std::vector<String> &out) {
  for (int i = 0; i + 4 <= (int)data.length() && max != 0; i += 4) {
    uint8_t a = strtoul(data.substring(i, i + 2).c_str(), nullptr, 16);
    uint8_t b = strtoul(data.substring(i + 2, i + 4).c_str(), nullptr, 16);
    if (max > 0) max--;
    if (a == 0 && b == 0) continue;
    String n = dtcName(a, b);
    bool dup = false;
    for (auto &o : out) dup |= o == n;
    if (!dup) out.push_back(n);
  }
}

// svc is "43" (mode 03) or "47" (mode 07). CAN responses carry a code count after
// the service byte and may be split over "0:" "1:" frames; older protocols send
// 3 codes per line padded with 0000. Returns false if the response is unusable.
static bool parseDtcResponse(String r, const char *svc, bool can, std::vector<String> &out) {
  r.toUpperCase();
  r.replace(" ", "");
  if (r.indexOf("NODATA") >= 0) return true;

  bool sawAny = false;
  String multi;
  int start = 0;
  while (start <= (int)r.length()) {
    int end = start;
    while (end < (int)r.length() && r[end] != '\r' && r[end] != '\n') end++;
    String l = r.substring(start, end);
    start = end + 1;
    if (l.isEmpty() || l.startsWith("SEARCHING")) continue;
    if (l.length() >= 2 && l[1] == ':' && isxdigit(l[0])) { multi += l.substring(2); continue; }
    if (l.length() == 3 && allHex(l)) continue;  // multi-frame byte count
    if (!l.startsWith(svc) || !allHex(l)) continue;
    sawAny = true;
    if (can) {
      int n = strtoul(l.substring(2, 4).c_str(), nullptr, 16);
      dtcAddCodes(l.substring(4), n, out);
    } else {
      dtcAddCodes(l.substring(2), -1, out);
    }
  }
  if (multi.startsWith(svc) && allHex(multi)) {
    sawAny = true;
    int n = strtoul(multi.substring(2, 4).c_str(), nullptr, 16);
    dtcAddCodes(multi.substring(4), n, out);
  }
  return sawAny;
}

// Generic (SAE) meanings for common codes; manufacturer codes (P1xxx etc.) are not listed.
static const char *dtcDescription(const String &code) {
  struct D { const char *c, *d; };
  static const D table[] = {
      {"P0100", "MAF circuit"},            {"P0101", "MAF range/performance"},
      {"P0102", "MAF circuit low"},        {"P0103", "MAF circuit high"},
      {"P0105", "MAP circuit"},            {"P0106", "MAP range/performance"},
      {"P0110", "Intake air temp circuit"},{"P0115", "Coolant temp circuit"},
      {"P0116", "Coolant temp range"},     {"P0117", "Coolant temp low"},
      {"P0118", "Coolant temp high"},      {"P0120", "Throttle pos sensor A"},
      {"P0121", "TPS range/performance"},  {"P0122", "TPS circuit low"},
      {"P0123", "TPS circuit high"},       {"P0125", "Coolant temp low for closed loop"},
      {"P0128", "Thermostat (coolant below temp)"},
      {"P0130", "O2 sensor B1S1 circuit"}, {"P0131", "O2 B1S1 low voltage"},
      {"P0132", "O2 B1S1 high voltage"},   {"P0133", "O2 B1S1 slow response"},
      {"P0134", "O2 B1S1 no activity"},    {"P0135", "O2 B1S1 heater"},
      {"P0136", "O2 sensor B1S2 circuit"}, {"P0141", "O2 B1S2 heater"},
      {"P0171", "System too lean B1"},     {"P0172", "System too rich B1"},
      {"P0174", "System too lean B2"},     {"P0175", "System too rich B2"},
      {"P0201", "Injector cyl 1 circuit"}, {"P0202", "Injector cyl 2 circuit"},
      {"P0203", "Injector cyl 3 circuit"}, {"P0204", "Injector cyl 4 circuit"},
      {"P0300", "Random/multiple misfire"},{"P0301", "Misfire cylinder 1"},
      {"P0302", "Misfire cylinder 2"},     {"P0303", "Misfire cylinder 3"},
      {"P0304", "Misfire cylinder 4"},     {"P0305", "Misfire cylinder 5"},
      {"P0306", "Misfire cylinder 6"},     {"P0325", "Knock sensor 1 circuit"},
      {"P0335", "Crankshaft position sensor A"},
      {"P0340", "Camshaft position sensor A"},
      {"P0351", "Ignition coil A circuit"},{"P0352", "Ignition coil B circuit"},
      {"P0353", "Ignition coil C circuit"},{"P0354", "Ignition coil D circuit"},
      {"P0400", "EGR flow"},               {"P0401", "EGR flow insufficient"},
      {"P0420", "Catalyst efficiency low B1"},
      {"P0430", "Catalyst efficiency low B2"},
      {"P0440", "EVAP system"},            {"P0441", "EVAP incorrect purge flow"},
      {"P0442", "EVAP small leak"},        {"P0446", "EVAP vent control"},
      {"P0455", "EVAP large leak"},        {"P0456", "EVAP very small leak"},
      {"P0500", "Vehicle speed sensor"},   {"P0505", "Idle air control system"},
      {"P0506", "Idle rpm lower than expected"},
      {"P0507", "Idle rpm higher than expected"},
      {"P0562", "System voltage low"},     {"P0563", "System voltage high"},
      {"P0600", "Serial comm link"},       {"P0606", "ECM processor fault"},
      {"P0700", "Transmission control system"},
      {"P0715", "Input/turbine speed sensor"},
      {"P0720", "Output speed sensor"},
  };
  for (auto &e : table) if (code == e.c) return e.d;
  return code.length() > 1 && code[1] == '0' ? "Generic code" : "Manufacturer-specific";
}
