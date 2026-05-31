#ifndef DEV_RELAY_SCHEDULER_H
#define DEV_RELAY_SCHEDULER_H

#include <Arduino.h>
#include <Preferences.h>
#include "DevRelay.h"
#include "DevNTP.h"

// ── Relay Schedule Entry ───────────────────────────────────────────
// แต่ละ entry คือ "เปิดที่เวลา onHH:onMM  ปิดที่เวลา offHH:offMM"
// สามารถกำหนด day-of-week mask ได้ (bit0=Sun … bit6=Sat, 0x7F=ทุกวัน)
struct RelayScheduleEntry {
  bool     enabled    = false;
  uint8_t  onHour     = 6;
  uint8_t  onMinute   = 0;
  uint8_t  offHour    = 22;
  uint8_t  offMinute  = 0;
  uint8_t  dayMask    = 0x7F;  // bit0=Sun…bit6=Sat  0x7F = ทุกวัน

  // ตรวจว่าวันนี้ตรงกับ dayMask
  bool matchesDay(int wday) const {
    return (dayMask >> wday) & 0x01;
  }

  // ตรวจว่าเวลา hh:mm ตรงกับเวลาเปิด
  bool isOnTime(int h, int m) const {
    return enabled && h == onHour && m == onMinute;
  }

  // ตรวจว่าเวลา hh:mm ตรงกับเวลาปิด
  bool isOffTime(int h, int m) const {
    return enabled && h == offHour && m == offMinute;
  }

  // ตรวจว่าเวลา h:m อยู่ใน window เปิด (สำหรับ restore หลัง reboot)
  bool isWithinOnWindow(int h, int m) const {
    if (!enabled) return false;
    int now  = h * 60 + m;
    int on   = onHour  * 60 + onMinute;
    int off  = offHour * 60 + offMinute;
    if (on < off) {
      return now >= on && now < off;
    } else {
      // ข้ามเที่ยงคืน
      return now >= on || now < off;
    }
  }
};

// ── DevRelayScheduler ─────────────────────────────────────────────
// จัดการ Schedule สำหรับ relay 3 ตัว  แต่ละตัวมีได้ 1 entry
// บันทึก/กู้คืนจาก NVS Preferences namespace "sched"
class DevRelayScheduler {
public:
  static const int NUM_RELAYS = 3;

private:
  DevRelay*           relay[NUM_RELAYS];
  DevNTP*             ntp;
  RelayScheduleEntry  sched[NUM_RELAYS];

  void (*onRelayChange)() = nullptr;

  // NVS keys  sN_en, sN_onh, sN_onm, sN_ofh, sN_ofm, sN_day
  static const char* NS;

  void saveOne(Preferences& p, int i) {
    char key[10];
    const RelayScheduleEntry& e = sched[i];
    snprintf(key, sizeof(key), "s%d_en",  i); p.putBool(key,  e.enabled);
    snprintf(key, sizeof(key), "s%d_onh", i); p.putUChar(key, e.onHour);
    snprintf(key, sizeof(key), "s%d_onm", i); p.putUChar(key, e.onMinute);
    snprintf(key, sizeof(key), "s%d_ofh", i); p.putUChar(key, e.offHour);
    snprintf(key, sizeof(key), "s%d_ofm", i); p.putUChar(key, e.offMinute);
    snprintf(key, sizeof(key), "s%d_day", i); p.putUChar(key, e.dayMask);
  }

  void loadOne(Preferences& p, int i) {
    char key[10];
    RelayScheduleEntry& e = sched[i];
    snprintf(key, sizeof(key), "s%d_en",  i); e.enabled   = p.getBool(key,  false);
    snprintf(key, sizeof(key), "s%d_onh", i); e.onHour    = p.getUChar(key, 6);
    snprintf(key, sizeof(key), "s%d_onm", i); e.onMinute  = p.getUChar(key, 0);
    snprintf(key, sizeof(key), "s%d_ofh", i); e.offHour   = p.getUChar(key, 22);
    snprintf(key, sizeof(key), "s%d_ofm", i); e.offMinute = p.getUChar(key, 0);
    snprintf(key, sizeof(key), "s%d_day", i); e.dayMask   = p.getUChar(key, 0x7F);
  }

  // ─── tick logic (เรียกทุก 1 วินาที) ───────────────────────────
  int  lastTickMin  = -1;   // ป้องกัน fire ซ้ำใน minute เดียวกัน
  bool didApplyInit = false; // restore ครั้งแรกหลัง boot

