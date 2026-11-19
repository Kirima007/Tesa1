// ===== ทดสอบ MQTT ง่ายๆ — TGR-17 =====
// อัปโหลด: pio run -e hello -t upload   แล้วเปิด monitor (9600)
// WiFi / broker ตั้งใน include/secrets.h (ไม่ขึ้น git)
//
// ส่ง {"team":"TGR-17","msg":"hello","n":<ลำดับ>} ไป topic TGR-17/hello ทุก 5 วินาที
// กดปุ่ม START (GPIO2) = ส่งทันที 1 ครั้ง
// ข้อความที่ส่งเข้า TGR-17/cmd (JSON) จะพิมพ์ออก Serial
//
// ดูฝั่ง PC:  mosquitto_sub -h <broker> -t "TGR-17/#" -v
#include <Arduino.h>
#include "MqttManager.h"

#define TEAM_ID       "TGR-17"
#define PIN_START     2          // ปุ่ม START: pull-up ภายนอก, กด = LOW
#define SEND_MS       5000       // ส่งอัตโนมัติทุกกี่ ms

uint32_t g_n = 0;
uint32_t g_lastTx = 0;                   // เวลาที่ส่งล่าสุด (นับ 5 s ใหม่ทุกครั้งที่ส่ง)

void sendHello(const char* why) {
    g_lastTx = millis();
    JsonDocument doc;
    doc["team"] = TEAM_ID;
    doc["msg"]  = "hello";
    doc["n"]    = ++g_n;
    doc["ms"]   = millis();
    bool ok = Mqtt::publish("hello", doc);
    Serial.printf("[%s] ส่ง hello #%lu → %s : %s\n", why, (unsigned long)g_n,
                  Mqtt::topic("hello").c_str(), ok ? "OK" : "ไม่สำเร็จ (ยังไม่ต่อ MQTT)");
}

void setup() {
    Serial.begin(9600);
    pinMode(PIN_START, INPUT);
    Serial.println();
    Serial.println("===== " TEAM_ID " hello MQTT =====");

    MqttConfig cfg;
    cfg.baseTopic = TEAM_ID;                 // topic = TGR-17/...
    Mqtt::begin(cfg, [](const char* topic, JsonDocument& doc) {
        Serial.printf("[รับ] %s : ", topic);
        serializeJson(doc, Serial);
        Serial.println();
    });
    Mqtt::subscribe("TGR-17/cmd");
}

void loop() {
    Mqtt::loop();

    static bool wasUp = false;
    bool up = Mqtt::connected();
    if (up && !wasUp) sendHello("เชื่อมต่อแล้ว");           // ส่งทันทีที่ต่อติด
    wasUp = up;

    if (up && millis() - g_lastTx >= SEND_MS) sendHello("auto");

    // ปุ่ม: กดแล้วปล่อย = ส่ง 1 ครั้ง (กันเด้งแบบง่าย)
    static bool     prev = HIGH;
    static uint32_t tEdge = 0;
    bool cur = digitalRead(PIN_START);
    if (cur != prev && millis() - tEdge > 50) {
        tEdge = millis();
        prev  = cur;
        if (cur == LOW) sendHello("ปุ่ม");
    }
}
