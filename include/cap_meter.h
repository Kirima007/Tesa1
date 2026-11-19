#pragma once
#include <Arduino.h>

// วัดค่า C ด้วยวงจร RC + LM393
// GPIO CHARGE ── R1 ── Vc (LM393 IN+) ── Cx ── GND
// Vref (LM393 IN-) = Vcc/2 จากตัวแบ่ง 10k/10k, OUT → GPIO SENSE (pull-up 10k)
// จับเวลาตั้งแต่ CHARGE = HIGH จนกว่า Vc > Vref แล้วคำนวณ C = t / (R1 * k)

struct CapConfig {
    uint8_t  pinCharge   = 4;        // ขาชาร์จ (ผ่าน R1)
    uint8_t  pinSense    = 1;        // ขารับ OUT ของ LM393
    float    r1Ohm       = 1.0e6f;   // R1 (Ω) วัดค่าจริงด้วยมิเตอร์แล้วใส่จะแม่นขึ้น
    float    vccV        = 3.30f;    // แรงดัน HIGH ของขา CHARGE
    float    vrefV       = 1.65f;    // แรงดัน Vref ที่ขา 2 ของ LM393
    float    calGain     = 1.0f;     // ปรับเทียบ: C = calGain * C_raw + calOffsetPf
    float    calOffsetPf = 0.0f;     // ชดเชย C แฝง (pF) ค่าติดลบ = ลบออก
    uint32_t dischargeUs = 3000;     // เวลาคายประจุขั้นต่ำก่อนชาร์จแต่ละรอบ
    uint32_t timeoutUs   = 5000;     // ชาร์จนานเกินนี้ = ล้มเหลว (5 ms ≈ 7,200 pF สูงสุด)
};

struct CapReading {
    float    timeUs;   // เวลาชาร์จเฉลี่ย (µs), NAN ถ้าอ่านไม่ได้
    float    pF;       // ค่า C (pF) หลังปรับเทียบ, NAN ถ้าอ่านไม่ได้
    uint16_t valid;    // จำนวนรอบที่ใช้ได้
    uint16_t samples;  // จำนวนรอบที่สั่ง
};

// รายละเอียดของ readRobust() ไว้ดู/debug
#define CAP_MAX_BLOCKS 16
struct CapRobust {
    uint8_t  blocks;                    // จำนวนชุดที่ใช้ได้
    float    blkPf[CAP_MAX_BLOCKS];     // pF แต่ละชุด เรียงน้อย → มาก
    uint16_t clipped;                   // จำนวนรอบที่ถูก MAD ตัดทิ้ง (รวมทุกชุด)
};

namespace Cap {
    void       begin(const CapConfig& cfg = CapConfig());
    CapConfig& config();                       // แก้ค่าระหว่างทำงานได้

    float      k();                            // ln(1 / (1 - Vref/Vcc)), Vref = Vcc/2 → 0.693
    float      chargeTimeOnceUs();             // ชาร์จ 1 ครั้ง (µs), -1 = ล้มเหลว
    float      timeToPf(float timeUs);         // แปลงเวลา → pF (ยังไม่ปรับเทียบ)
    CapReading read(uint16_t samples = 32);    // ชาร์จ samples ครั้งแล้วเฉลี่ย

    // ตัดค่าผิดปกติ 2 ชั้น: แบ่ง samples เป็น blocks ชุด
    //   ชั้น 1 (ในชุด): ตัดรอบที่ห่าง median เกิน madK × 1.4826 × MAD แล้วเฉลี่ย (madK 0 = ไม่ตัด)
    //   ชั้น 2 (ข้ามชุด): ทิ้งชุดสูงสุด + ต่ำสุด เฉลี่ยชุดที่เหลือ (ต้องมี ≥ 3 ชุด)
    // ชุดใช้ได้ไม่เกินครึ่ง → pF = NAN
    CapReading read2(uint16_t samples, uint8_t blocks, float madK, CapRobust* info = nullptr);
}
