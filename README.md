# ESP32 Level 2 — Smart IoT Controller

ระบบควบคุมและตรวจวัดอัตโนมัติบน ESP32 รองรับการแสดงผล OLED, Web Dashboard, MQTT และ OpenWeatherMap

---

## สารบัญ

- [ภาพรวมระบบ](#ภาพรวมระบบ)
- [Hardware ที่ใช้](#hardware-ที่ใช้)
- [การเชื่อมต่อขา GPIO](#การเชื่อมต่อขา-gpio)
- [โครงสร้างไฟล์](#โครงสร้างไฟล์)
- [คำอธิบาย Class แต่ละตัว](#คำอธิบาย-class-แต่ละตัว)
- [การติดตั้งเครื่องมือ](#การติดตั้งเครื่องมือ)
- [Libraries ที่ใช้](#libraries-ที่ใช้)
- [การตั้งค่า config.h](#การตั้งค่า-configh)
- [ลำดับการทำงาน Setup](#ลำดับการทำงาน-setup)
- [OLED หน้าจอ 2 หน้า](#oled-หน้าจอ-2-หน้า)
- [Web Dashboard](#web-dashboard)
- [MQTT Topics](#mqtt-topics)
- [การกู้คืนสถานะหลัง Reset](#การกู้คืนสถานะหลัง-reset)
- [การ Reset WiFi](#การ-reset-wifi)
- [การแก้ปัญหาเบื้องต้น](#การแก้ปัญหาเบื้องต้น)

---

## ภาพรวมระบบ

```
┌─────────────────────────────────────────────────────────────┐
│                        ESP32 DevKit                         │
│                                                             │
│  ┌──────────┐   ┌──────────┐   ┌─────────────────────────┐ │
│  │ SW1/2/3  │   │ Relay    │   │ OLED 128×64 (I2C)       │ │
│  │ GPIO     │   │ 1/2/3    │   │ 2-Page Auto Scroll      │ │
│  │ 34/35/32 │   │ 17/16/4  │   └─────────────────────────┘ │
│  └──────────┘   └──────────┘                               │
│                                                             │
│  ┌──────────┐   ┌──────────┐   ┌──────────┐               │
│  │ DS18B20  │   │ XY-MD03  │   │ WiFi     │               │
│  │ GPIO14   │   │ Serial0  │   │ Manager  │               │
│  │ OneWire  │   │ Modbus   │   │ Portal   │               │
│  └──────────┘   └──────────┘   └──────────┘               │
└───────────────────────┬─────────────────────────────────────┘
                        │ WiFi
          ┌─────────────┼─────────────┐
          │             │             │
   ┌──────▼──────┐ ┌────▼────┐ ┌─────▼──────┐
   │ Web Dashboard│ │  MQTT   │ │OpenWeather │
   │ Port 80     │ │HiveMQ   │ │   Map API  │
   │ WebSocket   │ │1883     │ │            │
   └─────────────┘ └─────────┘ └────────────┘
```

---

## Hardware ที่ใช้

| อุปกรณ์ | รุ่น / รายละเอียด |
|---------|-----------------|
| MCU | ESP32 DevKit V1 (30-pin) |
| จอแสดงผล | OLED SSD1306 128×64 (I2C) |
| Relay Module | 3-Channel Active Low |
| ปุ่มกด | Switch ×3 (Active Low, External Pull-up 10kΩ) |
| เซนเซอร์อุณหภูมิ 1 | DS18B20 (OneWire, GPIO14) |
| เซนเซอร์อุณหภูมิ 2 | XY-MD03 (Modbus RTU, RS485, Serial0) |
| RS485 Converter | MAX13487 Auto Direction (ไม่ต้องใช้ DE/RE pin) |
| Switch | สลับ RS232 ↔ RS485 บน Serial0 |

---

## การเชื่อมต่อขา GPIO

```
GPIO  2 — (ว่าง)
GPIO  4 — Relay 3          (Active Low)
GPIO 14 — DS18B20 Data     (OneWire + 4.7kΩ Pull-up ถึง 3.3V)
GPIO 16 — Relay 2          (Active Low)
GPIO 17 — Relay 1          (Active Low)
GPIO 21 — OLED SDA         (I2C)
GPIO 22 — OLED SCL         (I2C)
GPIO 32 — SW3              (Active Low, Pull-up 10kΩ ภายนอก)
GPIO 34 — SW1              (Active Low, Pull-up 10kΩ ภายนอก)  ← ใช้ Reset WiFi
GPIO 35 — SW2              (Active Low, Pull-up 10kΩ ภายนอก)
GPIO  1 — TX0 → RS485 A+  (Serial0 / XY-MD03)
GPIO  3 — RX0 ← RS485 B-  (Serial0 / XY-MD03)
```

> **หมายเหตุ GPIO 34/35:** เป็น Input-Only ไม่รองรับ Internal Pull-up ต้องใช้ External Pull-up เท่านั้น

---

## โครงสร้างไฟล์

```
ESP32_LEVEL2/
├── src/
│   └── main.cpp              ← โปรแกรมหลัก
├── include/
│   ├── config.h              ← การตั้งค่าทั้งหมด (API Key, MQTT, ฯลฯ)
│   ├── DevIsoInput.h         ← Base class สำหรับ Digital Input
│   ├── DevSwitch.h           ← จัดการปุ่มกด (extends DevIsoInput)
│   ├── DevRelay.h            ← ควบคุม Relay + Timer
│   ├── DevOLED.h             ← แสดงผล OLED 2 หน้าสลับกัน
│   ├── DevWifiManager.h      ← WiFi Manager + Captive Portal
│   ├── DevWeather.h          ← ดึงข้อมูล OpenWeatherMap API
│   ├── DevDS18B20.h          ← อ่านค่า DS18B20 + Simulation
│   ├── DevXYMDSensor.h       ← อ่านค่า XY-MD03 Modbus + Simulation
│   ├── DevMQTT.h             ← MQTT Client (HiveMQ)
│   ├── DevWebServer.h        ← Async Web Server + WebSocket
│   ├── DevStateStore.h       ← บันทึก/กู้คืน State ลง NVS Flash
│   ├── DevPZEM.h             ← (พร้อมใช้) PZEM-016 Power Monitor
│   └── dashboard.h           ← HTML Dashboard (embed ใน Flash)
├── platformio.ini            ← Project configuration
└── README.md                 ← เอกสารนี้
```

---

## คำอธิบาย Class แต่ละตัว

### `DevIsoInput` / `DevSwitch`
จัดการปุ่มกดพร้อม Debouncing และ Edge Detection

| Method | คำอธิบาย |
|--------|---------|
| `begin()` | ตั้งค่า pinMode |
| `update()` | เรียกใน `loop()` ทุกรอบ |
| `wasPressed()` | true เฉพาะรอบที่เพิ่งกด (rising edge) |
| `isPressed()` | true ตลอดที่กดค้างอยู่ |
| `readRawState()` | อ่านสถานะตรงจาก GPIO (ไม่ผ่าน debounce) |

---

### `DevRelay`
ควบคุม Relay รองรับ Active Low / Active High

| Method | คำอธิบาย |
|--------|---------|
| `begin()` | ตั้งค่า GPIO และปิด Relay เริ่มต้น |
| `on()` / `off()` | เปิด / ปิด |
| `toggle()` | สลับสถานะ |
| `setState(bool)` | กำหนดสถานะโดยตรง |
| `getState()` | อ่านสถานะปัจจุบัน |

**Class ขยาย:** `DevRelayWithTimer` — เปิด Relay แล้วปิดอัตโนมัติตาม Timer

---

### `DevOLED`
แสดงผล OLED SSD1306 แบบ **2 หน้าสลับอัตโนมัติ** ทุก 5 วินาที

| หน้า | เนื้อหา |
|------|---------|
| **หน้า 1** | DS18B20 (ตัวเลขใหญ่) + XY-MD03 Temp/Hum + Relay icons |
| **หน้า 2** | Weather OWM (Temp/Hum/Rain/PM2.5/AQI) + Relay icons + IP |

| Method | คำอธิบาย |
|--------|---------|
| `begin(sda, scl)` | เริ่มต้น I2C + Display |
| `tick()` | เรียกใน `loop()` — สลับหน้าอัตโนมัติ |
| `showMain(...)` | อัปเดตข้อมูลและวาดหน้าปัจจุบันใหม่ |
| `showMessage(l1,l2,l3)` | แสดงข้อความชั่วคราว |
| `showCountdown(sec, total)` | Progress bar นับถอยหลัง |
| `showIP(ip)` | แสดง IP หลังเชื่อมต่อ WiFi |

---

### `DevWifiManager`
ห่อ `WiFiManager` Library เพื่อเชื่อมต่อ WiFi แบบ Captive Portal

- ถ้ามี credentials เก่า → เชื่อมต่ออัตโนมัติ
- ถ้าไม่มี หรือ reset → เปิด AP "ESP32-Setup" รอตั้งค่า
- แสดงสถานะทุกขั้นตอนบน OLED

---

### `DevWeather`
ดึงข้อมูลสภาพอากาศจาก **OpenWeatherMap API** 3 endpoint

| Endpoint | ข้อมูล |
|---------|--------|
| Current Weather | อุณหภูมิ, ความชื้น |
| Forecast (cnt=1) | % โอกาสฝนตก (pop) ใน 3 ชั่วโมงข้างหน้า |
| Air Pollution | PM2.5, AQI (1=Good … 5=Very Poor) |

| Method | คำอธิบาย |
|--------|---------|
| `update()` | ดึงข้อมูลใหม่ทั้งหมด |
| `isDue(sec)` | true ถ้าถึงเวลาอัปเดต |
| `getData()` | คืน `WeatherData` struct |

---

### `DevDS18B20`
อ่านค่าอุณหภูมิจาก DS18B20 ผ่าน OneWire

- **Auto Simulation:** ถ้าไม่พบ sensor หรือ sensor หลุด → จำลองค่า 28–34°C อัตโนมัติ
- อ่านทุก 2 วินาที (12-bit resolution)

| Method | คำอธิบาย |
|--------|---------|
| `begin()` | ตรวจหา sensor, คืน `false` = sim mode |
| `update()` | อ่านค่าตาม interval, คืน `true` ถ้ามีค่าใหม่ |
| `getTemp()` | อ่านอุณหภูมิ (°C) |
| `isSimMode()` | true = กำลัง simulate |

---

### `DevXYMDSensor`
อ่านค่า Temp/Hum จาก **XY-MD03** ผ่าน Modbus RTU บน Serial0

- Slave ID: **2** (ตั้งค่าที่ sensor)
- Baud Rate: 9600, 8N1
- RS485 Auto Direction ผ่าน MAX13487

- **Auto Simulation:** ถ้า fail ติดกัน 5 ครั้ง → จำลองค่าอัตโนมัติ
- มี `scanSlaveID()` สำหรับค้นหา ID อัตโนมัติ

> ⚠️ `begin()` จะ reinit Serial0 เป็น 9600 — `Serial.print()` ใช้งานไม่ได้หลังจากนี้

---

### `DevMQTT`
MQTT Client เชื่อมต่อ **HiveMQ Public Broker** (ฟรี ไม่ต้องสมัคร)

- Reconnect อัตโนมัติเมื่อหลุด
- Publish Telemetry JSON ทุก 5 วินาที
- LWT (Last Will Testament): publish `offline` เมื่อบอร์ดตัดการเชื่อมต่อ
- รับคำสั่ง relay จาก topic `relay/N/set`

---

### `DevWebServer`
Async Web Server พร้อม WebSocket real-time

- Serve หน้า Dashboard จาก Flash (PROGMEM)
- Broadcast JSON ทุก 2 วินาที ผ่าน WebSocket `/ws`
- รับคำสั่ง toggle relay จาก browser ทันที

---

### `DevStateStore`
บันทึก/กู้คืน State ลง **NVS Flash** (Non-Volatile Storage)

- เขียนเฉพาะเมื่อค่าเปลี่ยน → ป้องกัน Flash Wear
- กู้คืนอัตโนมัติทุกครั้งที่ boot
- นับจำนวน boot ไว้ใน `boot_cnt`

| NVS Key | Type | ข้อมูล |
|---------|------|--------|
| `r1` | bool | Relay 1 state |
| `r2` | bool | Relay 2 state |
| `r3` | bool | Relay 3 state |
| `boot_cnt` | uint32 | จำนวนครั้งที่ boot |

---

## การติดตั้งเครื่องมือ

### 1. ติดตั้ง VS Code
ดาวน์โหลดที่ [https://code.visualstudio.com](https://code.visualstudio.com)

### 2. ติดตั้ง PlatformIO Extension
1. เปิด VS Code → Extensions (`Ctrl+Shift+X`)
2. ค้นหา **PlatformIO IDE**
3. กด Install และ Reload

### 3. เปิด Project
```
File → Open Folder → เลือกโฟลเดอร์ ESP32_LEVEL2
```

### 4. Build และ Upload
```
PlatformIO Toolbar:
  ✓  Build       (Ctrl+Alt+B)
  →  Upload      (Ctrl+Alt+U)
  🔌 Serial Mon  (Ctrl+Alt+S)
```

---

## Libraries ที่ใช้

| Library | เวอร์ชัน | การใช้งาน |
|---------|---------|---------|
| `ModbusMaster` | ^2.0.1 | Modbus RTU สำหรับ XY-MD03 |
| `Adafruit SSD1306` | ^2.5.9 | Driver จอ OLED |
| `Adafruit GFX Library` | ^1.11.9 | Graphics สำหรับ OLED |
| `WiFiManager` | ^2.0.17 | WiFi Captive Portal |
| `ArduinoJson` | ^7.2.0 | JSON serialize/parse |
| `ESPAsyncWebServer` | ^3.3.0 | Async Web Server + WebSocket |
| `OneWire` | ^2.3.8 | Protocol สำหรับ DS18B20 |
| `DallasTemperature` | ^3.11.0 | Driver DS18B20 |
| `PubSubClient` | ^2.8.0 | MQTT Client |

> Libraries ทั้งหมดถูกจัดการโดย PlatformIO อัตโนมัติผ่าน `platformio.ini`

---

## การตั้งค่า config.h

แก้ไขไฟล์ `include/config.h` ก่อน build:

```cpp
// ===== OpenWeatherMap =====
#define OWM_API_KEY   "ใส่ API Key ของคุณ"   // สมัครฟรีที่ openweathermap.org
#define OWM_LAT       "8.4322"                // ละติจูด นครศรีธรรมราช
#define OWM_LON       "99.9631"               // ลองจิจูด
#define OWM_CITY_NAME "Nakhon Si Thammarat"
#define WEATHER_UPDATE_SEC  300               // อัปเดตทุก 5 นาที

// ===== MQTT =====
#define MQTT_HOST       "broker.hivemq.com"  // Public broker (ฟรี)
#define MQTT_PORT       1883
#define MQTT_CLIENT_ID  "esp32-level2-xxxx"  // เปลี่ยนให้ unique
#define MQTT_BASE       "esp32level2"         // Base topic (เปลี่ยนให้ unique)
#define MQTT_TELEMETRY_INTERVAL  5000         // Publish ทุก 5 วินาที
```

> ⚠️ **ความปลอดภัย:** `config.h` มี API Key — ไม่ควร commit ขึ้น Git สาธารณะ

---

## ลำดับการทำงาน Setup

```
เปิดเครื่อง
    │
    ├─ 1. Serial.begin(115200)
    ├─ 2. OLED init → "Booting..."
    ├─ 3. NVS load → กู้คืน relay state
    ├─ 4. Relay begin → setState (ค่าที่บันทึกไว้)
    ├─ 5. DS18B20 init → GPIO14
    ├─ 6. ตรวจ SW1 ค้าง 5 วิ → Reset WiFi?
    ├─ 7. WiFiManager → เชื่อมต่อ / เปิด Portal
    ├─ 8. แสดง IP บน OLED (2 วินาที)
    ├─ 9. Serial.flush() → reinit Serial0 → 9600
    ├─ 10. XY-MD03 init → Modbus ID:2
    ├─ 11. Weather API → ดึงข้อมูลครั้งแรก
    ├─ 12. MQTT → เชื่อมต่อ broker
    ├─ 13. Web Server → เริ่ม port 80
    └─ 14. _updateDisplay() → แสดงหน้าจอหลัก
```

---

## OLED หน้าจอ 2 หน้า

สลับอัตโนมัติทุก **5 วินาที** (ปรับได้ที่ `PAGE_INTERVAL` ใน `DevOLED.h`)

### หน้า 1 — Sensors
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

### หน้า 2 — Weather
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

---

## Web Dashboard

เปิด Browser แล้วไปที่ `http://<IP ที่แสดงบน OLED>/`

### Cards ที่แสดง

| Card | เนื้อหา |
|------|---------|
| ⚡ Relay Control | สถานะ + ปุ่ม เปิด/ปิด Relay แต่ละตัว |
| 🌡 XY-MD03 (ID:2) | Temp, Hum + badge SIM/LIVE |
| 🌡 DS18B20 | อุณหภูมิ + progress bar + badge SIM/LIVE |
| 🌤 สภาพอากาศ | OWM Temp/Hum/Rain/PM2.5/AQI |
| 📶 WiFi & Network | SSID, IP, RSSI, MAC, Heap, Uptime |
| 🔗 MQTT Broker | Topic ทั้งหมด + สถานะการเชื่อมต่อ |

### API Endpoints

| Endpoint | Method | คำอธิบาย |
|---------|--------|---------|
| `/` | GET | หน้า Dashboard HTML |
| `/api/status` | GET | JSON snapshot ทุกค่า |
| `/ws` | WebSocket | Real-time update ทุก 2 วินาที |

---

## MQTT Topics

Base topic: `esp32level2` (เปลี่ยนได้ใน `config.h`)

### Publish (ESP32 → Broker)

| Topic | Payload | หมายเหตุ |
|-------|---------|---------|
| `esp32level2/telemetry` | JSON | ทุกค่า ทุก 5 วินาที |
| `esp32level2/status` | `online` / `offline` | LWT, retain |
| `esp32level2/relay/1/state` | `ON` / `OFF` | retain |
| `esp32level2/relay/2/state` | `ON` / `OFF` | retain |
| `esp32level2/relay/3/state` | `ON` / `OFF` | retain |

### Subscribe (Broker → ESP32)

| Topic | Payload | คำอธิบาย |
|-------|---------|---------|
| `esp32level2/relay/1/set` | `ON` / `OFF` / `TOGGLE` | สั่ง Relay 1 |
| `esp32level2/relay/2/set` | `ON` / `OFF` / `TOGGLE` | สั่ง Relay 2 |
| `esp32level2/relay/3/set` | `ON` / `OFF` / `TOGGLE` | สั่ง Relay 3 |

### ตัวอย่างการใช้งาน

```bash
# สั่งเปิด Relay 1
mosquitto_pub -h broker.hivemq.com -t esp32level2/relay/1/set -m ON

# Toggle Relay 2
mosquitto_pub -h broker.hivemq.com -t esp32level2/relay/2/set -m TOGGLE

# ดู Telemetry แบบ real-time
mosquitto_sub -h broker.hivemq.com -t esp32level2/telemetry

# ดูทุก topic พร้อมกัน
mosquitto_sub -h broker.hivemq.com -t "esp32level2/#"
```

### Telemetry JSON ตัวอย่าง

```json
{
  "relay": { "1": "ON", "2": "OFF", "3": "ON" },
  "ds18":  { "temp": "32.65", "sim": false },
  "xymd":  { "temp": "28.4", "hum": "68.2", "sim": false },
  "weather": {
    "valid": true,
    "temp": "33.5", "hum": 78,
    "rain": 45, "pm25": "18.2",
    "aqi": 2, "aqiLabel": "Fair"
  },
  "wifi": { "ip": "192.168.1.105", "rssi": -65 },
  "sys":  { "heap": 187432, "uptime": 3600 }
}
```

---

## การกู้คืนสถานะหลัง Reset

ระบบใช้ **NVS (Non-Volatile Storage)** ของ ESP32 บันทึกสถานะ Relay

```
เหตุการณ์ที่ trigger บันทึก:
  ✓ กดปุ่ม SW1/2/3
  ✓ Toggle จาก Web Dashboard
  ✓ สั่งผ่าน MQTT

เมื่อ Reset / ไฟดับ:
  → อ่านค่าจาก NVS
  → Relay กลับมาสถานะเดิมทันที
  → OLED แสดง "Restored State" + Boot#N
```

> NVS รองรับการเขียน ~100,000 ครั้งต่อ Key — ระบบเขียนเฉพาะเมื่อค่าเปลี่ยนจริงเท่านั้น

---

## การ Reset WiFi

กด **SW1 ค้าง 5 วินาที** ขณะ boot (ก่อน WiFi เชื่อมต่อ)

```
OLED แสดง:
  Hold to Reset WiFi
  [████████░░░░]   ← Progress bar
        3          ← นับถอยหลัง
  Release to cancel

ปล่อยก่อนครบ → ยกเลิก
ค้างครบ 5 วิ → ล้าง credentials → เปิด AP "ESP32-Setup"
```

**การตั้งค่า WiFi ผ่าน Portal:**
1. เชื่อมต่อ WiFi ชื่อ `ESP32-Setup` บน Phone/PC
2. Browser จะเปิด Portal อัตโนมัติ (หรือไปที่ `192.168.4.1`)
3. เลือก WiFi และใส่ Password
4. ESP32 เชื่อมต่อและ IP แสดงบน OLED

---

## การแก้ปัญหาเบื้องต้น

### XY-MD03 เป็น Simulation ตลอด
- ตรวจ Switch ว่าอยู่ตำแหน่ง **RS485** ก่อนเปิดเครื่อง
- ตรวจ Slave ID ที่ sensor ว่าตั้งเป็น **2** แล้ว
- ตรวจสาย A+/B- ไม่สลับ
- Serial Monitor จะแสดง `[XYMD] Scanning ID...` ถ้าหาไม่เจอที่ ID=2

### OLED ไม่แสดงผล
- ตรวจ I2C Address ว่าเป็น `0x3C`
- ตรวจขา SDA=21, SCL=22
- ลอง I2C Scanner sketch

### WiFi เชื่อมต่อไม่ได้
- กด SW1 ค้าง 5 วินาทีตอน boot เพื่อ reset credentials
- ตรวจว่า Router เป็น 2.4GHz (ESP32 ไม่รองรับ 5GHz)

### MQTT ไม่เชื่อมต่อ
- ตรวจว่า WiFi เชื่อมต่อสำเร็จก่อน
- `broker.hivemq.com` เป็น Public broker — ต้องการ Internet
- เปลี่ยน `MQTT_CLIENT_ID` ให้ unique ถ้าชนกับ client อื่น

### DS18B20 เป็น Simulation
- ตรวจ Pull-up 4.7kΩ ระหว่าง Data และ 3.3V
- ตรวจการเชื่อมต่อที่ GPIO14
- Sensor อาจร้อนเกินและ disconnect — ตรวจ `isSimMode()`

---

*สร้างด้วย PlatformIO + Arduino Framework สำหรับ ESP32*
