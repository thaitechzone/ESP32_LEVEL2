#ifndef DEV_TELEGRAM_H
#define DEV_TELEGRAM_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "config.h"
#include "DevRelay.h"
#include "DevWeather.h"
#include "DevDS18B20.h"
#include "DevXYMDSensor.h"
#include "DevNTP.h"

// ── Alert type bitmask — ส่งเฉพาะประเภทที่ enable ──────────────
namespace TgAlert {
  constexpr uint16_t RELAY_ON       = 1 << 0;  // relay เปิด
  constexpr uint16_t RELAY_OFF      = 1 << 1;  // relay ปิด
  constexpr uint16_t TEMP_HIGH      = 1 << 2;  // อุณหภูมิสูงเกิน threshold
  constexpr uint16_t TEMP_LOW       = 1 << 3;  // อุณหภูมิต่ำเกิน threshold
  constexpr uint16_t AQI_POOR       = 1 << 4;  // AQI ≥ 4
  constexpr uint16_t RAIN_HIGH      = 1 << 5;  // โอกาสฝน ≥ threshold
  constexpr uint16_t WIFI_LOST      = 1 << 6;  // WiFi หลุด
  constexpr uint16_t BOOT           = 1 << 7;  // เปิดเครื่อง
  constexpr uint16_t SCHEDULE_ON    = 1 << 8;  // relay เปิดตามตาราง
  constexpr uint16_t SCHEDULE_OFF   = 1 << 9;  // relay ปิดตามตาราง
  constexpr uint16_t ALL            = 0xFFFF;
}

class DevTelegram {
private:
  const char* botToken;
  const char* chatID;

  DevRelay*       relay[3];
  DevWeather*     weather   = nullptr;
  DevDS18B20*     ds18      = nullptr;
  DevXYMDSensor*  xymd      = nullptr;
  DevNTP*         ntp       = nullptr;

  uint16_t  enabledAlerts  = TgAlert::ALL;

  // Threshold
  float     tempHighLimit  = 40.0f;   // °C DS18B20 สูงสุด
  float     tempLowLimit   = 10.0f;   // °C DS18B20 ต่ำสุด
  int       rainHighLimit  = 70;      // % โอกาสฝน
  int       aqiAlertLevel  = 4;       // AQI ≥ ค่านี้แจ้งเตือน

  // Cooldown — ป้องกัน spam แต่ละ alert type
  static const unsigned long COOLDOWN_MS = 300000UL; // 5 นาที
  unsigned long lastSent[16] = {0};  // indexed by bit position

  // Rate-limit queue — ส่งได้ไม่เกิน 1 ข้อ/วินาที (Telegram limit)
  struct QueueItem { String text; unsigned long queuedAt; };
  static const int QUEUE_SIZE = 8;
  QueueItem  queue[QUEUE_SIZE];
  int        qHead = 0, qTail = 0, qCount = 0;
  unsigned long lastSentAt = 0;

  bool isEnabled(uint16_t type) {
    return (enabledAlerts & type) != 0;
  }

  bool isCooledDown(uint8_t bitPos) {
    return (millis() - lastSent[bitPos]) >= COOLDOWN_MS;
  }

  void markSent(uint8_t bitPos) {
    lastSent[bitPos] = millis();
  }

  // หา bit position จาก bitmask
  static uint8_t bitPos(uint16_t mask) {
    for (uint8_t i = 0; i < 16; i++) if ((mask >> i) & 1) return i;
    return 0;
  }

  // ─── ส่ง HTTP POST ไป Telegram Bot API ────────────────────────
  bool _sendNow(const String& text) {
    if (WiFi.status() != WL_CONNECTED) return false;

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(8);

    HTTPClient http;
    String url = "https://api.telegram.org/bot";
    url += botToken;
    url += "/sendMessage";

    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");

    JsonDocument doc;
    doc["chat_id"]    = chatID;
    doc["text"]       = text;
    doc["parse_mode"] = "HTML";

    String body;
    serializeJson(doc, body);

    int code = http.POST(body);
    http.end();

    if (code == 200) {
      Serial.println("[TG] Sent OK");
      return true;
    }
    Serial.printf("[TG] Send failed HTTP %d\n", code);
    return false;
  }

