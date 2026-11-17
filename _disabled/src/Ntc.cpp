#include "Ntc.h"
#include <math.h>

namespace Ntc {

float resistance(float mv, const NtcConfig& c) {
    if (isnan(mv)) return NAN;
    if (mv <= 1.0f)            return NAN;   // ช็อตลง GND
    if (mv >= c.vccMv - 1.0f)  return NAN;   // NTC หลุด / วงจรขาด
    // V = Vcc * Rntc / (R + Rntc)  →  Rntc = R * V / (Vcc - V)
    return c.rFixed * mv / (c.vccMv - mv);
}

float toKelvin(float mv, const NtcConfig& c) {
    float r = resistance(mv, c);
    if (isnan(r)) return NAN;
    // สมการ Beta: 1/T = 1/T0 + ln(R/R0)/B  (T เป็น Kelvin)
    float invT = 1.0f / (c.t0C + 273.15f) + logf(r / c.r0) / c.beta;
    return 1.0f / invT;
}

float toCelsius(float mv, const NtcConfig& c) {
    return toKelvin(mv, c) - 273.15f;   // NAN - 273.15 ยังเป็น NAN
}

} // namespace Ntc
