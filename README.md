# SmartGauge Libre

**เกจ OBD-II ฟรี สำหรับรถยนต์ — ESP32-S3 + จอสัมผัส 4.3" + ELM327 Bluetooth LE**
*Free OBD-II car gauge for ESP32-S3 with a 4.3" touchscreen and an ELM327 BLE adapter.*

code written by **khanatuch & Claude (AI)** · ไลเซนส์ [GPL-3.0-or-later](LICENSE) — **ฟรีและโอเพนซอร์ส ต้องให้เครดิต แจกหรือขายต่อต้องให้ซอร์สโค้ดเต็ม**

| | |
|---|---|
| ![RACE](docs/images/page-race.png) | ![DIGITAL](docs/images/page-digital.png) |
| ![TRIP](docs/images/page-trip.png) | ![TIMER](docs/images/page-timer.png) |
| ![GAUGE](docs/images/page-tach.png) | ![GRID](docs/images/page-grid.png) |
| ![ALARM](docs/images/alarm-card.png) | ![DTC](docs/images/page-dtc.png) |

## ทำไมถึงมีโปรเจกต์นี้

"สมาร์ทเกจ" ที่ขายกันทุกวันนี้ หลายเจ้าเป็นแค่ฮาร์ดแวร์ที่หาซื้อได้ทั่วไป + โค้ดที่สั่ง AI แต่งแล้วเอามาขาย
โปรเจกต์นี้ทำให้ทุกคนได้ของที่ดีกว่า **ฟรี** และพัฒนาไปด้วยกันในที่เดียว — ใครปรับปรุงแล้วดี ส่งกลับมารวมได้ ([CONTRIBUTING](CONTRIBUTING.md))

## ฟีเจอร์

- **7 แบบหน้าจอ** แตะเพื่อเปลี่ยนหน้า: RACE (สไตล์รถแข่ง คาร์บอน + shift light), DIGITAL (ตัวเลขสีส้มแบบ OLED), tach + 6 ค่า, trip computer A/B, จับเวลา 0–100 / 100–0, กริดค่าทั้งหมด, โค้ด error
- **บูสต์ค้างค่าสูงสุด** (กดค้างเพื่อรีเซ็ต) และ **เตือนต่ำ/สูงทุกค่า** การ์ดกระพริบแดง (ตั้งเกณฑ์ได้ เก็บในบอร์ด)
- อ่านค่ามาตรฐาน OBD-II **~80 ค่า** — แสดงเฉพาะค่าที่รถคันนั้นรองรับ (อัตโนมัติ)
- บูสต์, องศาจุดระเบิด, AFR, fuel trim, O2, MAF/MAP, อุณหภูมิน้ำ/น้ำมันเครื่อง/อากาศ/แคต, คันเร่ง, แรงบิด ฯลฯ
- อ่านโค้ด error (stored + pending) พร้อมคำอธิบาย + สถานะไฟ Check Engine
- ไฟเตือน 6 ดวง + shift light
- **60 fps** ลื่น ๆ — งาน OBD แยกไปคอร์ 0 จอไม่ค้างตอนรอรถตอบ
- ใช้ Bluetooth LE ในตัว ESP32-S3 ไม่ต้องต่อโมดูลเพิ่ม · โหมด DEMO ลองได้โดยไม่ต้องมีรถ
- ภาพหน้าจอผ่าน USB (`tools/screenshot.py`)

## ของที่ต้องใช้

| ของ | หมายเหตุ |
|---|---|
| บอร์ด **Sunton ESP32-4827S043C** (ESP32-S3, จอ 4.3" 480×272, touch GT911) | รุ่น C = จอสัมผัส capacitive |
| **ELM327 แบบ Bluetooth LE** (ไม่ใช่ Bluetooth Classic) | เช็คด้วยแอป nRF Connect — ต้องสแกนเจอ |
| สาย USB-C + ที่ชาร์จในรถ 5V ≥ 2A | ไฟไม่พอ = จอกระพริบ/รีบูต |
| รถยนต์ที่มีปลั๊ก OBD-II 16 ขา | มอเตอร์ไซค์ยังไม่รองรับ — ดู [docs/11](docs/11-vehicles-and-protocols.md) |

## เริ่มใช้งานเร็ว

```bash
arduino-cli core install esp32:esp32@3.3.10
```

```bash
arduino-cli lib install "LovyanGFX@1.2.28"
```

```bash
arduino-cli compile --fqbn esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=default --upload -p COM3 OBD_Dashboard
```

แตะปุ่ม **DEMO** มุมขวาบนเพื่อดูแบบไม่ต้องมีรถ · แตะฝั่งขวาของจอ = หน้าถัดไป, ขอบซ้าย = ย้อนกลับ

## เอกสาร

| | |
|---|---|
| [01 ฮาร์ดแวร์ + pinout + เรื่องไฟเลี้ยง](docs/01-hardware.md) | [07 UI และการทำให้ได้ 60 fps](docs/07-ui-and-performance.md) |
| [02 คอมไพล์และแฟลช](docs/02-build-and-flash.md) | [08 แก้ปัญหา](docs/08-troubleshooting.md) |
| [03 สถาปัตยกรรมโค้ด](docs/03-architecture.md) | [09 แผนพัฒนา (roadmap)](docs/09-roadmap.md) |
| [04 ELM327 ผ่าน BLE](docs/04-elm327-ble.md) | [10 ไลเซนส์และเครดิต](docs/10-licensing.md) |
| [05 ค่าเซ็นเซอร์ (PID)](docs/05-obd-pids.md) | [11 รถที่รองรับ / มอเตอร์ไซค์](docs/11-vehicles-and-protocols.md) |
| [06 โค้ด error (DTC)](docs/06-dtc.md) | |

## ไลเซนส์

[GPL-3.0-or-later](LICENSE) — ใช้ แก้ แจกได้ฟรี **ขายได้ แต่ผู้ซื้อต้องได้ซอร์สโค้ดเต็ม** ต้องให้เครดิตตาม [NOTICE](NOTICE) และเวอร์ชันที่แก้ต้องใช้ GPL-3.0 เหมือนกัน
ไลบรารีที่ใช้มีไลเซนส์ของตัวเอง — [THIRD_PARTY_LICENSES](THIRD_PARTY_LICENSES.md) · ชื่อโปรเจกต์ — [TRADEMARKS](TRADEMARKS.md)

> เป็น **open source** ตามนิยามของ OSI — เปลี่ยนจาก CC BY-NC-SA 4.0 เมื่อ 17 ต.ค. 2026 (สำเนาเก่าที่แจกไปแล้วยังเป็นไลเซนส์เดิม)
> ชื่อ "SmartGauge Libre" ห้ามใช้กับ fork หรือสินค้า — ดู [TRADEMARKS](TRADEMARKS.md) และ [docs/10-licensing.md](docs/10-licensing.md)

ผู้ร่วมสร้าง: [AUTHORS](AUTHORS.md)
