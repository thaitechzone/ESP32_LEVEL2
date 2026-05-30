#include <Arduino.h>
#include <WiFi.h>

#include "DevIsoInput.h"
#include "DevSwitch.h"
#include "DevRelay.h"
#include "DevPZEM.h"
#include "DevXYMDSensor.h"
#include "DevOLED.h"
#include "DevWifiManager.h"
#include "DevWeather.h"
#include "DevDS18B20.h"
#include "DevMQTT.h"
#include "DevWebServer.h"
#include "DevStateStore.h"
#include "DevStateMachine.h"

// ── Hardware ──────────────────────────────────────────────────
// SW1=MODE/ENTER  SW2=DOWN  SW3=UP
DevSwitch sw1(34, false);   // MODE / ENTER
DevSwitch sw2(35, false);   // DOWN
DevSwitch sw3(32, false);   // UP

DevRelay relay1(17, true);
DevRelay relay2(16, true);
DevRelay relay3(4,  true);

DevOLED oled;
DevWifiManager wifiMgr(&oled, "ESP32-Setup");
DevWeather weather;
DevDS18B20 ds18(14);
DevXYMDSensor xymd(&Serial, 2, 5000);
DevMQTT mqtt(&relay1, &relay2, &relay3, &weather, &ds18, &xymd);
DevWebServer webServer(&relay1, &relay2, &relay3, &weather, &ds18, &xymd);
DevStateStore stateStore;
DevStateMachine sm(sw1, sw2, sw3, &relay1, &relay2, &relay3);

// ── forward declarations ───────────────────────────────────────
static void _drawCurrentState();
static void _onRelayChanged(int n);

// ── OLED: วาดหน้าจอตาม State ปัจจุบัน ────────────────────────
static void _drawCurrentState() {
  using S = AppState;
  switch (sm.getState()) {

    case S::MONITOR:
      // หน้าจอหลัก — แสดง sensor + weather (auto-cycle ผ่าน oled.tick())
      {
        const WeatherData& w = weather.getData();
        String ip = WiFi.localIP().toString();
        oled.showMain(
          ds18.getTemp(),        ds18.isSimMode(),
          xymd.getTemperature(), xymd.getHumidity(), xymd.isSimMode(),
          w.valid ? w.temp       : 0,
          w.valid ? w.humidity   : 0,
          w.valid ? w.rainChance : 0,
          w.valid ? w.pm25       : 0,
          w.valid ? w.aqi        : 0,
          w.valid ? aqiLabel(w.aqi) : "--",
          relay1.getState(), relay2.getState(), relay3.getState(),
          ip.c_str()
        );
      }
      break;

    case S::MENU:
      oled.showMenu(sm.getCursor());
      break;

    case S::RELAY_CTRL:
      oled.showRelayCtrl(sm.getCursor(),
                         relay1.getState(),
                         relay2.getState(),
                         relay3.getState());
      break;

    case S::SETTINGS:
      oled.showSettings(sm.getCursor(), sm.isOledFast());
      break;

    case S::CONFIRM:
      oled.showConfirm("WiFi Reset!", sm.getCursor());
      break;
  }
}

// ── Relay เปลี่ยนสถานะ (ทุก source) ──────────────────────────
static void _onRelayChanged(int n) {
  stateStore.saveAllRelays(relay1.getState(), relay2.getState(), relay3.getState());
  if (n > 0) mqtt.publishRelayState(n);
  else {
    mqtt.publishRelayState(1);
    mqtt.publishRelayState(2);
    mqtt.publishRelayState(3);
  }
  _drawCurrentState();
}

// ── WiFi reset hold ใน setup (ก่อน state machine ทำงาน) ──────
static bool checkWifiResetHold(int holdSec = 5) {
  sw1.begin();
  if (!sw1.readRawState()) return false;
  for (int remain = holdSec; remain > 0; remain--) {
    oled.showCountdown(remain, holdSec);
    unsigned long tick = millis();
    while (millis() - tick < 1000) {
      if (!sw1.readRawState()) {
        oled.showMessage("WiFi Reset", "Cancelled", "");
        delay(1000);
        return false;
      }
      delay(50);
    }
  }
  oled.showMessage("WiFi Reset", "Resetting...", "");
  delay(800);
  return true;
}