  // ─── เพิ่มลง queue ────────────────────────────────────────────
  void enqueue(const String& text) {
    if (qCount >= QUEUE_SIZE) {
      Serial.println("[TG] Queue full, dropped");
      return;
    }
    queue[qTail] = { text, millis() };
    qTail = (qTail + 1) % QUEUE_SIZE;
    qCount++;
  }

  // ─── NTP timestamp string ──────────────────────────────────────
  String _ts() {
    if (ntp && ntp->isSynced()) {
      return ntp->dateStr() + " " + ntp->timeStr();
    }
    char buf[16];
    unsigned long s = millis() / 1000;
    snprintf(buf, sizeof(buf), "uptime %lus", s);
    return String(buf);
  }

  // ─── Build relay emoji/label helpers ──────────────────────────
  static const char* _relayIcon(bool on)  { return on ? "🟢" : "⚫"; }
  static const char* _relayLabel(bool on) { return on ? "เปิด (ON)" : "ปิด (OFF)"; }

  String _relayStates() {
    String s;
    for (int i = 0; i < 3; i++) {
      s += _relayIcon(relay[i]->getState());
      s += " R" + String(i+1) + ": " + _relayLabel(relay[i]->getState());
      if (i < 2) s += "\n";
    }
    return s;
  }

  static const char* _aqiIcon(int aqi) {
    switch(aqi) {
      case 1: return "🟢"; case 2: return "🟡";
      case 3: return "🟠"; case 4: return "🔴"; case 5: return "🟣";
      default: return "⚪";
    }
  }

  String _weatherLine() {
    if (!weather) return "";
    const WeatherData& w = weather->getData();
    if (!w.valid) return "🌤 สภาพอากาศ: ไม่มีข้อมูล";
    char buf[80];
    snprintf(buf, sizeof(buf),
      "🌡 %.1f°C  💧%d%%  🌧%d%%  %s PM2.5:%.1f AQI:%d",
      w.temp, w.humidity, w.rainChance,
      _aqiIcon(w.aqi), w.pm25, w.aqi);
    return String(buf);
  }

  String _tempLine() {
    String s;
    if (ds18) {
      char b[32]; snprintf(b, sizeof(b), "🌡 DS18B20: <b>%.2f°C</b>", ds18->getTemp());
      s += b;
    }
    if (xymd) {
      char b[48]; snprintf(b, sizeof(b), "\n🌡 XY-MD03: <b>%.1f°C</b>  💧<b>%.1f%%</b>",
        xymd->getTemperature(), xymd->getHumidity());
      s += b;
    }
    return s;
  }

  String _divider() { return "━━━━━━━━━━━━━━━━━━━━"; }

public:
  DevTelegram(const char* token, const char* chat)
    : botToken(token), chatID(chat) {
    relay[0] = relay[1] = relay[2] = nullptr;
  }

  // ─── Setters ──────────────────────────────────────────────────
  void setRelays(DevRelay* r1, DevRelay* r2, DevRelay* r3) {
    relay[0]=r1; relay[1]=r2; relay[2]=r3;
  }
  void setWeather(DevWeather* w)    { weather = w; }
  void setDS18(DevDS18B20* d)       { ds18 = d; }
  void setXYMD(DevXYMDSensor* x)    { xymd = x; }
  void setNTP(DevNTP* n)            { ntp = n; }
  void setEnabledAlerts(uint16_t m) { enabledAlerts = m; }
  void setTempHighLimit(float v)    { tempHighLimit = v; }
  void setTempLowLimit(float v)     { tempLowLimit = v; }
  void setRainLimit(int v)          { rainHighLimit = v; }
  void setAqiLevel(int v)           { aqiAlertLevel = v; }

