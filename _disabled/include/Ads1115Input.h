#pragma once
#include <Arduino.h>

// ADS1115 (16-bit ADC ภายนอก ผ่าน I2C) อ่านได้ 4 ช่อง: 0–3 = A0–A3
// ช่วงอ่าน ±4.096V (GAIN_ONE) ความละเอียด 0.125 mV/bit เหมาะกับไฟเลี้ยง 3.3V
namespace Ads {
    bool  begin(uint8_t sda = 8, uint8_t scl = 9, uint8_t addr = 0x48);
    bool  ready();                                   // เจอชิปหรือไม่
    // ค่า mV เฉลี่ย + offsetMv (บวก/ลบได้) ถ้าอ่านไม่ได้คืน NAN
    float readMv(uint8_t ch, float offsetMv = 0, uint8_t samples = 4);
    int16_t readRaw(uint8_t ch);                     // ค่าดิบ (-32768..32767)
}
