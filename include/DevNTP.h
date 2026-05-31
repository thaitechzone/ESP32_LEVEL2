#ifndef DEV_NTP_H
#define DEV_NTP_H

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>

// NTP server — syncs system time via configTzTime() (POSIX TZ string)
// Asia/Bangkok = UTC+7, ไม่มี DST  →  "ICT-7"
// ใช้ time.h ของ ESP-IDF ที่มาพร้อม Arduino-ESP32 ไม่ต้องติดตั้ง lib เพิ่ม
class DevNTP {
private:
  const char* server1;
  const char* server2;
  const char* tzPosix;   // POSIX TZ string

  unsigned long lastSync   = 0;
  bool          synced     = false;
  static const unsigned long SYNC_INTERVAL = 3600000UL; // re-sync ทุก 1 ชม.

public:
  // tzPosix default = Asia/Bangkok (ICT UTC+7, ไม่มี DST)
  DevNTP(const char* srv1   = "th.pool.ntp.org",
         const char* srv2   = "pool.ntp.org",
         const char* tz     = "ICT-7")
    : server1(srv1), server2(srv2), tzPosix(tz) {}

  // เรียกหลัง WiFi connected — block ได้สูงสุด timeout_ms
  bool begin(unsigned long timeout_ms = 8000) {
    // configTzTime ตั้ง TZ env + NTP server ในครั้งเดียว
    configTzTime(tzPosix, server1, server2);
    Serial.printf("[NTP] TZ=%s  Syncing %s ...\n", tzPosix, server1);

    unsigned long t0 = millis();
    struct tm info;
    while (!getLocalTime(&info)) {
      if (millis() - t0 > timeout_ms) {
        Serial.println("[NTP] Sync timeout");
        return false;
      }
      delay(200);
    }
    synced   = true;
    lastSync = millis();
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &info);
    Serial.printf("[NTP] Synced: %s\n", buf);
    return true;
  }

  // เรียกใน loop() — re-sync อัตโนมัติทุก 1 ชม.
  void loop() {
    if (synced && (millis() - lastSync >= SYNC_INTERVAL)) {
      configTzTime(tzPosix, server1, server2);
      lastSync = millis();
      Serial.println("[NTP] Re-synced");
    }
  }

  bool isSynced() const { return synced; }

  // คืน struct tm ปัจจุบัน — valid เฉพาะเมื่อ isSynced()
  bool getTime(struct tm& out) const {
    return getLocalTime(&out);
  }

  // คืน HH:MM:SS string
  String timeStr() const {
    struct tm t;
    if (!getLocalTime(&t)) return "--:--:--";
    char buf[12];
    strftime(buf, sizeof(buf), "%H:%M:%S", &t);
    return String(buf);
  }

  // คืน YYYY-MM-DD string
  String dateStr() const {
    struct tm t;
    if (!getLocalTime(&t)) return "--";
    char buf[16];
    strftime(buf, sizeof(buf), "%Y-%m-%d", &t);
    return String(buf);
  }

  // คืนเวลาเป็นจำนวนวินาทีนับจากเที่ยงคืน (0–86399)
  int secondsOfDay() const {
    struct tm t;
    if (!getLocalTime(&t)) return -1;
    return t.tm_hour * 3600 + t.tm_min * 60 + t.tm_sec;
  }

  // คืน hour (0-23) / minute (0-59)
  int hour()   const { struct tm t; return getLocalTime(&t) ? t.tm_hour : -1; }
  int minute() const { struct tm t; return getLocalTime(&t) ? t.tm_min  : -1; }
  int second() const { struct tm t; return getLocalTime(&t) ? t.tm_sec  : -1; }

  // วันในสัปดาห์ 0=Sunday … 6=Saturday
  int weekday() const { struct tm t; return getLocalTime(&t) ? t.tm_wday : -1; }
};

#endif // DEV_NTP_H