  // ─── Getters สำหรับ Dashboard ─────────────────────────────────
  uint16_t getEnabledAlerts()  const { return enabledAlerts; }
  float    getTempHighLimit()  const { return tempHighLimit; }
  float    getTempLowLimit()   const { return tempLowLimit; }
  int      getRainLimit()      const { return rainHighLimit; }
  int      getAqiLevel()       const { return aqiAlertLevel; }
  int      getQueueCount()     const { return qCount; }
  bool     isAlertEnabled(uint16_t mask) const { return (enabledAlerts & mask) != 0; }

  // ส่ง test message ไป Telegram (เรียกจาก dashboard)
  void sendTestMessage() {
    String msg;
    msg  = "<b>🔔 ทดสอบการแจ้งเตือน ESP32</b>\n";
    msg += _divider() + "\n";
    msg += "✅ การเชื่อมต่อ Telegram ทำงานปกติ\n";
    msg += "\n<b>การตั้งค่าปัจจุบัน</b>\n";
    msg += "🌡 Temp High: <b>" + String(tempHighLimit,1) + "°C</b>\n";
    msg += "🧊 Temp Low:  <b>" + String(tempLowLimit,1) + "°C</b>\n";
    msg += "🌧 Rain:      <b>" + String(rainHighLimit) + "%</b>\n";
    msg += "🌫 AQI Level: <b>" + String(aqiAlertLevel) + "</b>\n";
    msg += _divider() + "\n";
    msg += "🕐 <i>" + _ts() + "</i>\n";
    msg += "📍 <i>" OWM_CITY_NAME " · ESP32 Level2</i>";
    enqueue(msg);
  }

  // ─── Public alert triggers ────────────────────────────────────

  // เรียกเมื่อ relay เปลี่ยน (manual/MQTT/web)  source = "Manual"/"MQTT"/"Web"
  void alertRelay(int n, bool newState, const char* source = "Manual") {
    uint16_t type = newState ? TgAlert::RELAY_ON : TgAlert::RELAY_OFF;
    if (!isEnabled(type) || !isCooledDown(bitPos(type))) return;
    markSent(bitPos(type));

    const char* icon = newState ? "⚡" : "🔌";
    String msg;
    msg  = "<b>" + String(icon) + " Relay " + n + " " + (newState?"เปิดแล้ว":"ปิดแล้ว") + "</b>\n";
    msg += _divider() + "\n";
    msg += String(_relayIcon(newState)) + " <b>R" + n + ":</b> " + _relayLabel(newState) + "\n";
    msg += "📌 แหล่งสั่งงาน: <code>" + String(source) + "</code>\n";
    msg += "\n<b>สถานะ Relay ทั้งหมด</b>\n" + _relayStates() + "\n";
    msg += "\n" + _weatherLine() + "\n";
    msg += _divider() + "\n";
    msg += "🕐 <i>" + _ts() + "</i>\n";
    msg += "📍 <i>" OWM_CITY_NAME " · ESP32</i>";
    enqueue(msg);
  }

  // เรียกเมื่อ relay เปิด/ปิดตามตารางเวลา
  void alertSchedule(int n, bool newState, int onH, int onM, int offH, int offM) {
    uint16_t type = newState ? TgAlert::SCHEDULE_ON : TgAlert::SCHEDULE_OFF;
    if (!isEnabled(type) || !isCooledDown(bitPos(type))) return;
    markSent(bitPos(type));

    char timeBuf[16];
    if (newState) snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", onH, onM);
    else          snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", offH, offM);

    char winBuf[12];
    snprintf(winBuf, sizeof(winBuf), "%02d:%02d–%02d:%02d", onH, onM, offH, offM);

    String msg;
    msg  = "<b>⏰ Schedule Relay " + String(n) + " " + (newState?"เปิดอัตโนมัติ":"ปิดอัตโนมัติ") + "</b>\n";
    msg += _divider() + "\n";
    msg += String(_relayIcon(newState)) + " <b>R" + n + ":</b> " + _relayLabel(newState) + "\n";
    msg += "⏱ ตารางเวลา: <code>" + String(winBuf) + "</code>\n";
    msg += "🕐 ทำงานเมื่อ: <code>" + String(timeBuf) + "</code>\n";
    msg += "\n<b>สถานะ Relay ทั้งหมด</b>\n" + _relayStates() + "\n";
    msg += _divider() + "\n";
    msg += "🕐 <i>" + _ts() + "</i>\n";
    msg += "📍 <i>" OWM_CITY_NAME " · ESP32</i>";
    enqueue(msg);
  }

