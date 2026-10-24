# TRIX device protocol v1 (trix-node/1)

สัญญาระหว่างอุปกรณ์ IoT ทุกตัวกับ TRIX ใช้คู่กับ library firmware กลาง `TrixNode`
firmware อ้างอิงที่ทำตามสัญญานี้อยู่ใน repo นี้ (`TrixNode/` และ `examples/`)

ฝั่ง server เป็นของ TRIX (ไม่อยู่ใน repo นี้) เอกสารนี้คือสัญญาที่ server ทำตาม

## 1. Topic

```
trix/<site>/<device-id>/<channel>
```

- `site` = slug ของ site ใน TRIX ส่วน `device-id` = รหัสอุปกรณ์ภายใน site นั้น
- อุปกรณ์ใหม่ใช้ตัวพิมพ์เล็ก ตัวเลข และ `-` ยาวไม่เกิน 48 ตัว เช่น `bme-005`, `inv-01`
  - server ยอมรับ `A-Z a-z 0-9 _ . -` ยาวไม่เกิน 64 เพื่อให้อุปกรณ์รุ่นเก่าที่ตั้งชื่อด้วยตัวพิมพ์ใหญ่ยังใช้ได้
  - topic ที่ไม่ผ่านเกณฑ์นี้หรือชื่อ metric ผิดรูปแบบจะถูกทิ้ง
- MQTT client id = `<site>/<device-id>` เพราะ device-id ซ้ำข้าม site ได้
- ไม่ขึ้นต้น topic ด้วย `/` และไม่ใส่ช่องว่าง
- credential MQTT ของ site ถูก ACL จำกัดไว้ที่ `trix/<site>/#`

| channel | ทิศทาง | retain | payload |
|---|---|---|---|
| `telemetry` | device to server | no | JSON ค่าที่วัดได้ 1 รอบ |
| `status` | device to server | yes | `{"online":true,...}` ตอนเชื่อมต่อ และ LWT `{"online":false}` |
| `meta` | device to server | yes | อุปกรณ์คืออะไร ส่งครั้งเดียวหลังเชื่อมต่อ |
| `cmd` | server to device | no | คำสั่ง |
| `ack` | device to server | no | ผลของคำสั่ง |
| `ota` | device to server | no | ความคืบหน้าและผลของ OTA |

- **QoS:** LWT ตั้งเป็น QoS 1 ส่วนข้อความอื่นจาก TrixNode ออกที่ QoS 0 เพราะ PubSubClient ส่ง QoS 1 ไม่ได้ ตัวรับ subscribe ที่ QoS 1
- **ข้อความ retained:** broker จะส่ง retained ซ้ำทุกครั้งที่ตัวรับ subscribe ใหม่ ตัวรับจึงจัดการดังนี้
  - `status`/`meta` ที่เป็น retained: บันทึกสถานะ แต่ไม่ขยับ `last_seen`
  - `telemetry` และค่าแบบเดิมที่เป็น retained: ทิ้ง ไม่บันทึกเป็นค่าใหม่
- **ลำดับข้อความ:** ข้อความ `status`/`meta`/`ota`/`ack` ของอุปกรณ์เดียวกันถูกประมวลผลตามลำดับที่มาถึง
- **จำกัดการสร้างอัตโนมัติ:** device และ metric ใหม่สร้างได้ไม่เกิน 200 รายการต่อชั่วโมงต่อ process กันชื่อขยะท่วมตาราง

topic แบบเดิม `trix/<site>/<device-id>/<metric>` ที่ payload เป็นตัวเลขเปล่า ยังรับอยู่
เพื่อให้อุปกรณ์ที่แฟลชก่อน v1 ทำงานต่อได้ แต่อุปกรณ์ใหม่ห้ามใช้
ชื่อ metric จึงห้ามซ้ำกับชื่อ channel ทั้ง 6 ตัว (`state` ยังใช้เป็น metric ได้)

## 2. telemetry

