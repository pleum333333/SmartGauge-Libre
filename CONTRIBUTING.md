# Contributing / ร่วมพัฒนา

โปรเจกต์นี้ตั้งใจให้ **พัฒนาไปด้วยกันที่เดียว** ไม่ใช่ต่างคนต่าง fork ไปทำแล้วเอาไปขาย
ใครปรับปรุงแล้วได้ของดี (ไม่ว่าจะเขียนเอง หรือให้ AI ช่วยเขียน) ส่งกลับมาเป็น Pull Request ได้เลย
ฟีเจอร์ที่ดีจะถูกรวมเข้า repo หลัก และชื่อคุณจะอยู่ใน [AUTHORS.md](AUTHORS.md)

This project is meant to improve in one place. If you make it better — by hand or with an AI assistant — please send it back as a pull request.

## ขอความร่วมมือ (คำขอ ไม่ใช่เงื่อนไขไลเซนส์)

ไลเซนส์ GPL-3.0-or-later บังคับว่า **ถ้าแจกหรือขาย** เวอร์ชันที่แก้ ต้องใช้ GPL-3.0 เหมือนกันและผู้รับต้องได้ซอร์สโค้ดเต็ม
นอกจากนั้นเราขอ:

1. **เผยแพร่ซอร์สเป็นสาธารณะ** (เช่น fork บน GitHub) ไม่ใช่ให้เฉพาะคนที่ได้ไฟล์ `.bin`
2. **ส่ง PR กลับมา** สำหรับการแก้ไขที่คนอื่นได้ประโยชน์
3. บอกว่าเปลี่ยนอะไร ในไฟล์ NOTICE ของคุณ ("changes made: …")

## วิธีส่ง Pull Request

1. Fork → สร้าง branch ชื่อสื่อความหมาย (`feature/boost-peak-hold`, `fix/dtc-kwp-parse`)
2. คอมไพล์ผ่านด้วย FQBN ใน [docs/02-build-and-flash.md](docs/02-build-and-flash.md)
3. ทดสอบบนบอร์ดจริง — ถ้าแก้ UI ให้แนบภาพจาก `tools/screenshot.py` **และ** รูปถ่ายจอจริง (ฟอนต์บางบนจอจริงไม่เหมือนภาพ framebuffer)
4. ถ้าแก้ UI ต้องยังได้ ~60 fps (ตัวเลข FPS บนแถบด้านบน) — ดูกฎใน [docs/07-ui-and-performance.md](docs/07-ui-and-performance.md)
5. ถ้าเพิ่ม PID เฉพาะรุ่นรถ บอกรุ่น ปี เครื่องยนต์ และแนบ log จาก Serial ที่ยืนยันว่าใช้ได้จริง
6. อัปเดตเอกสารใน `docs/` ถ้าพฤติกรรมเปลี่ยน
7. ใช้ AI ช่วยได้เต็มที่ — แต่ **คุณต้องอ่านและทดสอบโค้ดเองก่อนส่ง** และบอกใน PR ว่าใช้ AI ส่วนไหน

## สไตล์โค้ด

- header-only, `static`, ไม่มี dynamic allocation ในลูปวาด
- ห้ามเรียก `elm.*` จาก UI (core 1) และห้ามวาดจอจาก `obdTask` (core 0) — ดู [docs/03-architecture.md](docs/03-architecture.md)
- ห้ามใช้ `Font2` / ฟอนต์เส้นบาง; เว้นขอบจอ ≥ 8 px
- คอมเมนต์อธิบาย "ทำไม" ไม่ใช่ "ทำอะไร"
- ทุกไฟล์ใหม่ใส่หัว:
  ```cpp
  // SmartGauge Libre — Copyright (c) 2026 khanatuch — code written by khanatuch & Claude (AI)
  // SPDX-License-Identifier: GPL-3.0-or-later
  ```

## สิทธิ์ในโค้ดที่ส่งมา

การส่ง PR ถือว่าคุณยืนยันว่า:
- คุณมีสิทธิ์ส่งโค้ดนั้น (เขียนเอง / ให้ AI ช่วยเขียนภายใต้การกำกับของคุณ / มาจากแหล่งที่ไลเซนส์ใช้ร่วมกันได้)
- **ไม่ได้ลอกโค้ดจากโปรเจกต์ที่ห้ามนำไปใช้** (ไม่มีไฟล์ไลเซนส์, ห้ามขาย เช่น Commons Clause / PolyForm NC / "MIT No Commercial Use", หรือ GPL-2.0-only) — ใช้เป็นไอเดียได้ แต่เขียนใหม่เอง
  โค้ด MIT / BSD / Apache-2.0 / LGPL / GPL-3.0 / AGPL-3.0 ใช้ได้ ถ้าคงหัวไฟล์เดิมและบอกที่มาใน PR — ดู [docs/10-licensing.md](docs/10-licensing.md)
- โค้ดที่ส่งมา **เข้ามาภายใต้ GPL-3.0-or-later** (inbound = outbound) และยินยอมให้ khanatuch ปรับไลเซนส์ของโปรเจกต์ในอนาคตได้
  — ถ้าไม่ยินยอมข้อหลัง บอกใน PR

> แผนในอนาคต: ใช้ CLA Assistant (https://cla-assistant.io/) ให้เซ็นยืนยันผ่าน GitHub อัตโนมัติ

## รายงานปัญหา (Issue)

แนบ: รุ่นรถ/ปี/เครื่องยนต์, รุ่น dongle ELM327, log จาก Serial (ลบ MAC ของอุปกรณ์อื่นออก), รูปจอ
ดู [docs/08-troubleshooting.md](docs/08-troubleshooting.md)
