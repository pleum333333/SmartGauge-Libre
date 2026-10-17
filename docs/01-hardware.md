# 01 · ฮาร์ดแวร์ (Hardware)

## บอร์ดที่ใช้: Sunton ESP32-4827S043

บอร์ดจอสัมผัส 4.3 นิ้วราคาถูกจากจีน (ชื่อเล่นในวงการ "Cheap Yellow Display" ตระกูลจอใหญ่)
รุ่นที่เราใช้ทดสอบคือรุ่น **C = จอสัมผัสแบบ capacitive (GT911)** ส่วนรุ่น **R = resistive (XPT2046)** จะใช้ขาสัมผัสคนละชุด
(ดูหัวข้อ "ถ้าเป็นรุ่น R" ด้านล่าง)

ค่าที่อ่านได้จากบอร์ดจริงด้วย `esptool flash_id`:

| รายการ | ค่า |
|---|---|
| ชิป | ESP32-S3 (QFN56) rev v0.1, 2 คอร์, 240 MHz |
| PSRAM | 8 MB (Octal / OPI) |
| Flash | 16 MB (quad) |
| USB-Serial | CH340 (VID 1A86 / PID 7523) → ขึ้นเป็น COMx บน Windows |
| จอ | 480×272 RGB565 แบบ parallel RGB (ไม่มีชิปไดรเวอร์ ภาพทั้งหมดอยู่ใน framebuffer บน PSRAM) |
| Touch | GT911 I2C address **0x14** (บางล็อตเป็น 0x5D — โค้ดตรวจให้อัตโนมัติ) |
| ไร้สาย | Wi-Fi 2.4 GHz + **Bluetooth LE เท่านั้น** (ESP32-S3 ไม่มี Bluetooth Classic) |

> ⚠️ **สำคัญมาก:** ESP32-S3 ต่อกับ ELM327 แบบ Bluetooth Classic (SPP) ไม่ได้
> ต้องใช้ตัว ELM327 ที่เป็น **BLE** เท่านั้น (เช็คด้วยแอป nRF Connect — ถ้าสแกนเจอในแอปนี้ แปลว่าเป็น BLE)
> ตัว ELM327 สีฟ้าราคาถูกที่ต้องใส่ PIN 1234 ส่วนใหญ่เป็น Classic ใช้ไม่ได้

## Pinout

### จอ RGB (ขา 16 บิต + sync)

| สัญญาณ | GPIO |
|---|---|
| B0..B4 | 8, 3, 46, 9, 1 |
| G0..G5 | 5, 6, 7, 15, 16, 4 |
| R0..R4 | 45, 48, 47, 21, 14 |
| DE (henable) | 40 |
| VSYNC | 41 |
| HSYNC | 39 |
| PCLK | 42 |
| Backlight (PWM) | 2 |

Timing ที่ใช้ (อยู่ใน [`board.h`](../OBD_Dashboard/board.h)):

| ค่า | H | V |
|---|---|---|
| polarity | 0 | 0 |
| front porch | 8 | 8 |
| pulse width | 4 | 4 |
| back porch | 43 | 12 |

`pclk = 9 MHz`, `pclk_active_neg = 1`

อัตรารีเฟรชของจอ = 9 MHz ÷ ((480+8+4+43) × (272+8+4+12)) = 9,000,000 ÷ (535 × 296) ≈ **56.8 Hz**
→ นี่คือ **เพดาน FPS ที่ตาเห็นได้จริง** วาดเร็วกว่านี้ก็ไม่ได้ขึ้นจอ

### Touch GT911 (I2C)

| สัญญาณ | GPIO |
|---|---|
| SDA | 19 |
| SCL | 20 |
| RST | 38 |
| INT | ไม่ได้ใช้ (อ่านแบบ polling) |

### microSD (SPI) — ยังไม่ได้ใช้ในเฟิร์มแวร์ แต่ทดสอบขาแล้ว

| สัญญาณ | GPIO |
|---|---|
| CS | 10 |
| MOSI | 11 |
| SCK | 12 |
| MISO | 13 |

### ถ้าเป็นรุ่น R (resistive XPT2046)

ใช้ SPI ร่วมกับ SD: CS 38, SCK 12, MOSI 11, MISO 13 (ยังไม่ได้ทำไดรเวอร์ในโปรเจกต์นี้ — ยินดีรับ PR)

## ไฟเลี้ยง — อ่านก่อนใช้งาน ⚡

เราเจอปัญหานี้จริงตอนพัฒนา:

- บอร์ด**รีบูตวนไปมา** จอกระพริบ และ COM port หายจาก Windows
- log บอกว่า `rst:0x1 (POWERON)` = **ไฟดับจริง** ไม่ใช่โปรแกรมพัง
- สาเหตุ: ตอนเปิด BLE (radio calibration) **พร้อมกับ** ไฟจอ 100% กินกระแสพุ่งเกินที่พอร์ต USB ของคอมจ่ายไหว

วิธีแก้ที่ใส่ไว้ในโค้ดแล้ว:

1. เปิด BLE **ก่อน** เปิดไฟจอ แล้วค่อย ๆ fade ไฟจอขึ้น
2. ไฟจอดีฟอลต์ `BACKLIGHT = 90` (จาก 255) ใน [`OBD_Dashboard.ino`](../OBD_Dashboard/OBD_Dashboard.ino)
3. ลด BLE TX power (`ESP_PWR_LVL_N0`) — ตัว OBD อยู่ใกล้จออยู่แล้ว
4. สแกน BLE แบบ duty 50% (`setWindow(50)` / `setInterval(100)`)

**ในรถ:** ใช้ที่ชาร์จ USB ในรถที่จ่ายได้ **5V ≥ 2A** แล้วจะเพิ่ม `BACKLIGHT` เป็น 150–200 ได้สบาย
ถ้าเจออาการรีบูตวนอีก ให้ลดไฟจอก่อนเป็นอย่างแรก

## ELM327 BLE ที่รองรับ

โค้ดตรวจหาเองอัตโนมัติจาก:

- ชื่อที่มีคำว่า `obd`, `elm`, `vlink`, `v-link`, `vlinker`, `vgate`, `icar`, `konnwei`, `veepeak`, `carista`
- หรือประกาศ service UUID `FFF0`, `FFE0`, `18F0` (3 แบบที่ dongle BLE ใช้กันเยอะที่สุด)

ถ้าของคุณชื่อแปลก ๆ ให้ตั้ง `#define OBD_DEVICE_NAME "ชื่อหรือ MAC"` ไว้บนสุดของไฟล์ `.ino`
รายละเอียดดู [04-elm327-ble.md](04-elm327-ble.md)