```json
{"ts":1760600000,"seq":12,"temperature_c":25.3,"humidity_pct":61.2,"relay_on":true}
```

- 1 ข้อความต่อ 1 รอบการวัด ไม่เกิน 64 ค่า
- `ts` = unix seconds (UTC) ของเวลาที่วัด ไม่ใส่ก็ได้ถ้านาฬิกาอุปกรณ์ยังไม่ sync
  - ถ้าห่างจากเวลา server เกิน 7 วันย้อนหลัง หรือ 5 นาทีล่วงหน้า server จะใช้เวลาของตัวเองแทน
- `seq` = ตัวนับเพิ่มทีละ 1 หลังบูต ใช้ดูว่าข้อความหาย
- `v`, `fw` สงวนไว้ ไม่ถูกบันทึกเป็นค่า
- `via` = id ของ gateway ที่ส่งต่อข้อมูลของ node LoRa แทน (เช่น CYD receiver ส่งให้ node P2P)
  - ต้องผ่านกฎเดียวกับ device id ใน topic ถ้าไม่ผ่านจะอยู่ใน `rejected`
  - ไม่ถูกบันทึกเป็นค่า server เก็บไว้ที่ `devices.metadata.last_via` และเขียนเฉพาะเมื่อ gateway เปลี่ยน
  - ส่ง `rssi_dbm` และ `snr_db` ของลิงก์ LoRa เป็นค่าปกติคู่กัน
- ค่าที่บันทึกต้องเป็นตัวเลข boolean แปลงเป็น 1/0 ข้อความและ object ถูกปฏิเสธ
- ชื่อ key เป็น snake_case ตัวพิมพ์เล็ก **ลงท้ายด้วยหน่วย** ระบบเติมหน่วยให้ metric ใหม่จากท้ายชื่อ

| ท้ายชื่อ | หน่วย | ตัวอย่าง |
|---|---|---|
| `_c` | C | `temperature_c` |
| `_pct` | % | `humidity_pct`, `battery_soc_pct` |
| `_hpa` `_kpa` | hPa kPa | `pressure_hpa` |
| `_v` `_a` `_w` `_kw` | V A W kW | `voltage_v`, `current_a`, `power_w` |
| `_wh` `_kwh` | Wh kWh | `energy_kwh` |
| `_va` `_var` | VA var | `apparent_va` |
| `_hz` | Hz | `grid_freq_hz` |
| `_dbm` | dBm | `rssi_dbm` |
| `_ppm` | ppm | `co2_ppm` |
| `_lux` | lx | `light_lux` |
| `_umol` | umol/m2/s | `ppfd_umol` |
| `_lpm` `_l` | L/min L | `flow_lpm` |
| `_mm` `_m` | mm m | `rain_mm`, `level_m` |
| `_s` `_ms` | s ms | `uptime_s` |
| `_deg` | deg | `wind_dir_deg` |

ค่าที่ไม่มีหน่วย (สถานะ จำนวนนับ) ไม่ต้องมีท้ายชื่อ เช่น `relay_on`, `state`, `error_count`

## 3. status (birth และ LWT)

ตอนเชื่อมต่อ broker ให้ตั้ง LWT เป็น `{"online":false}` (QoS 1, retain)
ทันทีที่เชื่อมต่อได้ ส่งข้อความนี้ทับ:

```json
{"online":true,"fw":"1.2.0","ip":"192.168.1.20","rssi_dbm":-61,"reset_reason":"poweron","uptime_s":3}
```

`online` และ `fw` ถูกเก็บลง `devices.firmware_ver` ส่วนค่าอื่นเก็บใน `devices.metadata.status`
ข้อความ `online` หรือ `offline` แบบข้อความเปล่าของอุปกรณ์รุ่นเก่ายังรับอยู่

## 4. meta

```json
{"type":"bme280-node","hw":"esp32dev","fw":"1.2.0","interval_s":60,"sensors":["bme280"]}
```

