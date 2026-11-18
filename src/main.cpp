// ===== เครื่องวัดค่า C ระดับ pF — TESA Top Gun Rally 2026 Track 1 (TGR-17) =====
#include <Arduino.h>
#include <esp_timer.h>
#include "cap_meter.h"
#include "AnalogInput.h"
// #include <WiFi.h>
// #include "MqttManager.h"     // MQTT ปิดไว้ก่อน

#define TEAM_ID       "TGR-17"
#define PIN_START     2          // ปุ่ม START: pull-up ภายนอก, ปกติ HIGH, กด = LOW
#define PIN_LED       48         // RGB LED บนบอร์ด (WS2812)
#define LED_MS        200        // กติกา 3.4: LED สว่าง 200 ms ทุกครั้งที่กด
#define CAP_SAMPLES   256        // จำนวนค่าเวลาที่เอามาเฉลี่ยก่อนคำนวณ C (ต้องเท่ากับ calib.cpp)
#define PRESS_MS      5          // ต้องเป็น LOW ต่อเนื่องเท่านี้ถึงนับว่ากด (กันสัญญาณรบกวน)
#define RELEASE_MS    50         // ต้องปล่อยปุ่มนานเท่านี้ถึงรับการกดครั้งถัดไป
#define RETRY_MS      2500       // วัดไม่ได้ → ลองใหม่อัตโนมัติจนถึงเวลานี้ (ยังไม่เกิน 3 s)
#define SHOW_DEBUG    0          // 1 = พิมพ์เวลาที่ใช้วัดเพิ่ม (ใช้ตอนทดสอบเท่านั้น ส่งงานต้องเป็น 0)

esp_timer_handle_t g_ledTimer;

void ledOff(void*) { rgbLedWrite(PIN_LED, 0, 0, 0); }

void ledFlash() {
    rgbLedWrite(PIN_LED, 255, 255, 255);          // สีขาว สว่างสุด เห็นชัดในวิดีโอ
    esp_timer_stop(g_ledTimer);
    esp_timer_start_once(g_ledTimer, LED_MS * 1000);   // ปิดเองหลัง 200 ms แม้กำลังวัดอยู่
}

void measure() {
    uint32_t t0 = millis();
    CapReading r;
    do {
        r = Cap::read(CAP_SAMPLES);
    } while (isnan(r.pF) && millis() - t0 < RETRY_MS);

    Serial.println("\"" TEAM_ID "\"");
    if (isnan(r.pF)) Serial.println("Capacitance = ERROR");
    else             Serial.printf("Capacitance = %.2f pF\n", r.pF);
#if SHOW_DEBUG
    Serial.printf("[debug] t = %.3f us, valid %u/%u, %lu ms\n",
                  r.timeUs, r.valid, r.samples, (unsigned long)(millis() - t0));
#endif
    Serial.println();                             // เว้น 1 บรรทัดก่อนการวัดครั้งถัดไป
}

void setup() {
    Serial.begin(9600);
    pinMode(PIN_START, INPUT);

    esp_timer_create_args_t ta = {};
    ta.callback = &ledOff;
    ta.name     = "led";
    esp_timer_create(&ta, &g_ledTimer);
    rgbLedWrite(PIN_LED, 0, 0, 0);

    CapConfig cfg;               // ค่าเริ่มต้นอยู่ใน cap_meter.h
    // cfg.r1Ohm  = 998000;      // ใส่ค่าที่วัดจริง
    cfg.calOffsetPf = -9.3134f;  // calib 18 พ.ย. (ค่าอ้างอิงจาก LCR) R^2 = 0.99998
    cfg.calGain     = 0.96213f;  // calib 18 พ.ย. (ค่าอ้างอิงจาก LCR)
    Cap::begin(cfg);

    // GPIO1 ใช้เป็น SENSE ของ LM393 แล้ว ห้ามอ่าน analog ขานี้พร้อมกัน
    // Analog::begin(1);
    // int analogR_1 = Analog::readMv(1, 17.834, 30);
}

void loop() {
    // วัดทันทีที่กด (กติกา: กด 1 ครั้งแล้วปล่อย) แล้วต้องปล่อยปุ่มก่อนถึงจะวัดครั้งใหม่ได้
    static bool     armed    = true;
    static uint32_t lowSince = 0, highSince = 0;
    uint32_t now = millis();

    if (digitalRead(PIN_START) == LOW) {
        highSince = 0;
        if (!lowSince) lowSince = now;
        if (armed && now - lowSince >= PRESS_MS) {
            armed = false;
            ledFlash();
            measure();
        }
    } else {
        lowSince = 0;
        if (!highSince) highSince = now;
        if (!armed && millis() - highSince >= RELEASE_MS) armed = true;
    }
}
