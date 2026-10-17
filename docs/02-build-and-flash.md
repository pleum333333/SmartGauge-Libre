# 02 · การคอมไพล์และแฟลช (Build & Flash)

## สิ่งที่ต้องมี

| เครื่องมือ | เวอร์ชันที่ทดสอบแล้ว |
|---|---|
| arduino-cli (หรือ Arduino IDE 2.x) | 1.4.1 |
| Core `esp32:esp32` (Arduino-ESP32) | 3.3.10 |
| ไลบรารี LovyanGFX | 1.2.28 |
| (ทางเลือก) esptool | 4.8.1 |

ไลบรารีอื่นที่ใช้ (BLE, Wire, SPI, SD) มากับ core อยู่แล้ว ไม่ต้องลงเพิ่ม

```bash
arduino-cli core install esp32:esp32@3.3.10
```

```bash
arduino-cli lib install "LovyanGFX@1.2.28"
```

## ตั้งค่าบอร์ด (สำคัญ — ผิดแล้วจอไม่ติด/บูตไม่ขึ้น)

| ตัวเลือก | ค่า | เหตุผล |
|---|---|---|
| Board | ESP32S3 Dev Module (`esp32:esp32:esp32s3`) | |
| PSRAM | **OPI PSRAM** (`PSRAM=opi`) | framebuffer จอ 255 KB อยู่ใน PSRAM |
| Flash Size | 16MB (`FlashSize=16M`) | |
| Partition | `app3M_fat9M_16MB` | แอปใหญ่ ~800 KB และเผื่อพื้นที่ FAT ไว้ใช้ภายหลัง |
| USB CDC On Boot | Disabled (`CDCOnBoot=default`) | Serial ออกทาง CH340 (UART0) ไม่ใช่ USB native |

FQBN เต็ม:

```
esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=default
```

## คอมไพล์ + แฟลชด้วย arduino-cli

```bash
arduino-cli compile --fqbn esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=default --upload -p COM3 OBD_Dashboard
```

เปลี่ยน `COM3` เป็นพอร์ตของเครื่องคุณ (Linux/macOS เป็น `/dev/ttyUSB0` หรือ `/dev/cu.wchusbserial*`)

## ดู Serial log

baud **115200** — ใช้ Serial Monitor ของ Arduino IDE หรือ:

```bash
arduino-cli monitor -p COM3 -c baudrate=115200
```

log ที่ควรเห็นตอนบูต:

```
== OBD Dashboard ==
BLE ok
83 values available, 14 pages
display ok, free heap 125432, free PSRAM 7760152
BLE scan start
BLE scan: 6 devices
  ...
```

ทุกคำสั่งที่ส่งให้ ELM327 จะพิมพ์ออกมาเป็น `> 010C   < 410C1AF8` ไว้ดีบัก

## ปัญหาที่เจอบ่อยตอนแฟลช

- **อัปโหลดไม่เข้า / พอร์ตหาย ๆ โผล่ ๆ** → บอร์ดกำลังรีบูตวนเพราะไฟไม่พอ (ดู [01-hardware.md](01-hardware.md#ไฟเลี้ยง--อ่านก่อนใช้งาน-))
  ลองอัปโหลดซ้ำหลายครั้ง (esptool จะจับจังหวะได้เอง) หรือกดปุ่ม BOOT ค้างแล้วกด RST
- **คอมไพล์ผ่านแต่จอดำ** → ลืมตั้ง `PSRAM=opi`
- **`'link' was not declared`** หรือชื่อชนแปลก ๆ → อย่าตั้งชื่อตัวแปร global ว่า `link`, `read`, `write` ฯลฯ (ชนกับฟังก์ชัน POSIX)

## เฟิร์มแวร์ทดสอบฮาร์ดแวร์

โฟลเดอร์ [`ESP32_4827S043_Test/`](../ESP32_4827S043_Test/) เป็นสเก็ตช์แยกสำหรับเช็คบอร์ดใหม่:
Flash, PSRAM (เขียน/อ่าน 1 MB), สีจอ + gradient + fill rate, ไฟจอ PWM, Touch GT911 (สแกน I2C), microSD (เขียน/อ่านไฟล์)
ผลจะขึ้นบนจอและ Serial เป็น PASS/FAIL ใช้ FQBN เดียวกัน
