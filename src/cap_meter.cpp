#include "cap_meter.h"
#include <driver/gpio.h>
#include <math.h>

namespace {
    CapConfig    g_cfg;
    portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
    float        g_lastUs = 0;   // ใช้คำนวณเวลาคายประจุให้พอ
}

namespace Cap {

void begin(const CapConfig& cfg) {
    g_cfg = cfg;
    pinMode(g_cfg.pinCharge, OUTPUT);
    digitalWrite(g_cfg.pinCharge, LOW);
    pinMode(g_cfg.pinSense, INPUT);          // มี pull-up 10k ภายนอกแล้ว
}

CapConfig& config() { return g_cfg; }

float k() {
    return logf(1.0f / (1.0f - g_cfg.vrefV / g_cfg.vccV));
}

float chargeTimeOnceUs() {
    const gpio_num_t chg = (gpio_num_t)g_cfg.pinCharge;
    const gpio_num_t sns = (gpio_num_t)g_cfg.pinSense;

    // คายประจุผ่าน R1: ต้องรอ ≥ 5RC; RC = t/k → ใช้ 8 เท่าของเวลาชาร์จล่าสุดเผื่อไว้
    gpio_set_level(chg, 0);
    uint32_t waitUs = max<uint32_t>(g_cfg.dischargeUs, (uint32_t)(g_lastUs * 8.0f));
    delayMicroseconds(waitUs);
    if (gpio_get_level(sns)) return -1;      // ยังคายไม่หมด / วงจรผิด

    const uint32_t mhz        = getCpuFrequencyMhz();
    const uint32_t timeoutCyc = g_cfg.timeoutUs * mhz;
    uint32_t dt;

    portENTER_CRITICAL(&g_mux);              // ปิด interrupt กันเวลาเพี้ยน
    const uint32_t t0 = ESP.getCycleCount();
    gpio_set_level(chg, 1);
    do {
        dt = ESP.getCycleCount() - t0;       // ลบแบบ uint32 รองรับตัวนับวนรอบ
        if (gpio_get_level(sns)) break;
    } while (dt < timeoutCyc);
    portEXIT_CRITICAL(&g_mux);

    gpio_set_level(chg, 0);                  // เริ่มคายประจุทันที
    if (dt >= timeoutCyc) { g_lastUs = g_cfg.timeoutUs; return -1; }

    g_lastUs = (float)dt / mhz;
    return g_lastUs;
}

float timeToPf(float timeUs) {
    if (isnan(timeUs) || timeUs < 0) return NAN;
    // C = t / (R1 * k)  →  µs/Ω = 1e-6 F → ×1e12 เป็น pF
    return timeUs * 1.0e6f / (g_cfg.r1Ohm * k());
}

CapReading read(uint16_t samples) {
    if (samples == 0) samples = 1;
    CapReading r { NAN, NAN, 0, samples };

    chargeTimeOnceUs();                      // รอบอุ่นเครื่อง ทิ้ง (ให้คายประจุครบ)

    double sum = 0;
    for (uint16_t i = 0; i < samples; i++) {
        float t = chargeTimeOnceUs();
        if (t < 0) continue;
        sum += t;
        r.valid++;
    }
    if (r.valid < (samples + 1) / 2) return r;   // ใช้ได้ไม่ถึงครึ่ง = ไม่น่าเชื่อ

    r.timeUs = sum / r.valid;
    r.pF     = g_cfg.calGain * timeToPf(r.timeUs) + g_cfg.calOffsetPf;
    return r;
}

} // namespace Cap
