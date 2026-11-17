#pragma once
#include <Arduino.h>

// วงจร: VCC ── R คงที่ ── (ขา ADC) ── NTC ── GND
struct NtcConfig {
    float rFixed = 10000.0f;   // ตัวต้านทานคงที่ (Ω)
    float r0     = 10000.0f;   // ค่า NTC ที่ t0 (เช่น NTC 10k)
    float beta   = 3950.0f;    // ค่า B จาก datasheet (มักเป็น 3950 หรือ 3435)
    float t0C    = 25.0f;      // อุณหภูมิอ้างอิงของ r0
    float vccMv  = 3300.0f;    // แรงดันที่ป้อนวงจรแบ่งแรงดัน
};

namespace Ntc {
    float resistance(float mv, const NtcConfig& c = NtcConfig());  // Ω, NAN ถ้าวงจรขาด/ช็อต
    float toKelvin(float mv, const NtcConfig& c = NtcConfig());    // K,  NAN ถ้าผิดปกติ
    float toCelsius(float mv, const NtcConfig& c = NtcConfig());   // °C, NAN ถ้าผิดปกติ
}