  // เรียกจาก loop() เมื่อตรวจพบอุณหภูมิสูง/ต่ำ
  void checkTempAlert() {
    if (!ds18) return;
    float t = ds18->getTemp();
    if (ds18->isSimMode()) return;

    if (t >= tempHighLimit) {
      uint16_t type = TgAlert::TEMP_HIGH;
      if (isEnabled(type) && isCooledDown(bitPos(type))) {
        markSent(bitPos(type));
        char buf[24]; snprintf(buf, sizeof(buf), "%.2f", t);
        String msg;
        msg  = "<b>🔥 อุณหภูมิสูงเกินกำหนด!</b>\n";
        msg += _divider() + "\n";
        msg += "🌡 DS18B20: <b>" + String(buf) + "°C</b>\n";
        msg += "⚠️ เกินขีดจำกัด: <b>" + String(tempHighLimit,1) + "°C</b>\n";
        msg += "\n" + _tempLine() + "\n";
        msg += "\n<b>สถานะ Relay</b>\n" + _relayStates() + "\n";
        msg += _divider() + "\n";
        msg += "🕐 <i>" + _ts() + "</i>\n";
        msg += "📍 <i>" OWM_CITY_NAME " · ESP32</i>";
        enqueue(msg);
      }
    } else if (t <= tempLowLimit) {
      uint16_t type = TgAlert::TEMP_LOW;
      if (isEnabled(type) && isCooledDown(bitPos(type))) {
        markSent(bitPos(type));
        char buf[24]; snprintf(buf, sizeof(buf), "%.2f", t);
        String msg;
        msg  = "<b>🧊 อุณหภูมิต่ำเกินกำหนด!</b>\n";
        msg += _divider() + "\n";
        msg += "🌡 DS18B20: <b>" + String(buf) + "°C</b>\n";
        msg += "⚠️ ต่ำกว่าขีดจำกัด: <b>" + String(tempLowLimit,1) + "°C</b>\n";
        msg += "\n" + _tempLine() + "\n";
        msg += "\n<b>สถานะ Relay</b>\n" + _relayStates() + "\n";
        msg += _divider() + "\n";
        msg += "🕐 <i>" + _ts() + "</i>\n";
        msg += "📍 <i>" OWM_CITY_NAME " · ESP32</i>";
        enqueue(msg);
      }
    }
  }

  // เรียกจาก loop() เมื่อ weather อัปเดต
  void checkWeatherAlert() {
    if (!weather) return;
    const WeatherData& w = weather->getData();
    if (!w.valid) return;

    // AQI poor
    if (w.aqi >= aqiAlertLevel) {
      uint16_t type = TgAlert::AQI_POOR;
      if (isEnabled(type) && isCooledDown(bitPos(type))) {
        markSent(bitPos(type));
        String msg;
        msg  = "<b>" + String(_aqiIcon(w.aqi)) + " คุณภาพอากาศแย่!</b>\n";
        msg += _divider() + "\n";
        msg += String(_aqiIcon(w.aqi)) + " AQI: <b>" + w.aqi + " (" + aqiLabel(w.aqi) + ")</b>\n";
        msg += "🌫 PM2.5: <b>" + String(w.pm25, 1) + " µg/m³</b>\n";
        msg += "🌡 อุณหภูมิ: " + String(w.temp, 1) + "°C  💧ความชื้น: " + w.humidity + "%\n";
        msg += "\n<b>สถานะ Relay</b>\n" + _relayStates() + "\n";
        msg += _divider() + "\n";
        msg += "🕐 <i>" + _ts() + "</i>\n";
        msg += "📍 <i>" OWM_CITY_NAME " · ESP32</i>";
        enqueue(msg);
      }
    }

    // Rain high
    if (w.rainChance >= rainHighLimit) {
      uint16_t type = TgAlert::RAIN_HIGH;
      if (isEnabled(type) && isCooledDown(bitPos(type))) {
        markSent(bitPos(type));
        String msg;
        msg  = "<b>🌧 โอกาสฝนตกสูง!</b>\n";
        msg += _divider() + "\n";
        msg += "🌧 โอกาสฝน: <b>" + String(w.rainChance) + "%</b>\n";
        msg += "🌡 อุณหภูมิ: " + String(w.temp, 1) + "°C\n";
        msg += "💧 ความชื้น: " + String(w.humidity) + "%\n";
        msg += _divider() + "\n";
        msg += "🕐 <i>" + _ts() + "</i>\n";
        msg += "📍 <i>" OWM_CITY_NAME " · ESP32</i>";
        enqueue(msg);
      }
    }
  }

