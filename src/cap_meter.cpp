#include "cap_meter.h"
#include <driver/gpio.h>
#include <math.h>
#include <algorithm>
#include <vector>

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

// median ของ a[0..n) (เรียง a ไปด้วย)
static float medianSort(float* a, int n) {
    std::sort(a, a + n);
    return (n % 2) ? a[n / 2] : (a[n / 2 - 1] + a[n / 2]) / 2;
}

CapReading read2(uint16_t samples, uint8_t blocks, float madK, CapRobust* info) {
    if (blocks < 1) blocks = 1;
    if (blocks > CAP_MAX_BLOCKS) blocks = CAP_MAX_BLOCKS;
    uint16_t n = samples / blocks;
    if (n < 1) n = 1;
    CapReading r { NAN, NAN, 0, (uint16_t)(n * blocks) };
    std::vector<float> t(n), d(n);
    float blkUs[CAP_MAX_BLOCKS];
    int   nb = 0;
    uint16_t clipped = 0;

    chargeTimeOnceUs();                      // รอบอุ่นเครื่อง ทิ้ง

    for (uint8_t b = 0; b < blocks; b++) {
        int m = 0;
        for (uint16_t i = 0; i < n; i++) {
            float x = chargeTimeOnceUs();
            if (x >= 0) t[m++] = x;
        }
        if (m < (n + 1) / 2) continue;       // ชุดนี้ใช้ได้ไม่ถึงครึ่ง ทิ้งทั้งชุด

        // ชั้น 1: MAD clipping
        float med = medianSort(t.data(), m);
        float thr = INFINITY;
        if (madK > 0) {
            for (int i = 0; i < m; i++) d[i] = fabsf(t[i] - med);
            float mad = medianSort(d.data(), m);
            if (mad > 0) thr = madK * 1.4826f * mad;   // MAD 0 (ค่าเท่ากันเกือบหมด) = ไม่ตัด
        }
        double sum = 0;
        int    k   = 0;
        for (int i = 0; i < m; i++) {
            if (fabsf(t[i] - med) > thr) continue;
            sum += t[i];
            k++;
        }
        clipped += m - k;
        r.valid += k;
        blkUs[nb++] = sum / k;               // k ≥ 1 เสมอ (median อยู่ในช่วงเสมอ)
    }

    if (info) { info->blocks = 0; info->clipped = clipped; }
    if (nb * 2 <= blocks) return r;          // ชุดใช้ได้ไม่เกินครึ่ง = ไม่น่าเชื่อ

    // ชั้น 2: trimmed mean ข้ามชุด
    std::sort(blkUs, blkUs + nb);
    int lo = 0, hi = nb;
    if (nb >= 3) { lo = 1; hi = nb - 1; }
    double s = 0;
    for (int i = lo; i < hi; i++) s += blkUs[i];
    r.timeUs = s / (hi - lo);
    r.pF     = g_cfg.calGain * timeToPf(r.timeUs) + g_cfg.calOffsetPf;

    if (info) {
        info->blocks = nb;
        for (int i = 0; i < nb; i++) info->blkPf[i] = g_cfg.calGain * timeToPf(blkUs[i]) + g_cfg.calOffsetPf;
    }
    return r;
}

} // namespace Cap
