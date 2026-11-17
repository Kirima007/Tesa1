#pragma once
#include <Arduino.h>

// อ่านค่า analog แบบง่าย ใช้ได้หลายขา
// ถ้าเปิด WiFi ให้ใช้ขา ADC1 เท่านั้น: GPIO1–10
namespace Analog {
    bool     begin(uint8_t pin);                      // ประกาศขา (false = ขาใช้ไม่ได้)
    // ค่า mV (calibrate แล้ว, เฉลี่ย) + offsetMv (บวก/ลบได้) ผลลัพธ์ไม่ต่ำกว่า 0
    uint32_t readMv(uint8_t pin, int32_t offsetMv = 0, uint8_t samples = 16);
    uint16_t readRaw(uint8_t pin);                    // ค่าดิบ 0–4095
}