- `type` = ชนิดอุปกรณ์ ใช้รูปแบบเดียวกับ device id และต้องตรงกับ `deviceType` ของ firmware ใน TRIX เพราะ OTA เลือกไฟล์ตามค่านี้
- เก็บลง `devices.metadata.meta`
- `devices.device_type` ถูกตั้งจาก `type` เฉพาะเมื่อยังเป็นค่าที่ระบบตั้งตอนสร้างอัตโนมัติ (`sensor` หรือ `auto`) ชนิดที่ admin ตั้งเองจะไม่ถูกเขียนทับ

## 5. cmd และ ack

```json
cmd: {"id":"c-17","action":"reboot"}
cmd: {"id":"c-18","action":"ota_check"}
cmd: {"id":"c-19","action":"set","interval_s":30}
ack: {"id":"c-17","ok":true}
ack: {"id":"c-19","ok":false,"error":"interval_s out of range"}
```

action ที่ library กลางรู้จัก: `reboot`, `factory_reset` (ล้าง WiFi/broker แล้วกลับเป็น setup AP), `ota_check`, `ping` และ `set` สำหรับ `interval_s`
action อื่นส่งต่อให้โค้ดของแต่ละอุปกรณ์จัดการ อุปกรณ์ต้องตอบ `ack` ทุกคำสั่งที่มี `id`

## 6. OTA

```
GET https://trix.technology/api/ota/check?type=<type>&fw=<semver>&id=<device-id>
```

| คำตอบ | ความหมาย |
|---|---|
| `{"ok":true,"upToDate":true,"version":"1.3.0"}` | ไม่ต้องอัปเดต |
| `{"ok":true,"upToDate":false,"type","version","size","sha256","url","token"}` | มีรุ่นใหม่ |
| 503 | server ยังไม่ได้ตั้ง `OTA_SIGNING_KEY` ห้ามอัปเดต |

`token` = `base64url(payload).base64url(sig)` โดยที่ payload คือ
`{"type","version","size","sha256","exp"}` เรียงตามนี้ และ sig คือ Ed25519 ของ payload
รูปแบบเดียวกับที่ SolarMonitor ใช้อยู่ (เพิ่มฟิลด์ `exp`)
`exp` = unix seconds หมดอายุ 24 ชั่วโมงหลังออก กัน manifest เก่าถูกนำมาเล่นซ้ำ

อุปกรณ์ต้องทำครบทุกข้อ ไม่อย่างนั้นห้ามแฟลช
1. ตรวจลายเซ็นของ token ด้วย public key ที่ฝังใน firmware (`TRIX_OTA_PUBKEY`) token ที่ไม่มี `exp` ถือว่าไม่ผ่าน
2. ค่าใน payload ที่ลงลายเซ็นต้องตรงกับ `type` `version` `size` `sha256` ใน manifest และ `type` ต้องตรงกับชนิดของตัวเอง
3. `version` ต้องใหม่กว่ารุ่นที่รันอยู่ ห้าม downgrade
   - ลำดับรุ่นตาม semver: `1.3.0-rc1` < `1.3.0`
   - ถ้ารุ่นที่รันอยู่อ่านไม่ออก (NVS ว่าง หรือรุ่นก่อนมาตรฐาน) ให้รับรุ่นใหม่ เพื่อกู้ตัวเองได้
   - นาฬิกาต้อง sync NTP แล้ว และเวลาปัจจุบันต้องไม่เกิน `exp` ถ้ายังไม่ sync ให้เลื่อนการตรวจไปก่อน
4. ดาวน์โหลดลง slot สำรอง (A/B) แล้วคำนวณ sha256 ของทั้งไฟล์ให้ตรงก่อน `Update.end()`
5. รายงานแต่ละขั้นที่ `.../ota`: `checking`, `downloading`, `verifying`, `success`, `failed`, `rejected`, `up_to_date`
   - ตัวอย่าง `{"state":"failed","version":"1.3.0","reason":"sha256"}`
   - หลังรีบูตเข้ารุ่นใหม่ ให้ส่ง `success` พร้อม `version` server จะอัปเดต `firmware_ver` ให้

