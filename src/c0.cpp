// ===== หา C0 (C กล่องเปล่า) สำหรับ moisture.cpp — TGR-17 =====
// อัปโหลด: pio run -e c0 -t upload   แล้วเปิด monitor (9600)
// วางกล่องเปล่าให้เหมือนตอนวัดจริง → กดปุ่มทีละครั้ง (5–6 ครั้งพอ)
// เครื่องพิมพ์ค่าเฉลี่ยสะสม + บรรทัด #define C0_PF ให้ copy ไปวางใน moisture.cpp
// อยากเริ่มนับใหม่ กด RESET บนบอร์ด
#include <Arduino.h>
#include <esp_timer.h>
#include "cap_meter.h"

#define PIN_START     2          // ปุ่ม START: pull-up ภายนอก, ปกติ HIGH, กด = LOW
#define PIN_LED       48         // RGB LED บนบอร์ด (WS2812)
#define LED_MS        200
#define CAP_SAMPLES   500       // ต้องเท่ากับ moisture.cpp
#define CAP_BLOCKS    5          // ต้องเท่ากับ moisture.cpp: แบ่งเป็นกี่ชุด ทิ้งสูงสุด+ต่ำสุด เฉลี่ยที่เหลือ
#define MAD_K         3.5f       // ต้องเท่ากับ moisture.cpp: ในชุด ตัดรอบที่ห่าง median เกิน MAD_K × σ / 0 = ไม่ตัด
#define PRESS_MS      5
#define RELEASE_MS    50

// ต้องเท่ากับ moisture.cpp
#define CAL_GAIN      0.96213f
#define CAL_OFFSET    -9.3134f

esp_timer_handle_t g_ledTimer;
int   g_n = 0;
float g_mean = 0, g_q = 0;       // Welford: ค่าเฉลี่ย / ผลรวมกำลังสองของส่วนต่าง
float g_min = 0, g_max = 0;

void ledOff(void*) { rgbLedWrite(PIN_LED, 0, 0, 0); }

void ledFlash() {
    rgbLedWrite(PIN_LED, 255, 255, 255);
    esp_timer_stop(g_ledTimer);
    esp_timer_start_once(g_ledTimer, LED_MS * 1000);
}

CapRobust g_rob;                 // วิธีเดียวกับ moisture.cpp: Cap::read2 (MAD ในชุด + ทิ้งชุดสูงสุด/ต่ำสุด)

void measure() {
    CapReading r = Cap::read2(CAP_SAMPLES, CAP_BLOCKS, MAD_K, &g_rob);
    if (isnan(r.pF)) {
        Serial.printf("!! วัดไม่ได้ (valid %u/%u) ครั้งนี้ไม่นับ\n\n", r.valid, r.samples);
        return;
    }
    g_n++;
    float d = r.pF - g_mean;
    g_mean += d / g_n;
    g_q    += d * (r.pF - g_mean);
    if (g_n == 1) g_min = g_max = r.pF;
    g_min = min(g_min, r.pF);
    g_max = max(g_max, r.pF);
    float sd = g_n > 1 ? sqrtf(g_q / (g_n - 1)) : 0;

    Serial.printf("ครั้งที่ %d: C = %.3f pF\n", g_n, r.pF);
    Serial.print("  ทีละชุด (เรียงแล้ว):");
    for (int i = 0; i < g_rob.blocks; i++) Serial.printf(" %.3f", g_rob.blkPf[i]);
    Serial.printf("  ห่างสุด %.3f pF  MAD ตัดทิ้ง %u/%u รอบ\n",
                  g_rob.blkPf[g_rob.blocks - 1] - g_rob.blkPf[0], g_rob.clipped, r.samples);
    Serial.printf("เฉลี่ย %d ครั้ง = %.3f pF, SD %.3f, ต่ำสุด %.3f, สูงสุด %.3f\n",
                  g_n, g_mean, sd, g_min, g_max);
    Serial.printf("#define C0_PF         %.3ff\n\n", g_mean);
}

void setup() {
    Serial.begin(9600);
    pinMode(PIN_START, INPUT);

    esp_timer_create_args_t ta = {};
    ta.callback = &ledOff;
    ta.name     = "led";
    esp_timer_create(&ta, &g_ledTimer);
    rgbLedWrite(PIN_LED, 0, 0, 0);

    CapConfig cfg;
    cfg.calGain     = CAL_GAIN;
    cfg.calOffsetPf = CAL_OFFSET;
    Cap::begin(cfg);

    Serial.println();
    Serial.println("===== หา C0: วางกล่องเปล่า แล้วกดปุ่มทีละครั้ง =====");
    Serial.println();
}

void loop() {
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
        if (!armed && now - highSince >= RELEASE_MS) armed = true;
    }
}
