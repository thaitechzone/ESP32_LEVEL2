#ifndef DEV_MQTT_H
#define DEV_MQTT_H

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "config.h"
#include "DevRelay.h"
#include "DevWeather.h"
#include "DevDS18B20.h"
#include "DevXYMDSensor.h"

// ── Topic map ────────────────────────────────────────────────────
//  MQTT_BASE/telemetry          pub  JSON ทุกค่า
//  MQTT_BASE/status             pub  online / offline (LWT)
//  MQTT_BASE/relay/1/state      pub  ON / OFF
//  MQTT_BASE/relay/2/state      pub
//  MQTT_BASE/relay/3/state      pub
//  MQTT_BASE/relay/1/set        sub  ON / OFF / TOGGLE
//  MQTT_BASE/relay/2/set        sub
//  MQTT_BASE/relay/3/set        sub

class DevMQTT {
private:
  WiFiClient    wifiClient;
  PubSubClient  client;

  DevRelay*       relay[3];
  DevWeather*     weather;
  DevDS18B20*     ds18;
  DevXYMDSensor*  xymd;

  void (*onRelayChange)() = nullptr;

  unsigned long lastTelemetry = 0;
  bool          connected     = false;

  // ── Topics ────────────────────────────────────────────────────
  String _top(const char* suffix) {
    return String(MQTT_BASE) + "/" + suffix;
  }
  String _relayStateTopic(int n) {   // n = 1..3
    return String(MQTT_BASE) + "/relay/" + n + "/state";
  }
  String _relaySetTopic(int n) {
    return String(MQTT_BASE) + "/relay/" + n + "/set";
  }

  // ── Build telemetry JSON ──────────────────────────────────────
  String _buildTelemetry() {
    JsonDocument doc;

    // relay
    JsonObject rel = doc["relay"].to<JsonObject>();
    for (int i = 0; i < 3; i++) {
      rel[String(i + 1)] = relay[i]->getState() ? "ON" : "OFF";
    }

    // DS18B20
    doc["ds18"]["temp"] = serialized(String(ds18->getTemp(), 2));
    doc["ds18"]["sim"]  = ds18->isSimMode();

    // XYMD
    doc["xymd"]["temp"] = serialized(String(xymd->getTemperature(), 1));
    doc["xymd"]["hum"]  = serialized(String(xymd->getHumidity(), 1));
    doc["xymd"]["sim"]  = xymd->isSimMode();

    // Weather
    const WeatherData& w = weather->getData();
    doc["weather"]["valid"]    = w.valid;
    doc["weather"]["temp"]     = serialized(String(w.temp, 1));
    doc["weather"]["hum"]      = w.humidity;
    doc["weather"]["rain"]     = w.rainChance;
    doc["weather"]["pm25"]     = serialized(String(w.pm25, 1));
    doc["weather"]["aqi"]      = w.aqi;
    doc["weather"]["aqiLabel"] = aqiLabel(w.aqi);

    // system
    doc["sys"]["ip"]     = WiFi.localIP().toString();
    doc["sys"]["rssi"]   = WiFi.RSSI();
    doc["sys"]["heap"]   = ESP.getFreeHeap();
    doc["sys"]["uptime"] = millis() / 1000UL;

    String out;
    serializeJson(doc, out);
    return out;
  }

  // ── Publish relay state ───────────────────────────────────────
  void _pubRelayState(int n) {   // n = 1..3
    String topic = _relayStateTopic(n);
    const char* val = relay[n - 1]->getState() ? "ON" : "OFF";
    client.publish(topic.c_str(), val, true);  // retain=true
  }

  // ── Subscribe to all relay/set topics ────────────────────────
  void _subscribeAll() {
    for (int i = 1; i <= 3; i++) {
      client.subscribe(_relaySetTopic(i).c_str());
    }
    Serial.printf("[MQTT] Subscribed relay/1-3/set\n");
  }

