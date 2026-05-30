#ifndef DEV_STATE_STORE_H
#define DEV_STATE_STORE_H

#include <Arduino.h>
#include <Preferences.h>

// บันทึก/กู้คืน state ลง NVS flash (ESP32 built-in)
// เขียนเฉพาะเมื่อค่าเปลี่ยนจริง — ไม่ wear flash โดยไม่จำเป็น
//
// NVS namespace: "appstate"
// Keys:
//   r1, r2, r3  → bool  relay state
//   boot_cnt    → uint32 boot counter

class DevStateStore {
private:
  Preferences prefs;

  // cache ค่าที่โหลดมา เพื่อ compare ก่อนเขียน
  bool     _r[3]    = {false, false, false};
  uint32_t _bootCnt = 0;

  static const char* NS;  // "appstate"

public:
  // โหลดค่าจาก NVS — เรียกใน setup() ก่อน begin relay
  void load() {
    prefs.begin(NS, false);  // read-write

    _r[0] = prefs.getBool("r1", false);
    _r[1] = prefs.getBool("r2", false);
    _r[2] = prefs.getBool("r3", false);

    // นับจำนวน boot
    _bootCnt = prefs.getUInt("boot_cnt", 0) + 1;
    prefs.putUInt("boot_cnt", _bootCnt);

    prefs.end();

    Serial.printf("[State] Loaded — R1:%d R2:%d R3:%d  boot#%u\n",
                  _r[0], _r[1], _r[2], _bootCnt);
  }

  // บันทึก relay state — เขียน NVS เฉพาะเมื่อค่าเปลี่ยน
  void saveRelay(uint8_t n, bool state) {  // n = 1..3
    if (n < 1 || n > 3) return;
    uint8_t i = n - 1;
    if (_r[i] == state) return;  // ไม่เปลี่ยน → ไม่เขียน

    _r[i] = state;
    const char* keys[] = {"r1", "r2", "r3"};
    prefs.begin(NS, false);
    prefs.putBool(keys[i], state);
    prefs.end();

    Serial.printf("[State] Saved R%d=%s\n", n, state ? "ON" : "OFF");
  }

  // บันทึก relay ทั้งหมดพร้อมกัน (ประหยัด open/close)
  void saveAllRelays(bool r1, bool r2, bool r3) {
    bool changed = (_r[0] != r1 || _r[1] != r2 || _r[2] != r3);
    if (!changed) return;

    _r[0] = r1;  _r[1] = r2;  _r[2] = r3;
    prefs.begin(NS, false);
    prefs.putBool("r1", r1);
    prefs.putBool("r2", r2);
    prefs.putBool("r3", r3);
    prefs.end();

    Serial.printf("[State] Saved R1:%d R2:%d R3:%d\n", r1, r2, r3);
  }

  // Getters สำหรับ restore ใน setup()
  bool     relayState(uint8_t n) const { return (n >= 1 && n <= 3) ? _r[n-1] : false; }
  uint32_t bootCount()           const { return _bootCnt; }

  // ล้าง NVS ทั้งหมด (factory reset)
  void clear() {
    prefs.begin(NS, false);
    prefs.clear();
    prefs.end();
    _r[0] = _r[1] = _r[2] = false;
    Serial.println("[State] Cleared all NVS");
  }
};

const char* DevStateStore::NS = "appstate";

#endif // DEV_STATE_STORE_H
