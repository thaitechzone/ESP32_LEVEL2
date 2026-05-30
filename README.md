# ESP32 Level 2 — Smart IoT Controller

ระบบควบคุมและตรวจวัดอัตโนมัติบน ESP32 พร้อม State Machine UI, OLED 2 หน้า, Web Dashboard, MQTT และ OpenWeatherMap

---

## สารบัญ

- [ภาพรวมระบบ](#ภาพรวมระบบ)
- [Hardware ที่ใช้](#hardware-ที่ใช้)
- [การเชื่อมต่อขา GPIO](#การเชื่อมต่อขา-gpio)
- [โครงสร้างไฟล์](#โครงสร้างไฟล์)
- [คำอธิบาย Class แต่ละตัว](#คำอธิบาย-class-แต่ละตัว)
- [State Machine — การควบคุมด้วยปุ่ม](#state-machine--การควบคุมด้วยปุ่ม)
- [OLED หน้าจอ](#oled-หน้าจอ)
- [Web Dashboard](#web-dashboard)
- [MQTT Topics](#mqtt-topics)
- [การกู้คืนสถานะหลัง Reset](#การกู้คืนสถานะหลัง-reset)
- [การติดตั้งเครื่องมือ](#การติดตั้งเครื่องมือ)
- [Libraries ที่ใช้](#libraries-ที่ใช้)
- [การตั้งค่า config.h](#การตั้งค่า-configh)
- [ลำดับการทำงาน Setup](#ลำดับการทำงาน-setup)
- [การแก้ปัญหาเบื้องต้น](#การแก้ปัญหาเบื้องต้น)

---

## ภาพรวมระบบ

```
┌──────────────────────────────────────────────────────────────────┐
│                          ESP32 DevKit                            │
│                                                                  │
│  ┌─────────────────────┐   ┌──────────┐   ┌──────────────────┐  │
│  │  State Machine UI   │   │ Relay    │   │ OLED 128×64      │  │
│  │  SW1 = MODE/ENTER   │   │ 1/2/3    │   │ 5 หน้าจอ:        │  │
│  │  SW2 = DOWN         │   │ 17/16/4  │   │ MONITOR/MENU/    │  │
│  │  SW3 = UP           │   │          │   │ RELAY/SETTINGS/  │  │
│  │  GPIO 34 / 35 / 32  │   │          │   │ CONFIRM          │  │
│  └─────────────────────┘   └──────────┘   └──────────────────┘  │
│                                                                  │
│  ┌──────────┐  ┌──────────────┐  ┌──────────┐  ┌────────────┐  │
│  │ DS18B20  │  │   XY-MD03    │  │   WiFi   │  │ NVS Flash  │  │
│  │ GPIO14   │  │ Serial0/9600 │  │ Manager  │  │ StateStore │  │
│  │ OneWire  │  │ Modbus ID:2  │  │ Portal   │  │ boot_cnt   │  │
│  └──────────┘  └──────────────┘  └──────────┘  └────────────┘  │
└──────────────────────────┬───────────────────────────────────────┘
                           │ WiFi
           ┌───────────────┼──────────────┐
           │               │              │
    ┌──────▼──────┐  ┌─────▼─────┐  ┌────▼───────┐
    │Web Dashboard│  │   MQTT    │  │OpenWeather │
    │  Port 80    │  │  HiveMQ   │  │  Map API   │
    │  WebSocket  │  │  Port 1883│  │  3 endpoint│
    └─────────────┘  └───────────┘  └────────────┘
```

---

## Hardware ที่ใช้

| อุปกรณ์ | รุ่น / รายละเอียด |
|---------|-----------------|
| MCU | ESP32 DevKit V1 (30-pin) |
| จอแสดงผล | OLED SSD1306 128×64 (I2C, 0x3C) |
| Relay Module | 3-Channel Active Low |
| ปุ่มกด | Switch ×3 (Active Low, External Pull-up 10kΩ) |
| เซนเซอร์อุณหภูมิ 1 | DS18B20 (OneWire, GPIO14) |
| เซนเซอร์อุณหภูมิ+ความชื้น | XY-MD03 (Modbus RTU RS485, Serial0) |
| RS485 Converter | MAX13487 Auto Direction (ไม่ต้องใช้ DE/RE pin) |
| Switch | สลับ RS232 ↔ RS485 บน Serial0 |

---

## การเชื่อมต่อขา GPIO

```
GPIO  1 — TX0 → RS485 A+   (Serial0 / XY-MD03 Modbus)
GPIO  3 — RX0 ← RS485 B-   (Serial0 / XY-MD03 Modbus)
GPIO  4 — Relay 3           (Active Low)
GPIO 14 — DS18B20 Data      (OneWire + 4.7kΩ Pull-up → 3.3V)
GPIO 16 — Relay 2           (Active Low)
GPIO 17 — Relay 1           (Active Low)
GPIO 21 — OLED SDA          (I2C)
GPIO 22 — OLED SCL          (I2C)
GPIO 32 — SW3  UP           (Active Low, External Pull-up 10kΩ)
GPIO 34 — SW1  MODE/ENTER   (Active Low, External Pull-up 10kΩ)
GPIO 35 — SW2  DOWN         (Active Low, External Pull-up 10kΩ)
```

> **GPIO 34/35** เป็น Input-Only — ไม่มี Internal Pull-up ต้องใช้ External 10kΩ เท่านั้น

> **Serial0** ใช้ร่วมกัน: `Serial.print()` (115200) ก่อน init XY-MD03, หลังจากนั้น reinit เป็น 9600 Modbus

---

## โครงสร้างไฟล์

```
ESP32_LEVEL2/
├── src/
│   └── main.cpp              ← โปรแกรมหลัก + event dispatcher
├── include/
│   ├── config.h              ← ค่าตั้งต้น API Key, MQTT, พิกัด GPS
│   ├── DevIsoInput.h         ← Base class: Digital Input + debounce
│   ├── DevSwitch.h           ← ปุ่มกด (extends DevIsoInput)
│   ├── DevRelay.h            ← Relay control + DevRelayWithTimer
│   ├── DevOLED.h             ← OLED driver: MONITOR/MENU/RELAY/SETTINGS/CONFIRM
│   ├── DevStateMachine.h     ← State Machine: states, transitions, events
│   ├── DevWifiManager.h      ← WiFi Manager + Captive Portal
│   ├── DevWeather.h          ← OpenWeatherMap API (3 endpoints)
│   ├── DevDS18B20.h          ← DS18B20 OneWire + Auto Simulation
│   ├── DevXYMDSensor.h       ← XY-MD03 Modbus RTU + Auto Simulation
│   ├── DevMQTT.h             ← MQTT Client (HiveMQ) + LWT + Telemetry
│   ├── DevWebServer.h        ← AsyncWebServer + WebSocket
│   ├── DevStateStore.h       ← NVS Flash persistence (relay state)
│   ├── DevPZEM.h             ← (พร้อมใช้) PZEM-016 Power Monitor
│   └── dashboard.h           ← HTML/CSS/JS Dashboard (PROGMEM)
├── platformio.ini            ← Build config + lib_deps
└── README.md                 ← เอกสารนี้
```

---

## คำอธิบาย Class แต่ละตัว

### `DevIsoInput` / `DevSwitch`
Base class สำหรับ Digital Input พร้อม Debouncing (50ms default) และ Edge Detection

| Method | คำอธิบาย |
|--------|---------|
| `begin()` | ตั้งค่า pinMode (INPUT_PULLUP สำหรับ Active Low) |
| `update()` | ต้องเรียกทุก `loop()` — อัปเดต debounce + edge flags |
| `wasPressed()` | `true` เฉพาะรอบที่เพิ่งกด (rising edge, one-shot) |
| `wasReleased()` | `true` เฉพาะรอบที่เพิ่งปล่อย (falling edge, one-shot) |
| `isPressed()` | `true` ตลอดที่กดค้างอยู่ |
| `readRawState()` | อ่าน GPIO โดยตรงไม่ผ่าน debounce (ใช้ใน hold detection) |

---

### `DevRelay`
ควบคุม Relay รองรับ Active Low / Active High

| Method | คำอธิบาย |
|--------|---------|
| `begin()` | ตั้งค่า GPIO และ `off()` เริ่มต้น |
| `on()` / `off()` | เปิด / ปิด |
| `toggle()` | สลับสถานะ |
| `setState(bool)` | กำหนดสถานะโดยตรง (ใช้ตอน restore จาก NVS) |
| `getState()` | อ่านสถานะปัจจุบัน |

**Class ขยาย:** `DevRelayWithTimer` — `onWithTimer(ms)` เปิดแล้วปิดอัตโนมัติ, `checkTimer()` เรียกใน `loop()`

---

### `DevStateMachine`
จัดการ State ทั้งหมดของระบบ แยก logic ออกจาก main.cpp

| State | เนื้อหา |
|-------|---------|
| `MONITOR` | หน้าจอหลัก auto-cycle sensor/weather |
| `MENU` | เมนูหลัก — Relay Control / Settings / Back |
| `RELAY_CTRL` | เลือก relay ด้วย SW1, สั่ง ON/OFF ด้วย SW3/SW2 |
| `SETTINGS` | WiFi Reset / OLED Speed / Back |
| `CONFIRM` | ยืนยันคำสั่งอันตราย (YES/NO) |

`update()` คืน `Result { event, relayNum }` ให้ main.cpp dispatch:

| Event | ความหมาย |
|-------|---------|
| `RELAY_CHANGED` | relay N เปลี่ยน → save NVS + MQTT + redraw |
| `WIFI_RESET` | ล้าง WiFi credentials + `ESP.restart()` |
| `OLED_SPEED_TOGGLE` | สลับ page interval 5s ↔ 2s |
| `REDRAW` | วาด OLED ใหม่ (ข้อมูลไม่เปลี่ยน) |
| `NONE` | ไม่มีเหตุการณ์ |

---

### `DevOLED`
แสดงผล OLED SSD1306 128×64 รองรับ **5 หน้าจอ** ตาม State Machine

**หน้าจอ MONITOR** — auto-cycle 2 หน้าตาม `tick(interval_ms)`:

| หน้า | เนื้อหา |
|------|---------|
| Sensors (1/2) | DS18B20 ตัวใหญ่ + XY-MD03 Temp/Hum + Relay icons |
| Weather (2/2) | OWM Temp/Hum/Rain/PM2.5/AQI + Relay icons + IP |

**หน้าจอ State Machine:**

| Method | State ที่ใช้ |
|--------|------------|
| `showMenu(cursor)` | MENU |
| `showRelayCtrl(cursor, r1, r2, r3)` | RELAY_CTRL |
| `showSettings(cursor, oledFast)` | SETTINGS |
| `showConfirm(label, cursor)` | CONFIRM |

**หน้าจอชั่วคราว:**

| Method | ใช้เมื่อ |
|--------|---------|
| `showMessage(l1,l2,l3)` | Boot messages, errors |
| `showCountdown(sec, total)` | WiFi reset hold |
| `showIP(ip)` | หลัง WiFi connect |
| `tick(interval_ms)` | Loop: auto-cycle หน้า MONITOR เท่านั้น |

---

### `DevWifiManager`
ห่อ `WiFiManager` Library

- มี credentials → เชื่อมต่ออัตโนมัติ
- ไม่มี / reset → เปิด AP `ESP32-Setup` + Captive Portal
- แสดงสถานะทุกขั้นตอนบน OLED

---

### `DevWeather`
ดึงข้อมูลสภาพอากาศ **นครศรีธรรมราช** จาก OpenWeatherMap API

| Endpoint | ข้อมูลที่ได้ |
|---------|------------|
| Current Weather | อุณหภูมิ, ความชื้น |
| Forecast (cnt=1) | % โอกาสฝนตก (pop) 3 ชั่วโมงข้างหน้า |
| Air Pollution | PM2.5, AQI (1=Good … 5=Very Poor) |

| Method | คำอธิบาย |
|--------|---------|
| `update()` | ดึง 3 API พร้อมกัน |
| `isDue()` | true ถ้าถึงเวลาอัปเดต (ทุก `WEATHER_UPDATE_SEC`) |
| `getData()` | คืน `WeatherData` struct |

---

### `DevDS18B20`
อ่านอุณหภูมิ DS18B20 ผ่าน OneWire (GPIO14, 12-bit resolution)

- ไม่พบ sensor หรือ sensor หลุด → **Auto Simulation** 28–34°C (sine wave)
- Fail ทุกครั้งจนกว่า `reconnect()` จะถูกเรียก

| Method | คำอธิบาย |
|--------|---------|
| `begin()` | ตรวจหา sensor, คืน `false` = เข้า sim |
| `update()` | อ่านทุก 2 วินาที, คืน `true` ถ้ามีค่าใหม่ |
| `getTemp()` | อุณหภูมิ °C |
| `isSimMode()` | `true` = กำลัง simulate |

---

### `DevXYMDSensor`
อ่าน Temp/Hum จาก XY-MD03 ผ่าน Modbus RTU บน Serial0

- Slave ID: **2**, Baud: 9600, 8N1
- RS485 Auto Direction ผ่าน MAX13487
- Fail ≥ 5 ครั้งติดกัน → **Auto Simulation** (sine wave สมจริง)
- มี `scanSlaveID(1–10)` หา ID อัตโนมัติเมื่อ ID=2 ไม่ตอบสนอง

> ⚠️ `begin(9600)` reinit Serial0 → `Serial.print()` ใช้งานไม่ได้หลังจากนี้

---

### `DevMQTT`
MQTT Client เชื่อมต่อ HiveMQ Public Broker (ไม่ต้องสมัคร)

- Auto reconnect ทุก 5 วินาทีเมื่อหลุด
- Publish Telemetry JSON ทุก `MQTT_TELEMETRY_INTERVAL` ms
- LWT: publish `offline` อัตโนมัติเมื่อบอร์ดตัดการเชื่อมต่อ
- Subscribe `relay/N/set` รับคำสั่ง ON/OFF/TOGGLE

---

### `DevWebServer`
Async Web Server + WebSocket real-time

- Serve Dashboard HTML จาก PROGMEM Flash
- Broadcast JSON state ทุก 2 วินาที ผ่าน `/ws`
- รับ `{ cmd:"relay", n:N }` จาก browser → toggle + callback

---

### `DevStateStore`
บันทึก/กู้คืน State ลง NVS Flash (Preferences API)

- เขียน NVS เฉพาะเมื่อค่าเปลี่ยนจริง → ป้องกัน Flash Wear
- กู้คืน Relay state อัตโนมัติทุก boot
- NVS namespace: `appstate`

| Key | Type | ข้อมูล |
|-----|------|--------|
| `r1` | bool | Relay 1 state |
| `r2` | bool | Relay 2 state |
| `r3` | bool | Relay 3 state |
| `boot_cnt` | uint32 | จำนวนครั้งที่ boot |

---

## State Machine — การควบคุมด้วยปุ่ม

### หน้าที่ปุ่ม

| ปุ่ม | GPIO | หน้าที่ |
|------|------|--------|
| **SW1** | 34 | MODE / ENTER — เข้าเมนู / ยืนยัน |
| **SW2** | 35 | DOWN — เลื่อนลง / relay OFF |
| **SW3** | 32 | UP — เลื่อนขึ้น / relay ON |
| **SW1 ค้าง 2 วิ** | — | กลับ MONITOR จากทุก state |

### State Diagram

```
                   ┌────────────────────────────────────────┐
                   │      Hold SW1 ≥ 2s  →  กลับ MONITOR   │
                   └──────────────────┬─────────────────────┘
                                      │
Boot ──────────► MONITOR ──[SW1]──► MENU
                    ▲                  │
                    │        ┌─────────┴──────────┐
                    │        ▼                    ▼
                    │   RELAY_CTRL           SETTINGS
                    │   ──────────           ────────
                    │   SW3 → relay ON       SW3/SW2 → เลื่อน
                    │   SW2 → relay OFF      SW1 → เลือก item
                    │   SW1 → เลือก relay        │
                    │   Hold → กลับ         [WiFi Reset]
                    │                            ▼
                    │                        CONFIRM
                    │                        ───────
                    │                        SW3 → YES
                    │                        SW2 → NO
                    │                        SW1 → execute
                    │                            │
                    └────────────────────────────┘
```

### ตารางปุ่มแต่ละ State

| State | SW1 (ENTER) | SW2 (DOWN) | SW3 (UP) | Hold SW1 |
|-------|------------|-----------|---------|---------|
| **MONITOR** | เข้า MENU | — | — | — |
| **MENU** | เลือก item | เลื่อน cursor ลง | เลื่อน cursor ขึ้น | กลับ MONITOR |
| **RELAY_CTRL** | เปลี่ยน relay ที่เลือก | relay ที่เลือก **OFF** | relay ที่เลือก **ON** | กลับ MONITOR |
| **SETTINGS** | เลือก item | เลื่อน cursor ลง | เลื่อน cursor ขึ้น | กลับ MONITOR |
| **CONFIRM** | ยืนยัน (YES/NO) | เลือก **NO** | เลือก **YES** | กลับ MONITOR |

### หน้าจอแต่ละ State

**MONITOR — หน้า Sensors (1/2)**
```
< Sensors            1/2>
─────────────────────────
DS18B20              [OK]
  32.65°C
─────────────────────────
XYMD (ID:2)         [SIM]
  T:28.4°C  H:68.2%
─────────────────────────
R1:■  R2:□  R3:■
```

**MONITOR — หน้า Weather (2/2)**
```
< Weather OWM        2/2>
─────────────────────────
OUT 33.5°C   Hum:78%
Rain:45% [████████░░]
PM2.5:18.2  AQI:2 Fair
─────────────────────────
R1:■  R2:□  R3:■
IP:192.168.1.105
```

**MENU**
```
  *** MENU ***
─────────────────────────
▶ Relay Control          ← cursor highlight
  Settings
  < Back
```

**RELAY_CTRL**
```
RELAY CTRL [SW1:next]
─────────────────────────
▶ Relay 1       [ON ]   ← relay ที่กำลังเลือก
  Relay 2       [OFF]
  Relay 3       [ON ]
─────────────────────────
UP=ON  DOWN=OFF  Hold=Back
```

**SETTINGS**
```
  *** SETTINGS ***
─────────────────────────
▶ WiFi Reset
  OLED Speed: [SLOW 5s]
  < Back
```

**CONFIRM**
```
  *** CONFIRM ***
─────────────────────────
WiFi Reset!
Are you sure?

   [ YES ]     NO
─────────────────────────
UP=YES DOWN=NO SW1=OK
```

---

## OLED หน้าจอ

- **MONITOR mode:** `tick(interval_ms)` สลับหน้า Sensors ↔ Weather อัตโนมัติ
- **ความเร็ว:** ปรับได้จาก Settings → OLED Speed (5 วินาที หรือ 2 วินาที)
- **ทุก State อื่น:** `tick()` ไม่ทำงาน — OLED แสดงหน้า UI ของ State นั้นคงที่

---

## Web Dashboard

เปิด Browser ไปที่ `http://<IP ที่แสดงบน OLED>/`

### Cards ที่แสดง

| Card | เนื้อหา |
|------|---------|
| ⚡ Relay Control | สถานะ + ปุ่ม เปิด/ปิด Relay 1-3 (real-time) |
| 🌡 XY-MD03 (ID:2) | Temp, Hum + gauge bar + badge **SIM**/**LIVE** |
| 🌡 DS18B20 | อุณหภูมิ + gauge bar + badge **SIM**/**LIVE** |
| 🌤 สภาพอากาศ | OWM Temp/Hum/Rain% + bar + PM2.5 + AQI |
| 📶 WiFi & Network | SSID, IP, RSSI signal bar, MAC, Heap, Uptime |
| 🔗 MQTT Broker | Topics ทั้งหมด + Connected badge + ตัวอย่าง command |

### API Endpoints

| Endpoint | Method | คำอธิบาย |
|---------|--------|---------|
| `/` | GET | Dashboard HTML (Tailwind + WebSocket) |
| `/api/status` | GET | JSON snapshot ทุกค่า |
| `/ws` | WebSocket | Push update ทุก 2 วินาที + relay command |

---

## MQTT Topics

Base topic: `esp32level2` (เปลี่ยนได้ใน `config.h → MQTT_BASE`)

### Publish (ESP32 → Broker)

| Topic | Payload | หมายเหตุ |
|-------|---------|---------|
| `esp32level2/telemetry` | JSON | ทุกค่า ทุก 5 วินาที |
| `esp32level2/status` | `online` / `offline` | LWT retain |
| `esp32level2/relay/1/state` | `ON` / `OFF` | retain |
| `esp32level2/relay/2/state` | `ON` / `OFF` | retain |
| `esp32level2/relay/3/state` | `ON` / `OFF` | retain |

### Subscribe (Broker → ESP32)

| Topic | Payload ที่รองรับ | คำอธิบาย |
|-------|----------------|---------|
| `esp32level2/relay/1/set` | `ON` `OFF` `TOGGLE` | สั่ง Relay 1 |
| `esp32level2/relay/2/set` | `ON` `OFF` `TOGGLE` | สั่ง Relay 2 |
| `esp32level2/relay/3/set` | `ON` `OFF` `TOGGLE` | สั่ง Relay 3 |

### ตัวอย่าง CLI

```bash
# เปิด Relay 1
mosquitto_pub -h broker.hivemq.com -t esp32level2/relay/1/set -m ON

# Toggle Relay 2
mosquitto_pub -h broker.hivemq.com -t esp32level2/relay/2/set -m TOGGLE

# ดู Telemetry real-time
mosquitto_sub -h broker.hivemq.com -t esp32level2/telemetry

# ดูทุก topic
mosquitto_sub -h broker.hivemq.com -t "esp32level2/#"
```

### Telemetry JSON ตัวอย่าง

```json
{
  "relay":   { "1": "ON", "2": "OFF", "3": "ON" },
  "ds18":    { "temp": "32.65", "sim": false },
  "xymd":    { "temp": "28.4", "hum": "68.2", "sim": false },
  "weather": {
    "valid": true, "temp": "33.5", "hum": 78,
    "rain": 45, "pm25": "18.2", "aqi": 2, "aqiLabel": "Fair"
  },
  "mqtt":  { "connected": true, "host": "broker.hivemq.com" },
  "wifi":  { "ssid": "MyWiFi", "ip": "192.168.1.105", "rssi": -65 },
  "sys":   { "heap": 187432, "uptime": 3600 }
}
```

---

## การกู้คืนสถานะหลัง Reset

ทุกครั้งที่ Relay เปลี่ยนสถานะ (จากปุ่ม, Web, MQTT) → บันทึกลง NVS ทันที

```
Power ON / Reset
  ├─ stateStore.load()           อ่าน NVS
  ├─ relay1.setState(saved_r1)   กู้คืน Relay 1
  ├─ relay2.setState(saved_r2)   กู้คืน Relay 2
  ├─ relay3.setState(saved_r3)   กู้คืน Relay 3
  └─ OLED: "Restored State / Boot #N"
```

> NVS รองรับ ~100,000 write cycles ต่อ key — ระบบเขียนเฉพาะเมื่อค่าเปลี่ยนจริง

---

## การติดตั้งเครื่องมือ

### 1. VS Code + PlatformIO

1. ติดตั้ง [VS Code](https://code.visualstudio.com)
2. Extensions (`Ctrl+Shift+X`) → ค้นหา **PlatformIO IDE** → Install → Reload

### 2. เปิด Project

```
File → Open Folder → เลือกโฟลเดอร์ ESP32_LEVEL2
```

### 3. Build / Upload / Monitor

```
PlatformIO Toolbar (ซ้ายล่าง):
  ✓  Build         Ctrl+Alt+B
  →  Upload        Ctrl+Alt+U
  🔌 Serial Monitor Ctrl+Alt+S   ← ดู debug log 115200 baud
```

### 4. ตั้งค่าก่อน Build

แก้ `include/config.h` ตามหัวข้อ [การตั้งค่า config.h](#การตั้งค่า-configh)

---

## Libraries ที่ใช้

| Library | เวอร์ชัน | หน้าที่ |
|---------|---------|--------|
| `ModbusMaster` | ^2.0.1 | Modbus RTU (XY-MD03) |
| `Adafruit SSD1306` | ^2.5.9 | OLED driver |
| `Adafruit GFX Library` | ^1.11.9 | OLED graphics primitives |
| `WiFiManager` | ^2.0.17 | WiFi Captive Portal |
| `ArduinoJson` | ^7.2.0 | JSON build/parse |
| `ESPAsyncWebServer` | ^3.3.0 | Async HTTP + WebSocket |
| `OneWire` | ^2.3.8 | OneWire protocol (DS18B20) |
| `DallasTemperature` | ^3.11.0 | DS18B20 driver |
| `PubSubClient` | ^2.8.0 | MQTT client |

> PlatformIO จัดการ download + build อัตโนมัติผ่าน `platformio.ini`

---

## การตั้งค่า config.h

แก้ไขไฟล์ `include/config.h` ก่อน build ครั้งแรก:

```cpp
// ===== OpenWeatherMap =====
#define OWM_API_KEY   "YOUR_KEY_HERE"    // สมัครฟรีที่ openweathermap.org
#define OWM_LAT       "8.4322"           // ละติจูด (นครศรีธรรมราช)
#define OWM_LON       "99.9631"          // ลองจิจูด
#define OWM_CITY_NAME "Nakhon Si Thammarat"
#define WEATHER_UPDATE_SEC  300          // อัปเดตทุก 5 นาที (Free plan: 60 calls/min)

// ===== MQTT (HiveMQ Public) =====
#define MQTT_HOST            "broker.hivemq.com"
#define MQTT_PORT            1883
#define MQTT_CLIENT_ID       "esp32-level2-xxxx"  // เปลี่ยนให้ unique (ป้องกันชน)
#define MQTT_BASE            "esp32level2"          // Base topic (เปลี่ยนให้ unique)
#define MQTT_TELEMETRY_INTERVAL  5000               // Publish ทุก 5 วินาที
```

> ⚠️ **ความปลอดภัย:** `config.h` มี API Key — ไม่ควร commit ขึ้น public Git repository

---

## ลำดับการทำงาน Setup

```
Power ON
  │
  1.  Serial.begin(115200)
  2.  OLED.begin() → "Booting..."
  3.  stateStore.load() → โหลด relay state จาก NVS
  4.  relay1/2/3.begin() + setState(NVS value)
  5.  DS18B20.begin() → ตรวจ sensor GPIO14
  6.  checkWifiResetHold(SW1, 5s) → Reset WiFi?
  7.  WiFiManager.begin() → เชื่อมต่อ / เปิด Captive Portal
  8.  OLED แสดง IP (2 วินาที)
  9.  Serial.flush() → XY-MD03.begin(9600) → reinit Serial0 Modbus
  10. OLED แสดงผล XYMD init status
  11. Weather.update() → ดึง OWM API ครั้งแรก
  12. MQTT.begin() → เชื่อมต่อ broker.hivemq.com
  13. WebServer.begin() → เริ่ม port 80 + WebSocket /ws
  14. State Machine เริ่มทำงาน (MONITOR state)
```

---

## การแก้ปัญหาเบื้องต้น

### XY-MD03 เป็น SIM ตลอด
- ตรวจ Switch อยู่ตำแหน่ง **RS485** ก่อนเปิดเครื่อง
- ตรวจสาย **A+** (TX0/GPIO1) และ **B-** (RX0/GPIO3) ไม่สลับกัน
- ตรวจ Slave ID ที่ sensor ตั้งเป็น **2** แล้วหรือยัง
- ดู OLED: ถ้าแสดง `Scanning ID...` แสดงว่าหาไม่พบที่ ID=2 แต่ยังสแกนต่อ

### OLED ไม่แสดงผล
- ตรวจ I2C Address ต้องเป็น `0x3C`
- ตรวจขา SDA=GPIO21, SCL=GPIO22
- ลอง I2C Scanner sketch ตรวจสอบ address

### WiFi เชื่อมต่อไม่ได้
- กด **SW1 ค้าง 5 วิ** ตอน boot → reset credentials → ตั้งค่าใหม่ผ่าน Portal
- ตรวจว่า Router ใช้ **2.4GHz** (ESP32 ไม่รองรับ 5GHz)

### MQTT ไม่เชื่อมต่อ
- ต้องมี Internet ก่อน (`broker.hivemq.com` = public cloud)
- เปลี่ยน `MQTT_CLIENT_ID` ให้ unique ถ้าชนกับ client อื่น
- ดู Serial Monitor ก่อน `xymd.begin()` — จะแสดง `rc=` error code

### DS18B20 เป็น SIM
- ตรวจ Pull-up **4.7kΩ** ระหว่าง Data pin และ 3.3V
- ตรวจสายที่ GPIO14
- อาจเกิดจาก sensor ร้อนเกิน → ตรวจ `isSimMode()` ใน Serial Monitor

### ปุ่มไม่ตอบสนอง
- ตรวจ External Pull-up **10kΩ** ต่อถึง 3.3V (GPIO 34/35 ไม่มี Internal Pull-up)
- ตรวจ debounce: `wasPressed()` คืน `true` ครั้งเดียวต่อการกดเท่านั้น
- ถ้า hold SW1 ค้างแล้วไม่กลับ MONITOR ตรวจว่า `isPressed()` คืน `true` ถูกต้อง

---

*สร้างด้วย PlatformIO · Arduino Framework · ESP32 DevKit V1*
