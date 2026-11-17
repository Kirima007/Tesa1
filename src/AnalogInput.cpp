#include "AnalogInput.h"

namespace Analog {

bool begin(uint8_t pin) {
    if (pin < 1 || pin > 20) {
        Serial.printf("[ADC] GPIO%u is not an ADC pin\n", pin);
        return false;
    }
    if (pin > 10) {
        Serial.printf("[ADC] GPIO%u is ADC2: unreliable while WiFi is on, use GPIO1-10\n", pin);
    }
    pinMode(pin, INPUT);
    analogReadResolution(12);                   // 0–4095
    analogSetPinAttenuation(pin, ADC_11db);     // ช่วงประมาณ 0–3.1V
    return true;
}

uint32_t readMv(uint8_t pin, int32_t offsetMv, uint8_t samples) {
    if (samples == 0) samples = 1;
    uint32_t sum = 0;
    for (uint8_t i = 0; i < samples; i++) {
        sum += analogReadMilliVolts(pin);
        delay(5);
    }
    int32_t mv = (int32_t)(sum / samples) + offsetMv;
    return mv < 0 ? 0 : (uint32_t)mv;
}

uint16_t readRaw(uint8_t pin) {
    return analogRead(pin);
}

} // namespace Analog
