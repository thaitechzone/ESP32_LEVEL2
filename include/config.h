#ifndef CONFIG_H
#define CONFIG_H

// ===== OpenWeatherMap =====
// สมัครฟรีที่ https://openweathermap.org/api
// แผน Free รองรับ Current Weather + Air Pollution API
#define OWM_API_KEY   "1bef650d2c6ea7a91f58252948c2d325"

// พิกัด นครศรีธรรมราช (ใช้ lat/lon แม่นยำกว่า city name)
#define OWM_LAT       "8.4322"
#define OWM_LON       "99.9631"
#define OWM_CITY_NAME "Nakhon Si Thammarat"

// อัปเดตทุกกี่วินาที (Free plan limit: 60 calls/min, แนะนำ 300s+)
#define WEATHER_UPDATE_SEC  5000

// ===== HiveMQ (Free public broker) =====
#define MQTT_HOST     "192.168.1.10"
#define MQTT_PORT     1883
// Client ID ควร unique — ใส่ mac address ท้าย 6 ตัวก็ได้
#define MQTT_CLIENT_ID  "esp32-level2"

// Base topic — เปลี่ยนให้ unique เพื่อไม่ชนกับคนอื่นบน public broker
#define MQTT_BASE       "esp32level2"

// Publish interval (ms)
#define MQTT_TELEMETRY_INTERVAL  5000

#endif // CONFIG_H