ช่วงเวลาตรวจ: หลังบูตแบบสุ่มหน่วง 0-2 นาที, ทุก 6 ชั่วโมง (สุ่มเลื่อน 0-30 นาที) และเมื่อได้ `cmd` `ota_check`
server จำกัด 120 ครั้งต่อนาทีต่อ IP
ใน URL ให้เข้ารหัส `+` ของ build metadata เป็น `%2B`

ระหว่างดาวน์โหลด TrixNode ยังรักษา MQTT ไว้ (เรียก `loop()` ทุกวินาที) เพื่อไม่ให้ LWT ทำงาน
HTTP response ที่เป็น JSON อ่านแบบ HTTP/1.0 จึงไม่มีปัญหา chunked encoding ส่วนไฟล์ .bin อ่านผ่าน `writeToStream` ซึ่งแกะ chunked ให้

ฝั่ง server: TRIX ถือ private key ไว้ฝั่งเดียว เซ็น token ให้ทุก release และแจก public key คู่กันให้ใส่ใน firmware

`/api/firmware/latest` แบบเดิมยังอยู่สำหรับอุปกรณ์ที่แฟลชก่อน v1

## 7. Provisioning

`GET /api/portal/<view-token>/config.json` (QR บนหน้า portal ของ site) ให้ค่าต่อไปนี้
- broker, credential MQTT และ prefix ของ topic
- `protocol` (`"trix-node/1"`), `topic.channels` และ `ota.checkUrl`

อุปกรณ์เก็บค่าทั้งหมดลง NVS **ห้ามฝัง credential ใน source code**

## 8. ทดสอบ

```bash
npm test                                   # unit: protocol + OTA signing
MONITOR_DATABASE_URL=... npx tsx scripts/test-mqtt-bridge-v1.ts   # bridge e2e (DB ทิ้งได้)
TURSO_DATABASE_URL=file:/tmp/x.db npx tsx scripts/test-ota-check.ts  # OTA e2e
MONITOR_DATABASE_URL=... npx tsx scripts/test-lorawan-ingest.ts      # LoRaWAN webhook e2e (DB ทิ้งได้)
```

## 9. LoRaWAN (ผ่าน network server)

node LoRaWAN ไม่พูด MQTT ข้อมูลเดินทางแบบนี้

```
node --LoRaWAN--> gateway (SenseCAP) --> ChirpStack / TTN --codec--> POST /api/ingest/lorawan --> TRIX
```

ใน TRIX node แต่ละตัวเป็นอุปกรณ์ปกติหนึ่งตัว เหมือนอุปกรณ์ที่ส่ง `telemetry` ผ่าน MQTT

### 9.1 สัญญาของ endpoint

```
POST /api/ingest/lorawan?site=<site-slug>[&event=up]
Authorization: Bearer <LORAWAN_WEBHOOK_TOKEN>      (หรือ X-Trix-Token: <token>)
Content-Type: application/json
body: ChirpStack v4 HTTP integration event หรือ TTN v3 webhook uplink (แยกจากรูปร่าง JSON เอง)
```

| ผล | ความหมาย |
|---|---|
| 200 `{"ok":true,"site","device","stored":n,"rejected":[...]}` | เก็บแล้ว |
| 204 | event ที่ไม่ใช่ uplink (join, ack, status, txack, TTN join accept) รับทราบแต่ไม่เก็บ |
| 400 | ไม่ใช่ JSON หรือไม่ใช่รูปแบบของ ChirpStack v4 / TTN v3 |
| 401 | token ผิดหรือไม่มี |
| 404 | ไม่มี site นี้ใน TRIX |
| 422 | ไม่มีผลถอดรหัส (ยังไม่ได้ตั้ง codec) หรือหา site / device id ไม่ได้ |
| 429 | ส่งเกิน 600 ครั้ง/นาที/IP หรือชนเพดานสร้างอุปกรณ์ใหม่ 200 ตัว/ชั่วโมง |
| 503 | server ยังไม่ได้ตั้ง `LORAWAN_WEBHOOK_TOKEN` |

