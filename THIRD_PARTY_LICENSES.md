# Third-party licenses

The firmware is built on these components. Each keeps its own license; our
CC BY-NC-SA 4.0 license covers only the code written for SmartGauge Libre.

| Component | Version tested | License | Source |
|---|---|---|---|
| Arduino-ESP32 core | 3.3.10 | LGPL-2.1 | https://github.com/espressif/arduino-esp32 |
| ESP-IDF (inside the core) | as bundled | Apache-2.0, plus FreeRTOS (MIT), lwIP (BSD), mbedTLS (Apache-2.0) and others | https://github.com/espressif/esp-idf |
| ESP32 BLE library (Bluedroid) | bundled with the core | Apache-2.0 | https://github.com/espressif/arduino-esp32/tree/master/libraries/BLE |
| LovyanGFX | 1.2.28 | FreeBSD (BSD-2-Clause), includes parts under MIT / BSD from Adafruit_GFX, Adafruit_ILI9341, TFT_eSPI | https://github.com/lovyan03/LovyanGFX/blob/master/license.txt |
| FreeSans fonts (via LovyanGFX GFXFF) | – | GNU FreeFont: GPLv3 with font exception | https://www.gnu.org/software/freefont/license.html |
| Python: pyserial, Pillow (tools/screenshot.py only) | 3.5 / 10+ | BSD-3-Clause / HPND (MIT-CMU) | https://github.com/pyserial/pyserial · https://github.com/python-pillow/Pillow |

## When you distribute firmware binaries (`.bin`)

Ship these alongside the binary (in the release zip):

1. This file, `LICENSE`, and `NOTICE`.
2. The full license texts of LGPL-2.1, Apache-2.0, BSD-2-Clause, MIT, and the GNU FreeFont license.
3. A link to the complete source code of the exact version (this repository / tag).
   LGPL-2.1 requires that users can rebuild the firmware with a modified core — publishing the full source satisfies this.

> TODO before the first binary release: copy the full license texts listed in step 2 into a `licenses/` folder.
> The font license has not been checked against the font files themselves — see docs/10-licensing.md.
