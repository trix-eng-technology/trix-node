# TRIX Node

firmware อ้างอิงสำหรับอุปกรณ์ IoT ที่ส่งข้อมูลเข้า [TRIX Monitor](https://trix.technology) ตามสัญญา `trix-node/1`
ใช้เป็นจุดเริ่มต้นทำอุปกรณ์ของตัวเองได้ เขียนเพิ่มแค่ส่วนอ่านเซนเซอร์

Reference firmware for IoT devices that report to TRIX Monitor over the `trix-node/1` protocol (MQTT over TLS, signed OTA). ESP32 / Arduino / PlatformIO. Apache-2.0.

## มีอะไรบ้าง

| โฟลเดอร์ | คืออะไร |
|---|---|
| `TrixNode/` | library กลาง: หน้า setup ผ่าน WiFi, provisioning จาก QR ของ site, MQTT over TLS ที่ตรวจใบรับรอง, รับคำสั่ง, OTA ที่ตรวจลายเซ็น Ed25519 |
| `examples/bme280-node/` | อุปกรณ์ WiFi อ้างอิง (ESP32, ESP32-S3, ESP32-C3) อ่าน BME280 |
| `examples/lorawan-bme280-node/` | Heltec WiFi LoRa 32 V2 ส่ง LoRaWAN OTAA AS923 พร้อม codec สำหรับ ChirpStack/TTN |
| `docs/device-protocol-v1.md` | สัญญาระหว่างอุปกรณ์กับ TRIX: topic, ชื่อค่า, สถานะ, คำสั่ง, OTA, LoRaWAN |

## เริ่มอุปกรณ์ WiFi

1. ติดตั้ง [PlatformIO](https://platformio.org/) แล้วคัดลอก `examples/bme280-node` ไปเป็นโปรเจกต์ใหม่
   - แก้ `symlink://../../TrixNode` ใน `platformio.ini` ให้ชี้มาที่ `TrixNode`
   - ตั้ง `FW_TYPE` และ `FW_VERSION`
2. ถ้าจะใช้ OTA: `cp include/trix_ota_pubkey.example.h include/trix_ota_pubkey.h` แล้วใส่ public key ที่ได้จาก TRIX
   ไม่มีไฟล์นี้ อุปกรณ์ก็ทำงานได้ แค่ไม่อัปเดต firmware ทางอากาศ
3. เขียนส่วนอ่านเซนเซอร์ใน `loop()` ใช้ชื่อค่าตามมาตรฐาน เช่น `temperature_c`, `humidity_pct`, `power_w` (ดู `docs/`)
4. `pio test -e native` ต้องผ่าน แล้ว `pio run -e esp32dev -t upload`
5. ต่อ WiFi `trix-setup-xxxxxx` เปิด `http://192.168.4.1` กรอก WiFi, device id และ URL `config.json` จาก QR ในหน้า portal ของ site

## เริ่มอุปกรณ์ LoRaWAN

1. `cp include/lorawan_keys.example.h include/lorawan_keys.h` ใส่ DevEUI, JoinEUI, AppKey ของเครื่อง แล้วลบบรรทัด `LORAWAN_KEYS_PLACEHOLDER`
2. ChirpStack: device profile AS923 OTAA MAC 1.0.3 อัปโหลด `codec/trix-lorawan-bme280.js`
3. ตั้ง HTTP integration ไปที่ TRIX ตาม `docs/device-protocol-v1.md` หัวข้อ LoRaWAN
4. เทสต์: `pio test -e native` และ `node --test codec/test.mjs` ใช้ test vector ชุดเดียวกัน

## ความปลอดภัย

- อย่า commit `include/lorawan_keys.h` หรือ `include/trix_ota_pubkey.h` (อยู่ใน `.gitignore` แล้ว)
- OTA ยอมรับเฉพาะ firmware ที่มี token ลงลายเซ็นด้วย private key ของ TRIX และยังไม่หมดอายุ key ใน test เป็นค่าตัวอย่างจาก RFC 8032 ห้ามใช้กับเครื่องจริง
- พบช่องโหว่ แจ้งทีม TRIX ทางหน้าติดต่อที่ https://trix.technology/contact อย่าเปิด issue สาธารณะ

## ผลทดสอบ

| ทดสอบ | ผล |
|---|---|
| bme280-node `pio test -e native` | 11/11 ผ่าน |
| bme280-node `pio run -e esp32dev` | build ผ่าน |
| lorawan-bme280-node `pio test -e native` / `pio run` / codec | 6/6 ผ่าน / build ผ่าน / 7/7 ผ่าน |
| บอร์ดจริง | lorawan-bme280-node ยังไม่ได้ลองบนบอร์ดจริง (pin BME280 4/15 และตัวคูณแบตเตอรี่ 3.2 ของ Heltec ต้องตรวจกับบอร์ด) |

## License

Apache License 2.0 ดู `LICENSE` และ `NOTICE`