// ── Setup ─────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  oled.begin(21, 22);
  oled.showMessage("Booting...", "", "");

  // โหลด + กู้คืน state
  stateStore.load();

  sw2.begin();
  sw3.begin();
  relay1.begin();  relay1.setState(stateStore.relayState(1));
  relay2.begin();  relay2.setState(stateStore.relayState(2));
  relay3.begin();  relay3.setState(stateStore.relayState(3));

  char bootMsg[24];
  snprintf(bootMsg, sizeof(bootMsg), "Boot #%lu", stateStore.bootCount());
  oled.showMessage("Restored State", bootMsg,
    relay1.getState() || relay2.getState() || relay3.getState()
      ? "Some relays ON" : "All relays OFF");
  delay(1500);

  // DS18B20
  oled.showMessage("DS18B20", "Initializing...", "GPIO14");
  ds18.begin();

  // WiFi reset check
  bool doReset = checkWifiResetHold(5);
  wifiMgr.begin(doReset);

  String ip = wifiMgr.localIP().toString();
  oled.showIP(ip.c_str());
  delay(2000);

  // XY-MD03 (reinit Serial0 → 9600)
  Serial.println("[XYMD] Switching Serial0 to 9600...");
  Serial.flush();
  oled.showMessage("XY-MD03", "SlaveID:2", "Serial0 9600");
  bool xymdOk = xymd.begin(9600);
  if (!xymdOk) {
    oled.showMessage("XY-MD03", "Scanning ID...", "");
    uint8_t foundID = xymd.scanSlaveID(10);
    if (foundID > 0) {
      char buf[24];
      snprintf(buf, sizeof(buf), "Found ID:%d", foundID);
      oled.showMessage("XY-MD03", buf, "Set ID=2 on sensor");
      xymd.reconnect();
    } else {
      oled.showMessage("XY-MD03", "Not found", "Sim mode");
    }
  } else {
    oled.showMessage("XY-MD03", "Found! ID:2", "");
  }
  delay(1000);

  // Weather
  oled.showMessage("Weather", "Fetching...", OWM_CITY_NAME);
  weather.update();

  // MQTT — relay เปลี่ยนจาก broker
  mqtt.setOnRelayChange([]() { _onRelayChanged(0); });
  oled.showMessage("MQTT", "Connecting...", MQTT_HOST);
  mqtt.begin();

  // Web Server — relay เปลี่ยนจาก dashboard
  webServer.setOnRelayChange([]() { _onRelayChanged(0); });
  webServer.begin();

  // แสดงหน้าจอ MONITOR
  _drawCurrentState();
  Serial.println("Ready — SW1=Menu  SW2=Down  SW3=Up  Hold SW1=Back");
}

// ── Loop ──────────────────────────────────────────────────────
void loop() {
  sw1.update();
  sw2.update();
  sw3.update();

  // ── State Machine ──────────────────────────────────────────
  DevStateMachine::Result r = sm.update();

  switch (r.event) {
    case DevStateMachine::Event::RELAY_CHANGED:
      _onRelayChanged(r.relayNum);
      break;

    case DevStateMachine::Event::WIFI_RESET:
      oled.showMessage("WiFi Reset", "Clearing...", "Restarting...");
      delay(1000);
      WiFi.disconnect(true, true);   // ล้าง credentials
      delay(500);
      ESP.restart();
      break;

    case DevStateMachine::Event::OLED_SPEED_TOGGLE:
      // เปลี่ยน PAGE_INTERVAL ตาม isOledFast()
      // ใช้ค่าจาก sm โดยตรงใน tick() ผ่าน getter
      _drawCurrentState();  // redraw settings เพื่อแสดงค่าใหม่
      break;

    case DevStateMachine::Event::REDRAW:
      _drawCurrentState();
      break;

    case DevStateMachine::Event::NONE:
    default:
      break;
  }

  // ── Sensors update ─────────────────────────────────────────
  if (ds18.update() && sm.getState() == AppState::MONITOR) {
    _drawCurrentState();
  }
  if (xymd.update() && sm.getState() == AppState::MONITOR) {
    _drawCurrentState();
  }
  if (weather.isDue()) {
    weather.update();
    if (sm.getState() == AppState::MONITOR) _drawCurrentState();
  }

  // ── OLED auto-cycle (เฉพาะ MONITOR state) ─────────────────
  if (sm.getState() == AppState::MONITOR) {
    oled.tick(sm.isOledFast() ? 2000UL : 5000UL);
  }

  // ── Network ────────────────────────────────────────────────
  webServer.setMqttConnected(mqtt.isConnected());
  mqtt.loop();
  webServer.loop();
}