  void applyInitialState() {
    // หลัง boot+NTP sync — ตรวจว่าตอนนี้อยู่ใน window เปิดหรือไม่
    struct tm t;
    if (!ntp->getTime(t)) return;
    int h = t.tm_hour, m = t.tm_min, wd = t.tm_wday;
    bool changed = false;
    for (int i = 0; i < NUM_RELAYS; i++) {
      const RelayScheduleEntry& e = sched[i];
      if (!e.enabled || !e.matchesDay(wd)) continue;
      bool shouldBeOn = e.isWithinOnWindow(h, m);
      if (relay[i]->getState() != shouldBeOn) {
        relay[i]->setState(shouldBeOn);
        changed = true;
        Serial.printf("[Sched] Relay%d restored → %s (window check)\n",
                      i + 1, shouldBeOn ? "ON" : "OFF");
      }
    }
    didApplyInit = true;
    if (changed && onRelayChange) onRelayChange();
  }

public:
  DevRelayScheduler(DevRelay* r1, DevRelay* r2, DevRelay* r3, DevNTP* ntpRef)
    : ntp(ntpRef) {
    relay[0] = r1;
    relay[1] = r2;
    relay[2] = r3;
  }

  void setOnRelayChange(void (*cb)()) { onRelayChange = cb; }

  // โหลดจาก NVS
  void load() {
    Preferences p;
    p.begin(NS, true); // read-only
    for (int i = 0; i < NUM_RELAYS; i++) loadOne(p, i);
    p.end();
    Serial.println("[Sched] Loaded schedules from NVS");
  }

  // บันทึก entry เดียว
  void saveEntry(int n) {  // n = 0-2
    if (n < 0 || n >= NUM_RELAYS) return;
    Preferences p;
    p.begin(NS, false);
    saveOne(p, n);
    p.end();
    Serial.printf("[Sched] Saved relay%d schedule\n", n + 1);
  }

  // บันทึกทั้งหมด
  void saveAll() {
    Preferences p;
    p.begin(NS, false);
    for (int i = 0; i < NUM_RELAYS; i++) saveOne(p, i);
    p.end();
  }

  // อัปเดต entry แล้วบันทึก
  void setEntry(int n, const RelayScheduleEntry& e) {
    if (n < 0 || n >= NUM_RELAYS) return;
    sched[n] = e;
    saveEntry(n);
    Serial.printf("[Sched] Relay%d: %s %02d:%02d→%02d:%02d day=0x%02X\n",
                  n + 1, e.enabled ? "ON" : "OFF",
                  e.onHour, e.onMinute, e.offHour, e.offMinute, e.dayMask);
  }

  const RelayScheduleEntry& getEntry(int n) const {
    static RelayScheduleEntry dummy;
    if (n < 0 || n >= NUM_RELAYS) return dummy;
    return sched[n];
  }

  // เรียกใน loop() — ตรวจทุก 1 วินาที fire เมื่อถึงนาทีใหม่
  void loop() {
    if (!ntp->isSynced()) return;

    // Restore ครั้งแรกหลัง sync
    if (!didApplyInit) {
      applyInitialState();
      return;
    }

    struct tm t;
    if (!ntp->getTime(t)) return;

    int currentMin = t.tm_hour * 60 + t.tm_min;
    if (currentMin == lastTickMin) return;  // ยังอยู่ minute เดิม
    lastTickMin = currentMin;

    int h  = t.tm_hour;
    int m  = t.tm_min;
    int wd = t.tm_wday;

    bool changed = false;
    for (int i = 0; i < NUM_RELAYS; i++) {
      const RelayScheduleEntry& e = sched[i];
      if (!e.enabled || !e.matchesDay(wd)) continue;

      if (e.isOnTime(h, m) && !relay[i]->getState()) {
        relay[i]->on();
        changed = true;
        Serial.printf("[Sched] Relay%d ON (scheduled %02d:%02d)\n",
                      i + 1, h, m);
      } else if (e.isOffTime(h, m) && relay[i]->getState()) {
        relay[i]->off();
        changed = true;
        Serial.printf("[Sched] Relay%d OFF (scheduled %02d:%02d)\n",
                      i + 1, h, m);
      }
    }

    if (changed && onRelayChange) onRelayChange();
  }
};

const char* DevRelayScheduler::NS = "sched";

#endif // DEV_RELAY_SCHEDULER_H
