#ifndef DEV_WEB_SERVER_H
#define DEV_WEB_SERVER_H

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "dashboard.h"
#include "config.h"
#include "DevRelay.h"
#include "DevWeather.h"
#include "DevDS18B20.h"
#include "DevXYMDSensor.h"
#include "DevNTP.h"
#include "DevRelayScheduler.h"

class DevWebServer {
private:
  AsyncWebServer  server;
  AsyncWebSocket  ws;

  DevRelay*           relay[3];
  DevWeather*         weather;
  DevDS18B20*         ds18;
  DevXYMDSensor*      xymd;
  DevNTP*             ntp       = nullptr;
  DevRelayScheduler*  scheduler = nullptr;

  unsigned long lastBroadcast = 0;
  static const unsigned long BROADCAST_INTERVAL = 2000;
  bool mqttConnected = false;

  void (*onRelayChange)() = nullptr;

  // ─── JSON payload ─────────────────────────────────────────────
  String buildJson() {
    JsonDocument doc;

    JsonArray relayArr = doc["relay"].to<JsonArray>();
    for (int i = 0; i < 3; i++) relayArr.add(relay[i]->getState());

    // NTP / time
    if (ntp) {
      doc["ntp"]["synced"] = ntp->isSynced();
      doc["ntp"]["time"]   = ntp->timeStr();
      doc["ntp"]["date"]   = ntp->dateStr();
    }

    // Schedules
    if (scheduler) {
      JsonArray sa = doc["schedules"].to<JsonArray>();
      for (int i = 0; i < DevRelayScheduler::NUM_RELAYS; i++) {
        const RelayScheduleEntry& e = scheduler->getEntry(i);
        JsonObject o = sa.add<JsonObject>();
        o["enabled"]   = e.enabled;
        o["onHour"]    = e.onHour;
        o["onMinute"]  = e.onMinute;
        o["offHour"]   = e.offHour;
        o["offMinute"] = e.offMinute;
        o["dayMask"]   = e.dayMask;
      }
    }

    // weather
    const WeatherData& w = weather->getData();
    doc["weather"]["valid"]    = w.valid;
    doc["weather"]["temp"]     = serialized(String(w.temp, 1));
    doc["weather"]["hum"]      = w.humidity;
    doc["weather"]["rain"]     = w.rainChance;
    doc["weather"]["pm25"]     = serialized(String(w.pm25, 1));
    doc["weather"]["aqi"]      = w.aqi;
    doc["weather"]["aqiLabel"] = aqiLabel(w.aqi);

    // wifi
    doc["wifi"]["ssid"] = WiFi.SSID();
    doc["wifi"]["ip"]   = WiFi.localIP().toString();
    doc["wifi"]["mac"]  = WiFi.macAddress();
    doc["wifi"]["rssi"] = WiFi.RSSI();

    // DS18B20
    doc["ds18"]["temp"] = serialized(String(ds18->getTemp(), 2));
    doc["ds18"]["sim"]  = ds18->isSimMode();

    // XYMD
    doc["xymd"]["temp"] = serialized(String(xymd->getTemperature(), 1));
    doc["xymd"]["hum"]  = serialized(String(xymd->getHumidity(), 1));
    doc["xymd"]["sim"]  = xymd->isSimMode();
    doc["xymd"]["id"]   = xymd->getSlaveID();

    // MQTT
    doc["mqtt"]["host"]         = MQTT_HOST;
    doc["mqtt"]["port"]         = MQTT_PORT;
    doc["mqtt"]["base"]         = MQTT_BASE;
    doc["mqtt"]["connected"]    = mqttConnected;
    doc["mqtt"]["t_telemetry"]  = String(MQTT_BASE) + "/telemetry";
    doc["mqtt"]["t_status"]     = String(MQTT_BASE) + "/status";
    doc["mqtt"]["t_r1_state"]   = String(MQTT_BASE) + "/relay/1/state";
    doc["mqtt"]["t_r2_state"]   = String(MQTT_BASE) + "/relay/2/state";
    doc["mqtt"]["t_r3_state"]   = String(MQTT_BASE) + "/relay/3/state";
    doc["mqtt"]["t_r1_set"]     = String(MQTT_BASE) + "/relay/1/set";
    doc["mqtt"]["t_r2_set"]     = String(MQTT_BASE) + "/relay/2/set";
    doc["mqtt"]["t_r3_set"]     = String(MQTT_BASE) + "/relay/3/set";

    // system
    doc["sys"]["heap"]   = ESP.getFreeHeap();
    doc["sys"]["uptime"] = millis() / 1000UL;

    String out;
    serializeJson(doc, out);
    return out;
  }