การจับคู่

| ใน TRIX | มาจาก (ลำดับก่อนหลัง) |
|---|---|
| site | query `?site=` แล้วจึง device tag `trix_site` |
| device id | device tag `trix_device` แล้วจึงชื่ออุปกรณ์ (ChirpStack `deviceName`, TTN `device_id`) แปลงอักขระที่ใช้ไม่ได้เป็น `-` แล้วจึง devEui |
| device type | device tag `trix_type` แล้วจึงชื่อ device profile (ChirpStack) แทนที่เฉพาะค่าเริ่มต้น `sensor` ไม่ทับที่ admin ตั้ง |
| readings | ผลถอดรหัสของ codec (`object` / `decoded_payload`) ตัวเลขและ boolean เท่านั้น object ซ้อนต่อชื่อด้วย `_` (`{"battery":{"v":3.6}}` เป็น `battery_v`) |
| เพิ่มจากวิทยุ | `rssi_dbm`, `snr_db` ของ gateway ที่สัญญาณแรงสุด และ `f_cnt` |
| เวลา | `time` (ChirpStack) / `received_at` (TTN) ถ้าห่างจากเวลา server เกินขอบเขตเดียวกับข้อ 2 ใช้เวลา server |
| `devices.metadata.last_via` | gateway id ที่สัญญาณแรงสุด ตัวเดียวกับ `via` ของ telemetry ทาง MQTT |
| `devices.metadata.lorawan` | `via` (gateway id), `dev_eui`, `f_cnt`, `f_port`, `rssi_dbm`, `snr_db`, `gateways`, `at` |

**codec ต้องออกชื่อ key ตามมาตรฐานเลย** (`temperature_c`, `humidity_pct`, `battery_v`) เพราะ TRIX ไม่แปลงชื่อ
key ที่ผิดรูปแบบจะอยู่ใน `rejected` และไม่ถูกเก็บ online/offline ของ node LoRaWAN ให้ดูจาก `last_seen` เพราะไม่มี LWT

### 9.2 ตั้งค่า ChirpStack v4

1. Applications แล้วเลือก application แล้ว Integrations แล้ว HTTP
2. Payload encoding: **JSON**
3. Event endpoint URL: `https://trix.technology/api/ingest/lorawan?site=<site-slug>`
   - ChirpStack ต่อ `&event=up` ให้เอง
   - ถ้า ChirpStack อยู่ใน LAN เดียวกับ homework จะใช้ที่อยู่ภายในแทนก็ได้
4. Headers: `Authorization` = `Bearer <LORAWAN_WEBHOOK_TOKEN>`
5. Device profile แล้ว Codec: JavaScript ที่มี `decodeUplink` คืนค่า `{ data: { temperature_c: ..., ... } }`
6. (ถ้าต้องการ) ที่ตัว device ใส่ tag `trix_device` เพื่อกำหนด device id และ `trix_type` เพื่อกำหนดชนิด

### 9.3 ตั้งค่า The Things Stack (TTN v3)

1. Applications แล้ว Integrations แล้ว Webhooks แล้ว Add webhook แล้ว Custom webhook
2. Webhook format: **JSON**
3. Base URL: `https://trix.technology/api/ingest/lorawan?site=<site-slug>`
4. Additional headers: `Authorization` = `Bearer <LORAWAN_WEBHOOK_TOKEN>`
5. เปิดเฉพาะ **Uplink message** (path ว่าง)
6. Payload formatter ของ application หรือ device: `decodeUplink` ที่ออกชื่อ key มาตรฐาน
7. device id ใน TRIX คือ `device_id` ของ TTN เพราะ webhook ไม่ส่ง attributes มาด้วย

