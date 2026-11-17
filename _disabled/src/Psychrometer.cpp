#include "Psychrometer.h"
#include <math.h>

namespace Psychro {

float satVaporKPa(float tC) {
    // สมการ Tetens (แม่นพอในช่วง 0–50 °C)
    return 0.6108f * expf(17.27f * tC / (tC + 237.3f));
}

float relativeHumidity(float tDry, float tWet, float A, float pressureKPa) {
    if (isnan(tDry) || isnan(tWet)) return NAN;
    if (tWet > tDry + 0.5f) return NAN;          // wet ร้อนกว่า dry ผิดหลัก
    if (tWet > tDry) tWet = tDry;                // ต่างกันนิดเดียว = อิ่มตัว

    // e = es(Tw) - A * P * (Td - Tw)
    float e  = satVaporKPa(tWet) - A * pressureKPa * (tDry - tWet);
    float rh = 100.0f * e / satVaporKPa(tDry);
    return constrain(rh, 0.0f, 100.0f);
}

} // namespace Psychro
