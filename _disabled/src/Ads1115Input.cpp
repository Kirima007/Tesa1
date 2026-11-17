#include "Ads1115Input.h"
#include <Wire.h>
#include <Adafruit_ADS1X15.h>

namespace {
    Adafruit_ADS1115 g_ads;
    bool g_ready = false;
}

namespace Ads {

bool begin(uint8_t sda, uint8_t scl, uint8_t addr) {
    Wire.begin(sda, scl);
    Wire.setClock(400000);
    g_ready = g_ads.begin(addr, &Wire);
    if (!g_ready) {
        Serial.printf("[ADS] not found at 0x%02X (SDA=%u SCL=%u)\n", addr, sda, scl);
        return false;
    }
    g_ads.setGain(GAIN_ONE);                  // ±4.096V
    g_ads.setDataRate(RATE_ADS1115_128SPS);   // ช้าแต่ noise ต่ำ (~8 ms/ครั้ง)
    Serial.printf("[ADS] ready at 0x%02X\n", addr);
    return true;
}

bool ready() { return g_ready; }

int16_t readRaw(uint8_t ch) {
    if (!g_ready || ch > 3) return 0;
    return g_ads.readADC_SingleEnded(ch);
}

float readMv(uint8_t ch, float offsetMv, uint8_t samples) {
    if (!g_ready || ch > 3) return NAN;
    if (samples == 0) samples = 1;
    int32_t sum = 0;
    for (uint8_t i = 0; i < samples; i++) {
        sum += g_ads.readADC_SingleEnded(ch);
    }
    float mv = g_ads.computeVolts((int16_t)(sum / samples)) * 1000.0f;
    return mv + offsetMv;
}

} // namespace Ads