  // ส่งสรุปสถานะตอนเปิดเครื่อง
  void sendBootAlert(uint32_t bootCount) {
    if (!isEnabled(TgAlert::BOOT)) return;
    String ip = WiFi.localIP().toString();

    String msg;
    msg  = "<b>🚀 ESP32 เปิดเครื่องแล้ว</b>\n";
    msg += _divider() + "\n";
    msg += "🔢 Boot ครั้งที่: <b>" + String(bootCount) + "</b>\n";
    msg += "🌐 IP: <code>" + ip + "</code>\n";
    msg += "📶 WiFi: <code>" + WiFi.SSID() + "</code>  RSSI: " + WiFi.RSSI() + " dBm\n";
    msg += "💾 Free Heap: " + String(ESP.getFreeHeap()/1024) + " KB\n";
    msg += "\n<b>สถานะ Relay เริ่มต้น</b>\n" + _relayStates() + "\n";
    if (weather) msg += "\n" + _weatherLine() + "\n";
    msg += _divider() + "\n";
    msg += "🕐 <i>" + _ts() + "</i>\n";
    msg += "📍 <i>" OWM_CITY_NAME " · ESP32 Level2</i>";
    enqueue(msg);
  }

  // ส่งรายงานสถานะตามต้องการ (เรียกจากภายนอก)
  void sendStatusReport() {
    String ip = WiFi.localIP().toString();
    String msg;
    msg  = "<b>📊 รายงานสถานะ ESP32</b>\n";
    msg += _divider() + "\n";
    msg += "<b>⚡ Relay</b>\n" + _relayStates() + "\n";
    msg += "\n<b>🌡 เซนเซอร์</b>\n" + _tempLine() + "\n";
    if (weather) msg += "\n<b>🌤 สภาพอากาศ</b>\n" + _weatherLine() + "\n";
    msg += "\n<b>📡 เครือข่าย</b>\n";
    msg += "🌐 IP: <code>" + ip + "</code>\n";
    msg += "📶 RSSI: " + String(WiFi.RSSI()) + " dBm\n";
    msg += "💾 Heap: " + String(ESP.getFreeHeap()/1024) + " KB\n";
    msg += "⏱ Uptime: ";
    unsigned long s = millis()/1000;
    char up[24]; snprintf(up, sizeof(up), "%luh %lum %lus", s/3600, (s%3600)/60, s%60);
    msg += String(up) + "\n";
    msg += _divider() + "\n";
    msg += "🕐 <i>" + _ts() + "</i>\n";
    msg += "📍 <i>" OWM_CITY_NAME " · ESP32 Level2</i>";
    enqueue(msg);
  }

  // ─── loop() — drain queue ทีละ 1 ข้อ/วินาที ──────────────────
  void loop() {
    if (qCount == 0) return;
    if (millis() - lastSentAt < 1100) return;  // Telegram: max ~1 msg/s

    QueueItem& item = queue[qHead];
    if (_sendNow(item.text)) {
      lastSentAt = millis();
    } else {
      // ถ้าส่งไม่ได้และ queue เกิน 30s ให้ drop
      if (millis() - item.queuedAt > 30000) {
        Serial.println("[TG] Dropped stale message");
      } else {
        return; // รอรอบหน้า
      }
    }
    qHead = (qHead + 1) % QUEUE_SIZE;
    qCount--;
  }
};

#endif // DEV_TELEGRAM_H
