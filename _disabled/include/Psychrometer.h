#pragma once
#include <Arduino.h>

// Psychrometer: หา %RH จากอุณหภูมิกระเปาะแห้ง (dry) และกระเปาะเปียก (wet)
namespace Psychro {
    // ค่าสัมประสิทธิ์ A (ต่อ °C) ขึ้นกับลมที่พัดผ่านกระเปาะเปียก
    constexpr float A_VENTILATED = 6.62e-4f;  // มีพัดลมเป่า ≥ 3 m/s (แม่นกว่า)
    constexpr float A_STILL_AIR  = 8.0e-4f;   // ไม่มีพัดลม อากาศนิ่ง

    float satVaporKPa(float tC);   // ความดันไอน้ำอิ่มตัว (kPa)
    // คืน %RH 0–100, NAN ถ้าค่าผิดปกติ (เช่น wet สูงกว่า dry มาก = เซนเซอร์สลับ/เสีย)
    float relativeHumidity(float tDry, float tWet,
                           float A = A_VENTILATED,
                           float pressureKPa = 101.325f);
}
