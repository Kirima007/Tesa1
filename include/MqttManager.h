#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <functional>

#if __has_include("secrets.h")
#include "secrets.h"            // ค่าจริง (ไม่ขึ้น git)
#else
#include "secrets.example.h"    // ค่าตัวอย่าง
#endif

// ===== แก้ config ตรงนี้ที่เดียว =====
struct MqttConfig {
    const char* wifiSsid     = SECRET_WIFI_SSID;
    const char* wifiPass     = SECRET_WIFI_PASS;
    const char* host         = SECRET_MQTT_HOST;
    uint16_t    port         = 1883;
    const char* user         = SECRET_MQTT_USER;        // ว่าง = ไม่ใช้ auth
    const char* pass         = SECRET_MQTT_PASS;
    const char* baseTopic    = "test";   // topic หลัก
    uint16_t    keepAlive    = 30;        // วินาที
    uint16_t    bufferSize   = 1024;      // ขนาด JSON สูงสุด (byte)
};

// callback เวลามีข้อความเข้า: topic + JSON ที่ parse แล้ว
using MqttJsonHandler = std::function<void(const char* topic, JsonDocument& doc)>;

namespace Mqtt {
    void begin(const MqttConfig& cfg, MqttJsonHandler onMessage = nullptr);
    void loop();                                   // เรียกใน loop() ทุกรอบ
    bool connected();

    // ส่ง JSON ไป topic เต็ม
    bool publishJson(const char* topic, const JsonDocument& doc, bool retain = false);
    // ส่ง JSON ไป <baseTopic>/<subTopic>
    bool publish(const char* subTopic, const JsonDocument& doc, bool retain = false);

    bool subscribe(const char* topic);             // subscribe ซ้ำอัตโนมัติหลัง reconnect
    String topic(const char* subTopic);            // ช่วยต่อ topic
}