  // ── Connect / reconnect ───────────────────────────────────────
  bool _connect() {
    String lwt = _top("status");
    bool ok = client.connect(
      MQTT_CLIENT_ID,
      MQTT_USERNAME, MQTT_PASSWORD,  // authentication
      lwt.c_str(), 1, true,          // LWT: QoS1, retain
      "offline"
    );
    if (!ok) {
      Serial.printf("[MQTT] Connect failed rc=%d\n", client.state());
      return false;
    }

    // publish online
    client.publish(lwt.c_str(), "online", true);

    _subscribeAll();

    // publish current relay states immediately
    for (int i = 1; i <= 3; i++) _pubRelayState(i);

    connected = true;
    Serial.printf("[MQTT] Connected to %s:%d\n", MQTT_HOST, MQTT_PORT);
    return true;
  }

  // ── Message callback ──────────────────────────────────────────
  static void _callback(char* topic, byte* payload, unsigned int len, DevMQTT* self) {
    String t(topic);
    String msg;
    msg.reserve(len + 1);
    for (unsigned int i = 0; i < len; i++) msg += (char)payload[i];
    msg.toUpperCase();

    Serial.printf("[MQTT] << %s = %s\n", topic, msg.c_str());

    // match relay/N/set
    for (int n = 1; n <= 3; n++) {
      if (t == self->_relaySetTopic(n)) {
        if      (msg == "ON")     self->relay[n-1]->on();
        else if (msg == "OFF")    self->relay[n-1]->off();
        else if (msg == "TOGGLE") self->relay[n-1]->toggle();
        else break;

        self->_pubRelayState(n);
        if (self->onRelayChange) self->onRelayChange();

        Serial.printf("[MQTT] Relay%d → %s\n",
                      n, self->relay[n-1]->getState() ? "ON" : "OFF");
        break;
      }
    }
  }

public:
  DevMQTT(DevRelay* r1, DevRelay* r2, DevRelay* r3,
          DevWeather* wth, DevDS18B20* d18, DevXYMDSensor* xym)
    : client(wifiClient), weather(wth), ds18(d18), xymd(xym) {
    relay[0] = r1; relay[1] = r2; relay[2] = r3;
  }

  void setOnRelayChange(void (*cb)()) { onRelayChange = cb; }

  void begin() {
    client.setServer(MQTT_HOST, MQTT_PORT);
    client.setBufferSize(1024);
    // ส่ง this ผ่าน lambda capture
    client.setCallback([this](char* t, byte* p, unsigned int l) {
      _callback(t, p, l, this);
    });
    _connect();
  }

  // เรียกใน loop() ──────────────────────────────────────────────
  void loop() {
    // reconnect ถ้าหลุด
    if (!client.connected()) {
      connected = false;
      static unsigned long lastRetry = 0;
      if (millis() - lastRetry > 5000) {
        lastRetry = millis();
        _connect();
      }
      return;
    }

    client.loop();

    // telemetry publish
    if (millis() - lastTelemetry >= MQTT_TELEMETRY_INTERVAL) {
      lastTelemetry = millis();
      String payload = _buildTelemetry();
      client.publish(_top("telemetry").c_str(), payload.c_str());
      Serial.printf("[MQTT] >> telemetry (%u bytes)\n", payload.length());
    }
  }

  // เรียกเมื่อ relay เปลี่ยนจาก switch/web ─────────────────────
  void publishRelayState(int n) {   // n = 1..3
    if (client.connected()) _pubRelayState(n);
  }

  bool isConnected() { return client.connected(); }

  // Topic strings สำหรับแสดงบน dashboard / OLED
  String topicTelemetry()   { return _top("telemetry"); }
  String topicStatus()      { return _top("status"); }
  String topicRelayState(int n) { return _relayStateTopic(n); }
  String topicRelaySet(int n)   { return _relaySetTopic(n); }
};

#endif // DEV_MQTT_H
