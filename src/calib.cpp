// ===== โหมดปรับเทียบ: หา gain และ offset ของเครื่องวัด C =====
// ใช้ env "calib" เท่านั้น (pio run -e calib -t upload)
// ขั้นตอน: เสียบ C อ้างอิงทีละตัว → กดแล้วปล่อยปุ่ม START → รอวัด → เปลี่ยนตัวถัดไป
// ครบทุกตัวแล้วคำนวณ C_จริง = gain × C_raw + offset ด้วย least squares

#include <Arduino.h>
#include <math.h>
#include "cap_meter.h"

#define PIN_START    2      // ปุ่ม pull-up ภายนอก: กด = LOW
#define CAP_SAMPLES  256    // ต้องเท่ากับใน main.cpp (ให้เงื่อนไขเหมือนตอนใช้งานจริง)
#define CAL_READS    50     // จำนวนครั้งที่อ่านต่อ 1 ตัว (50 × 256 = 12,800 รอบชาร์จ ≈ 40 s)

const float REF_PF[] = { 5.570f, 10.725f, 15.915f, 20.825f };   // ค่าจริงจาก LCR (ตัว 5/10/15/20 pF)
const int   N_REF    = sizeof(REF_PF) / sizeof(REF_PF[0]);

float g_rawMean[N_REF];
float g_rawSd[N_REF];
int   g_step = 0;

void prompt() {
    if (g_step < N_REF)
        Serial.printf("\n>> [%d/%d] เสียบ C = %.1f pF แล้วกดปล่อยปุ่ม (หรือพิมพ์ m) | b = ย้อนกลับ 1 ตัว\n",
                      g_step + 1, N_REF, REF_PF[g_step]);
}

// อ่าน CAL_READS ครั้ง หาค่าเฉลี่ยและ SD ของ C_raw (Welford: ไม่ต้องเก็บ array)
bool measurePoint(float& mean, float& sd) {
    uint32_t t0 = millis();
    double m = 0, m2 = 0;
    int n = 0, fail = 0;
    for (int i = 0; i < CAL_READS; i++) {
        CapReading r = Cap::read(CAP_SAMPLES);
        if (isnan(r.pF)) { fail++; continue; }
        n++;
        double d = r.pF - m;
        m  += d / n;
        m2 += d * (r.pF - m);
        if ((i + 1) % 5 == 0) Serial.printf("  %d/%d  เฉลี่ยตอนนี้ %.3f pF\n", i + 1, CAL_READS, m);
    }
    if (n < CAL_READS / 2) {
        Serial.printf("  ล้มเหลว: อ่านได้ %d/%d ครั้ง ตรวจการเสียบ C\n", n, CAL_READS);
        return false;
    }
    mean = m;
    sd   = n > 1 ? sqrt(m2 / (n - 1)) : 0;
    Serial.printf("  เสร็จ: C_raw = %.3f pF  SD = %.3f pF  (%.2f%%)  อ่านได้ %d/%d  ใช้เวลา %.1f s\n",
                  mean, sd, 100.0f * sd / mean, n, CAL_READS, (millis() - t0) / 1000.0f);
    return true;
}

void computeFit() {
    // least squares: y = gain·x + offset  (x = C_raw, y = C อ้างอิง)
    double sx = 0, sy = 0;
    for (int i = 0; i < N_REF; i++) { sx += g_rawMean[i]; sy += REF_PF[i]; }
    double mx = sx / N_REF, my = sy / N_REF, sxx = 0, sxy = 0, syy = 0;
    for (int i = 0; i < N_REF; i++) {
        double dx = g_rawMean[i] - mx, dy = REF_PF[i] - my;
        sxx += dx * dx; sxy += dx * dy; syy += dy * dy;
    }
    double gain   = sxy / sxx;
    double offset = my - gain * mx;
    double r2     = (sxy * sxy) / (sxx * syy);

    Serial.println("\n================ ผลการปรับเทียบ ================");
    Serial.println("  C_ref(pF)   C_raw(pF)   SD(pF)   C_cal(pF)   Error(%)");
    for (int i = 0; i < N_REF; i++) {
        double cal = gain * g_rawMean[i] + offset;
        Serial.printf("  %8.2f   %9.3f   %6.3f   %9.3f   %+7.2f\n",
                      REF_PF[i], g_rawMean[i], g_rawSd[i], cal,
                      100.0 * (cal - REF_PF[i]) / REF_PF[i]);
    }
    Serial.printf("\n  gain   = %.5f\n  offset = %.4f pF\n  R^2    = %.6f\n", gain, offset, r2);
    Serial.println("\n  คัดลอกไปใส่ใน setup() ของ main.cpp:");
    Serial.printf("    cfg.calGain     = %.5ff;\n", gain);
    Serial.printf("    cfg.calOffsetPf = %.4ff;\n", offset);
    Serial.println("================================================");
    Serial.println("กดปุ่มหรือพิมพ์ m เพื่อเริ่มปรับเทียบใหม่");
}

void onStart() {
    if (g_step >= N_REF) { g_step = 0; prompt(); return; }   // เริ่มรอบใหม่
    Serial.printf("  กำลังวัด %.1f pF ... อย่าแตะวงจร\n", REF_PF[g_step]);
    if (!measurePoint(g_rawMean[g_step], g_rawSd[g_step])) { prompt(); return; }
    g_step++;
    if (g_step >= N_REF) computeFit();
    else prompt();
}

void setup() {
    Serial.begin(9600);
    pinMode(PIN_START, INPUT);

    CapConfig cfg;               // ต้องตั้ง r1/vcc/vref เหมือน main.cpp
    cfg.calGain     = 1.0f;      // ปรับเทียบต้องใช้ค่าดิบ ห้ามใส่ gain/offset เดิม
    cfg.calOffsetPf = 0.0f;
    Cap::begin(cfg);

    delay(1000);
    Serial.printf("\n=== โหมดปรับเทียบ C | %d จุด | อ่าน %d × %d รอบต่อจุด ===\n",
                  N_REF, CAL_READS, CAP_SAMPLES);
    prompt();
}

void loop() {
    static bool     pressed = false;
    static uint32_t downAt  = 0;
    bool active = digitalRead(PIN_START) == LOW;
    if (active && !pressed) { pressed = true; downAt = millis(); }
    if (!active && pressed) {
        pressed = false;
        if (millis() - downAt > 30) onStart();
    }

    if (Serial.available()) {
        char c = Serial.read();
        if (c == 'm') onStart();
        if (c == 'b' && g_step > 0 && g_step < N_REF) { g_step--; prompt(); }
    }
}