  // ─── WebSocket event ─────────────────────────────────────────
  void onWsEvent(AsyncWebSocket* s, AsyncWebSocketClient* client,
                 AwsEventType type, void* arg, uint8_t* data, size_t len) {
    if (type == WS_EVT_CONNECT) {
      Serial.printf("[WS] Client #%u connected from %s\n",
                    client->id(), client->remoteIP().toString().c_str());
      client->text(buildJson());
    } else if (type == WS_EVT_DISCONNECT) {
      Serial.printf("[WS] Client #%u disconnected\n", client->id());
    } else if (type == WS_EVT_DATA) {
      AwsFrameInfo* info = (AwsFrameInfo*)arg;
      if (info->final && info->index == 0 && info->len == len
          && info->opcode == WS_TEXT) {
        String msg = String((char*)data, len);
        handleWsMessage(client, msg);
      }
    }
  }

  // ─── WebSocket commands ───────────────────────────────────────
  void handleWsMessage(AsyncWebSocketClient* client, const String& msg) {
    JsonDocument doc;
    if (deserializeJson(doc, msg) != DeserializationError::Ok) return;

    String cmd = doc["cmd"].as<String>();

    if (cmd == "relay") {
      int n = doc["n"].as<int>(); // 1-3
      if (n >= 1 && n <= 3) {
        relay[n - 1]->toggle();
        Serial.printf("[WS] Relay%d toggled → %s\n",
                      n, relay[n-1]->getState() ? "ON" : "OFF");
        if (onRelayChange) onRelayChange();
        ws.textAll(buildJson());
      }

    } else if (cmd == "set_schedule" && scheduler) {
      // {"cmd":"set_schedule","n":1,"enabled":true,
      //  "onHour":6,"onMinute":0,"offHour":22,"offMinute":0,"dayMask":127}
      int n = doc["n"].as<int>() - 1; // 0-based
      if (n >= 0 && n < DevRelayScheduler::NUM_RELAYS) {
        RelayScheduleEntry e;
        e.enabled   = doc["enabled"]   | false;
        e.onHour    = doc["onHour"]    | 6;
        e.onMinute  = doc["onMinute"]  | 0;
        e.offHour   = doc["offHour"]   | 22;
        e.offMinute = doc["offMinute"] | 0;
        e.dayMask   = doc["dayMask"]   | 0x7F;
        // clamp
        e.onHour    = constrain(e.onHour,   0, 23);
        e.onMinute  = constrain(e.onMinute, 0, 59);
        e.offHour   = constrain(e.offHour,  0, 23);
        e.offMinute = constrain(e.offMinute,0, 59);
        scheduler->setEntry(n, e);
        ws.textAll(buildJson());
      }

    } else if (cmd == "relay_set" ) {
      // {"cmd":"relay_set","n":1,"state":true}
      int  n  = doc["n"].as<int>();
      bool st = doc["state"].as<bool>();
      if (n >= 1 && n <= 3) {
        relay[n-1]->setState(st);
        if (onRelayChange) onRelayChange();
        ws.textAll(buildJson());
      }
    }
  }

public:
  DevWebServer(DevRelay* r1, DevRelay* r2, DevRelay* r3,
               DevWeather* wth, DevDS18B20* d18, DevXYMDSensor* xym)
    : server(80), ws("/ws"), weather(wth), ds18(d18), xymd(xym) {
    relay[0] = r1; relay[1] = r2; relay[2] = r3;
  }

  void setOnRelayChange(void (*cb)())        { onRelayChange = cb; }
  void setMqttConnected(bool v)              { mqttConnected = v; }
  void setNTP(DevNTP* n)                     { ntp = n; }
  void setScheduler(DevRelayScheduler* s)    { scheduler = s; }

  void begin() {
    ws.onEvent([this](AsyncWebSocket* s, AsyncWebSocketClient* c,
                      AwsEventType t, void* a, uint8_t* d, size_t l) {
      onWsEvent(s, c, t, a, d, l);
    });
    server.addHandler(&ws);

    server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
      req->send(200, "text/html", DASHBOARD_HTML);
    });

    server.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest* req) {
      req->send(200, "application/json", buildJson());
    });

    server.onNotFound([](AsyncWebServerRequest* req) {
      req->send(404, "text/plain", "Not found");
    });

    server.begin();
    Serial.printf("[WebServer] Started — http://%s\n",
                  WiFi.localIP().toString().c_str());
  }

  void loop() {
    ws.cleanupClients();
    unsigned long now = millis();
    if (now - lastBroadcast >= BROADCAST_INTERVAL && ws.count() > 0) {
      lastBroadcast = now;
      ws.textAll(buildJson());
    }
  }
};

#endif // DEV_WEB_SERVER_H
