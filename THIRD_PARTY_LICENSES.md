# Third-party licenses

The firmware is built on these components. Each keeps its own license; our
GPL-3.0-or-later license covers only the code written for SmartGauge Libre.
The last column says whether the component may be combined with
GPL-3.0-or-later code (checked 17 Oct 2026, see docs/10-licensing.md).

| Component | Version tested | License | GPL-3.0 compatible? | Source |
|---|---|---|---|---|
| Arduino-ESP32 core | 3.3.10 | LGPL-2.1 (core), some files Apache-2.0 | ✅ LGPL-2.1 allows linking into GPL-3.0 works; Apache-2.0 is one-way compatible with GPL-3.0 | https://github.com/espressif/arduino-esp32 |
| ESP-IDF (inside the core) | as bundled | Apache-2.0, plus FreeRTOS (MIT), lwIP (BSD), mbedTLS (Apache-2.0) and others | ✅ | https://github.com/espressif/esp-idf |
| ESP32 BLE library (bundled with the core) on the NimBLE host stack — core 3.3.10 builds the ESP32-S3 with `CONFIG_BT_NIMBLE_ENABLED=y`, not Bluedroid | bundled with the core | Apache-2.0 (library and Apache Mynewt NimBLE) | ✅ one-way: Apache-2.0 code may go into a GPL-3.0 work | https://github.com/espressif/arduino-esp32/tree/master/libraries/BLE · https://github.com/apache/mynewt-nimble |
| LovyanGFX | 1.2.28 | FreeBSD (BSD-2-Clause), includes parts under MIT / BSD from Adafruit_GFX, Adafruit_ILI9341, TFT_eSPI | ✅ permissive | https://github.com/lovyan03/LovyanGFX/blob/master/license.txt |
| FreeSans fonts (Adafruit GFX font format, via LovyanGFX GFXFF) | – | GNU FreeFont: GPL-3.0-or-later with font exception | ✅ same license family | https://www.gnu.org/software/freefont/license.html |
| Python: pyserial, Pillow (tools/screenshot.py only) | 3.5 / 10+ | BSD-3-Clause / HPND (MIT-CMU) | ✅ permissive | https://github.com/pyserial/pyserial · https://github.com/python-pillow/Pillow |

## When you distribute firmware binaries (`.bin`)

GPL-3.0 sections 4–6 apply to every copy you give or sell. Ship these alongside the binary (in the release zip):

1. This file, `LICENSE`, and `NOTICE`.
2. The full license texts of LGPL-2.1, Apache-2.0, BSD-2-Clause, MIT, and the GNU FreeFont license.
3. The complete corresponding source code of that exact version, or a written offer / link to it
   (this repository at the matching tag). This also satisfies LGPL-2.1, which requires that users can
   rebuild the firmware with a modified core.

> TODO before the first binary release: copy the full license texts listed in step 2 into a `licenses/` folder.
