#ifndef DEV_STATE_MACHINE_H
#define DEV_STATE_MACHINE_H

#include <Arduino.h>
#include "DevSwitch.h"
#include "DevRelay.h"
#include "DevStateStore.h"

// ═══════════════════════════════════════════════════════════════
//  State diagram
//
//  MONITOR ──[SW1]──► MENU
//  MONITOR ◄──[SW1 hold 2s]── (any state)
//
//  MENU: cursor เลือก item
//    ├─ [SW3/UP]   → cursor ขึ้น
//    ├─ [SW2/DOWN] → cursor ลง
//    └─ [SW1/ENTER]→ เข้า sub-state ตาม cursor
//
//  RELAY_CTRL: เลือก relay (cursor) แล้วสั่ง ON/OFF
//    ├─ [SW3/UP]   → relay ที่ cursor → ON
//    ├─ [SW2/DOWN] → relay ที่ cursor → OFF
//    ├─ [SW1]      → toggle relay ที่ cursor
//    └─ hold SW1   → กลับ MENU
//
//  SETTINGS: เลือก setting item
//    ├─ [SW3/UP / SW2/DOWN] → เลื่อน cursor
//    ├─ [SW1] on "WiFi Reset" → CONFIRM
//    └─ [SW1] on "Back"       → MENU
//
//  CONFIRM: ยืนยัน action อันตราย
//    ├─ [SW3/UP]   → YES
//    ├─ [SW2/DOWN] → NO (cancel)
//    └─ [SW1]      → execute ตาม cursor แล้ว → MONITOR
//
// ═══════════════════════════════════════════════════════════════

enum class AppState : uint8_t {
  MONITOR    = 0,
  MENU       = 1,
  RELAY_CTRL = 2,
  SETTINGS   = 3,
  CONFIRM    = 4,
};

// Menu items
enum class MenuItem : uint8_t {
  RELAY_CTRL = 0,
  SETTINGS   = 1,
  BACK       = 2,
  MENU_COUNT = 3,
};

// Settings items
enum class SettingItem : uint8_t {
  WIFI_RESET   = 0,
  OLED_TIMEOUT = 1,   // สลับหน้าช้า/เร็ว
  BACK         = 2,
  SETTING_COUNT = 3,
};

// Confirm actions
enum class ConfirmAction : uint8_t {
  WIFI_RESET = 0,
};

// ── labels ───────────────────────────────────────────────────────
static const char* MENU_LABELS[] = {
  "Relay Control",
  "Settings",
  "< Back",
};

static const char* SETTING_LABELS[] = {
  "WiFi Reset",
  "OLED Speed",
  "< Back",
};

static const char* RELAY_LABELS[] = { "Relay 1", "Relay 2", "Relay 3" };

// ═══════════════════════════════════════════════════════════════
class DevStateMachine {
public:
  // ── event ที่ state machine ส่งออกมาให้ main.cpp handle ──────
  enum class Event : uint8_t {
    NONE,
    RELAY_CHANGED,    // relay N เปลี่ยนสถานะ → save + MQTT + OLED
    WIFI_RESET,       // ให้ทำ WiFi factory reset แล้ว restart
    OLED_SPEED_TOGGLE,// สลับ OLED page interval
    REDRAW,           // วาด OLED ใหม่ (data ไม่เปลี่ยน)
  };

  struct Result {
    Event   event    = Event::NONE;
    uint8_t relayNum = 0;  // 1..3 เมื่อ event == RELAY_CHANGED

    Result() = default;
    Result(Event e) : event(e), relayNum(0) {}
    Result(Event e, uint8_t n) : event(e), relayNum(n) {}
  };

private:
  // ── hardware refs ─────────────────────────────────────────────
  DevSwitch&  sw1;   // MODE / ENTER
  DevSwitch&  sw2;   // DOWN
  DevSwitch&  sw3;   // UP
  DevRelay*   relay[3];

  // ── state ─────────────────────────────────────────────────────
  AppState      _state  = AppState::MONITOR;
  uint8_t       _cursor = 0;   // cursor ใน menu/relay/settings
  ConfirmAction _confirmAction = ConfirmAction::WIFI_RESET;

  // ── hold detection สำหรับ SW1 ─────────────────────────────────
  unsigned long _sw1PressAt  = 0;
  bool          _sw1Holding  = false;
  bool          _holdFired   = false;
  static const unsigned long HOLD_MS = 2000;  // 2 วินาที → กลับ MONITOR

  // ── OLED page speed toggle state ──────────────────────────────
  bool _oledFast = false;   // false=5s, true=2s

  // ── helpers ───────────────────────────────────────────────────
  uint8_t _menuCount()    const { return (uint8_t)MenuItem::MENU_COUNT; }
  uint8_t _settingCount() const { return (uint8_t)SettingItem::SETTING_COUNT; }

  void _enterState(AppState s) {
    _state  = s;
    _cursor = 0;
  }

  // ── process hold บน SW1 ──────────────────────────────────────
  // คืน true เมื่อ hold ครบ (fire once per hold)
  bool _checkHold() {
    if (sw1.isPressed()) {
      if (!_sw1Holding) {
        _sw1Holding = true;
        _sw1PressAt = millis();
        _holdFired  = false;
      } else if (!_holdFired && millis() - _sw1PressAt >= HOLD_MS) {
        _holdFired = true;
        return true;
      }
    } else {
      _sw1Holding = false;
      _holdFired  = false;
    }
    return false;
  }

  // ── handle MONITOR state ──────────────────────────────────────
  Result _handleMonitor() {
    if (sw1.wasPressed()) {
      _enterState(AppState::MENU);
      return {Event::REDRAW};
    }
    return {};
  }

  // ── handle MENU state ────────────────────────────────────────
  Result _handleMenu() {
    if (sw3.wasPressed()) {  // UP
      _cursor = (_cursor == 0) ? _menuCount() - 1 : _cursor - 1;
      return {Event::REDRAW};
    }
    if (sw2.wasPressed()) {  // DOWN
      _cursor = (_cursor + 1) % _menuCount();
      return {Event::REDRAW};
    }
    if (sw1.wasPressed()) {  // ENTER
      switch ((MenuItem)_cursor) {
        case MenuItem::RELAY_CTRL:
          _enterState(AppState::RELAY_CTRL);
          return {Event::REDRAW};
        case MenuItem::SETTINGS:
          _enterState(AppState::SETTINGS);
          return {Event::REDRAW};
        case MenuItem::BACK:
          _enterState(AppState::MONITOR);
          return {Event::REDRAW};
        default: break;
      }
    }
    return {};
  }

  // ── handle RELAY_CTRL state ──────────────────────────────────
  Result _handleRelayCtrl() {
    if (sw3.wasPressed()) {  // UP → relay ON
      relay[_cursor]->on();
      return {Event::RELAY_CHANGED, (uint8_t)(_cursor + 1)};
    }
    if (sw2.wasPressed()) {  // DOWN → relay OFF
      relay[_cursor]->off();
      return {Event::RELAY_CHANGED, (uint8_t)(_cursor + 1)};
    }
    if (sw1.wasPressed()) {  // ENTER → เลื่อน cursor (เลือก relay ถัดไป)
      _cursor = (_cursor + 1) % 3;
      return {Event::REDRAW};
    }
    return {};
  }

  // ── handle SETTINGS state ────────────────────────────────────
  Result _handleSettings() {
    if (sw3.wasPressed()) {  // UP
      _cursor = (_cursor == 0) ? _settingCount() - 1 : _cursor - 1;
      return {Event::REDRAW};
    }
    if (sw2.wasPressed()) {  // DOWN
      _cursor = (_cursor + 1) % _settingCount();
      return {Event::REDRAW};
    }
    if (sw1.wasPressed()) {  // ENTER
      switch ((SettingItem)_cursor) {
        case SettingItem::WIFI_RESET:
          _confirmAction = ConfirmAction::WIFI_RESET;
          _cursor = 0;  // YES highlighted by default
          _state  = AppState::CONFIRM;
          return {Event::REDRAW};
        case SettingItem::OLED_TIMEOUT:
          _oledFast = !_oledFast;
          return Result(Event::OLED_SPEED_TOGGLE);
        case SettingItem::BACK:
          _enterState(AppState::MENU);
          return {Event::REDRAW};
        default: break;
      }
    }
    return {};
  }

  // ── handle CONFIRM state ─────────────────────────────────────
  // cursor: 0=YES, 1=NO
  Result _handleConfirm() {
    if (sw3.wasPressed()) { _cursor = 0; return {Event::REDRAW}; }  // UP → YES
    if (sw2.wasPressed()) { _cursor = 1; return {Event::REDRAW}; }  // DOWN → NO
    if (sw1.wasPressed()) {  // ENTER
      if (_cursor == 0) {
        // YES — execute
        _enterState(AppState::MONITOR);
        switch (_confirmAction) {
          case ConfirmAction::WIFI_RESET:
            return {Event::WIFI_RESET};
        }
      } else {
        // NO — cancel
        _enterState(AppState::SETTINGS);
        return {Event::REDRAW};
      }
    }
    return {};
  }

public:
  DevStateMachine(DevSwitch& sw1, DevSwitch& sw2, DevSwitch& sw3,
                  DevRelay* r1, DevRelay* r2, DevRelay* r3)
    : sw1(sw1), sw2(sw2), sw3(sw3) {
    relay[0] = r1; relay[1] = r2; relay[2] = r3;
  }

  // เรียกใน loop() — คืน Result ให้ main.cpp handle
  Result update() {
    // hold SW1 จากทุก state → กลับ MONITOR
    if (_state != AppState::MONITOR && _checkHold()) {
      _enterState(AppState::MONITOR);
      return {Event::REDRAW};
    }

    switch (_state) {
      case AppState::MONITOR:    return _handleMonitor();
      case AppState::MENU:       return _handleMenu();
      case AppState::RELAY_CTRL: return _handleRelayCtrl();
      case AppState::SETTINGS:   return _handleSettings();
      case AppState::CONFIRM:    return _handleConfirm();
    }
    return {};
  }

  // ── Getters สำหรับ OLED ──────────────────────────────────────
  AppState      getState()        const { return _state; }
  uint8_t       getCursor()       const { return _cursor; }
  ConfirmAction getConfirmAction() const { return _confirmAction; }
  bool          isOledFast()      const { return _oledFast; }

  // label helpers
  const char* menuLabel(uint8_t i)    const { return MENU_LABELS[i]; }
  const char* settingLabel(uint8_t i) const { return SETTING_LABELS[i]; }
  const char* relayLabel(uint8_t i)   const { return RELAY_LABELS[i]; }
  bool        relayState(uint8_t i)   const { return relay[i]->getState(); }  // i=0..2
};

#endif // DEV_STATE_MACHINE_H
