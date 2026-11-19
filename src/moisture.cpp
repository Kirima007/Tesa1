// ===== เครื่องวัดความชื้นเมล็ดกาแฟ — TESA Top Gun Rally 2026 Track 1 วันที่ 2 (TGR-17) =====
// อัปโหลด: pio run -e moisture -t upload   แล้วเปิด monitor (9600, บันทึกไฟล์ใน logs/)
//
// สูตร:  εr = (C − C_STRAY) / (C0 − C_STRAY)   →   MC% จากตาราง CAL_ER / CAL_MC
//
// โหมดเก็บค่า (COLLECT 1): เครื่องบอกทาง Serial ว่าต้องเทอะไร ครั้งที่เท่าไร
//   กล่องเปล่า ×6 → ถุง 6.9% ×6 → ถุง 11.5% ×6 → ถุง 14.4% ×6 → กล่องเปล่า ×6 (เช็ก C0 ลอย)
//   ทุกครั้ง: เทออกให้หมด → เทผ่านกรวย → ปาดผิว → ไม่เขย่า ไม่เคาะ → กดปุ่ม
//   ถ้าพลาด กด RESET บนบอร์ดแล้วเริ่มใหม่
//   ครบแล้วพิมพ์ค่า C0_PF, CAL_ER[] ให้ copy มาวางด้านล่าง
#include <Arduino.h>
#include <esp_timer.h>
#include "cap_meter.h"

#define TEAM_ID       "TGR-17"
#define PIN_START     2          // ปุ่ม START: pull-up ภายนอก, ปกติ HIGH, กด = LOW
#define PIN_LED       48         // RGB LED บนบอร์ด (WS2812)
#define LED_MS        200        // LED สว่าง 200 ms ทุกครั้งที่กด
#define CAP_SAMPLES   256        // จำนวนรอบชาร์จที่เอามาเฉลี่ยต่อ 1 ค่า (เท่ากับ main / calib)
#define PRESS_MS      5          // ต้องเป็น LOW ต่อเนื่องเท่านี้ถึงนับว่ากด
#define RELEASE_MS    50         // ต้องปล่อยปุ่มนานเท่านี้ถึงรับการกดครั้งถัดไป
#define RETRY_MS      2500       // วัดไม่ได้ → ลองใหม่จนถึงเวลานี้

#define COLLECT       0          // 1 = โหมดเก็บค่า (พิมพ์ RAW + บอกขั้นตอน) / ส่งงานต้องเป็น 0
#define COLLECT_N     6          // จำนวนครั้งต่อ 1 ตัวอย่าง (เทใหม่ทุกครั้ง)

// ---- ปรับเทียบเครื่องวัด C (ค่าเดียวกับ main.cpp) ----
#define CAL_GAIN      0.96213f
#define CAL_OFFSET    -9.3134f

// ---- εr ----
#define C0_PF         13.422f    // C กล่องเปล่า (pF) วัดด้วย env c0 19 พ.ย. (calib ตอน 08:23 ใช้ 12.781) / วันแข่งวัดใหม่แล้วแก้ค่านี้
#define C_STRAY_PF    0.0f       // C สาย (pF) ถ้าไม่รู้ใส่ 0 ได้ / ถ้าเปลี่ยนต้องคิด CAL_ER ใหม่ด้วย

// ---- จุดปรับเทียบความชื้น: εr คู่กับความชื้นของถุง (%) เรียงจากน้อยไปมาก ----
#define CAL_N         3
const float CAL_ER[CAL_N] = { 1.5060f, 2.1599f, 2.5219f };   // calib 19 พ.ย. 08:23–08:34 ด้วย C_STRAY 0 (data/moisture_cal_2026-11-19.csv)
const float CAL_MC[CAL_N] = { 6.9f, 11.5f, 14.4f };
#define FIT_MODE      1          // 0 = เส้นตรงเดียว (least squares), 1 = เส้นตรงต่อจุด (หลายท่อน) ← ใช้อันนี้

esp_timer_handle_t g_ledTimer;
float g_slope, g_intercept;      // เส้นตรง least squares จาก CAL_ER / CAL_MC
float g_c0 = C0_PF;              // C กล่องเปล่าที่ใช้คำนวณ εr (โหมดเก็บค่าจะแทนด้วยค่าที่วัดได้)

void ledOff(void*) { rgbLedWrite(PIN_LED, 0, 0, 0); }

void ledFlash() {
    rgbLedWrite(PIN_LED, 255, 255, 255);
    esp_timer_stop(g_ledTimer);
    esp_timer_start_once(g_ledTimer, LED_MS * 1000);
}

// least squares y = slope·x + intercept, คืนค่า R²
float fitLine(const float* x, const float* y, int n, float& slope, float& intercept) {
    float sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (int i = 0; i < n; i++) { sx += x[i]; sy += y[i]; sxx += x[i] * x[i]; sxy += x[i] * y[i]; }
    slope     = (n * sxy - sx * sy) / (n * sxx - sx * sx);
    intercept = (sy - slope * sx) / n;
    float ym = sy / n, ssr = 0, sst = 0;
    for (int i = 0; i < n; i++) {
        float e = y[i] - (slope * x[i] + intercept);
        ssr += e * e; sst += (y[i] - ym) * (y[i] - ym);
    }
    return 1.0f - ssr / sst;
}

// เส้นตรงต่อจุด: ใช้ท่อนที่ x ตกอยู่ ถ้าเลยปลายให้ต่อเส้นของท่อนริมออกไป
float piecewise(const float* x, const float* y, int n, float v) {
    int i = 0;
    while (i < n - 2 && v > x[i + 1]) i++;
    return y[i] + (v - x[i]) / (x[i + 1] - x[i]) * (y[i + 1] - y[i]);
}

float toEr(float c) { return (c - C_STRAY_PF) / (g_c0 - C_STRAY_PF); }

float erToMoisture(float er) {
#if FIT_MODE == 0
    return g_slope * er + g_intercept;
#else
    return piecewise(CAL_ER, CAL_MC, CAL_N, er);
#endif
}

#if COLLECT
// ===== โหมดเก็บค่า =====
#define N_STAGE (CAL_N + 2)                         // กล่องเปล่า(เริ่ม) + ถุงทั้งหมด + กล่องเปล่า(จบ)
#define LAST    (N_STAGE - 1)
float g_raw[N_STAGE][COLLECT_N];                    // ค่า C (pF) ทุกครั้ง
int   g_stage = 0, g_idx = 0;                       // ขั้นปัจจุบัน / ครั้งที่วัดแล้วในขั้นนี้

bool   isEmpty(int s)   { return s == 0 || s == LAST; }
String stageName(int s) {
    if (s == 0)    return "กล่องเปล่า(เริ่ม)";
    if (s == LAST) return "กล่องเปล่า(จบ)";
    return "ถุง " + String(CAL_MC[s - 1], 1) + "%";
}

void meanSd(const float* v, int n, float& mean, float& sd) {
    float m = 0, q = 0;                             // Welford
    for (int i = 0; i < n; i++) { float d = v[i] - m; m += d / (i + 1); q += d * (v[i] - m); }
    mean = m; sd = n > 1 ? sqrtf(q / (n - 1)) : 0;
}

void promptNext() {
    if (g_stage >= N_STAGE) return;
    if (isEmpty(g_stage))
        Serial.printf(">> ถัดไป: %s (%d/%d) — เทเมล็ดออกให้หมด แล้วกดปุ่ม\n\n",
                      stageName(g_stage).c_str(), g_idx + 1, COLLECT_N);
    else
        Serial.printf(">> ถัดไป: %s (%d/%d) — เทออก → เทผ่านกรวย → ปาดผิว → ไม่เขย่า → กดปุ่ม\n\n",
                      stageName(g_stage).c_str(), g_idx + 1, COLLECT_N);
}

void printFinal() {
    float sdC, c0a, c0b;
    meanSd(g_raw[0],    COLLECT_N, c0a, sdC);
    meanSd(g_raw[LAST], COLLECT_N, c0b, sdC);
    g_c0 = 0.5f * (c0a + c0b);                      // ใช้ค่าเฉลี่ยกล่องเปล่าตอนเริ่มกับตอนจบ

    float er[CAL_N], cm[CAL_N], sd[CAL_N];
    for (int i = 0; i < CAL_N; i++) {
        meanSd(g_raw[i + 1], COLLECT_N, cm[i], sd[i]);
        er[i] = toEr(cm[i]);
    }
    float sl, ic;
    float r2 = fitLine(er, CAL_MC, CAL_N, sl, ic);

    Serial.println("==================== สรุปผล ====================");
    Serial.printf("C0 เริ่ม %.3f pF, จบ %.3f pF → ลอย %+.3f pF\n", c0a, c0b, c0b - c0a);
    Serial.printf("ใช้ C0 = %.3f pF (เฉลี่ยเริ่ม+จบ), C_STRAY = %.2f pF\n", g_c0, C_STRAY_PF);
    if (CAL_N >= 2)                                 // C0 ลอยเท่านี้ทำให้ค่ากลางคลาดกี่ %MC
        Serial.printf("ผลของ C0 ที่ลอยต่อค่าความชื้น ≈ %+.2f %%MC\n",
                      -sl * er[CAL_N / 2] * (c0b - c0a) / g_c0);
    Serial.println("ตัวอย่าง   C เฉลี่ย (pF)   SD (pF)   εr");
    for (int i = 0; i < CAL_N; i++)
        Serial.printf("%5.1f%%    %9.3f   %7.3f   %.4f\n", CAL_MC[i], cm[i], sd[i], er[i]);
    Serial.printf("เส้นตรง (FIT_MODE 0): MC = %.4f*er %+.4f, R^2 = %.5f\n", sl, ic, r2);
    Serial.printf("SD จากการเท ≈ %.2f %%MC (ถุงกลาง)\n", sl * sd[CAL_N / 2] / g_c0);

    // จุดกลางห่างจากเส้นที่ลากผ่านจุดแรกกับจุดสุดท้ายกี่ %MC
    if (CAL_N >= 3) {
        const float ex[2] = { er[0], er[CAL_N - 1] };
        const float ey[2] = { CAL_MC[0], CAL_MC[CAL_N - 1] };
        float lin = piecewise(ex, ey, 2, er[1]);
        float dev = CAL_MC[1] - lin;
        Serial.printf("จุด %.1f%% ห่างจากเส้นตรง %.1f→%.1f%% = %+.2f %%MC  → แนะนำ FIT_MODE %d\n",
                      CAL_MC[1], CAL_MC[0], CAL_MC[CAL_N - 1], dev, fabsf(dev) <= 0.2f ? 0 : 1);
    }

    Serial.println("\n---- copy ไปวางใน moisture.cpp ----");
    Serial.printf("#define C0_PF         %.3ff\n", g_c0);
    Serial.print("const float CAL_ER[CAL_N] = { ");
    for (int i = 0; i < CAL_N; i++) Serial.printf("%.4ff%s", er[i], i < CAL_N - 1 ? ", " : " };\n");
    Serial.println("แล้วตั้ง COLLECT 0 ก่อนส่งงาน");
    Serial.println("================================================\n");
}

void collect(const CapReading& r) {
    if (g_stage >= N_STAGE) {                       // เก็บครบแล้ว: พิมพ์ RAW ต่อไว้ดูเฉยๆ
        Serial.printf("RAW,extra,-,%.3f,%.3f,%.4f,%u/%u\n\n",
                      r.timeUs, r.pF, toEr(r.pF), r.valid, r.samples);
        return;
    }
    if (isnan(r.pF)) {                              // วัดไม่ได้ ไม่นับ ให้เทแล้วกดใหม่
        Serial.println("!! วัดไม่ได้ ครั้งนี้ไม่นับ");
        promptNext();
        return;
    }
    g_raw[g_stage][g_idx++] = r.pF;
    Serial.printf("RAW,%s,%d/%d,%.3f,%.3f,%.4f,%u/%u\n",
                  stageName(g_stage).c_str(), g_idx, COLLECT_N,
                  r.timeUs, r.pF, toEr(r.pF), r.valid, r.samples);

    if (g_idx == COLLECT_N) {                       // ครบ 12 ครั้งของขั้นนี้
        float m, sd;
        meanSd(g_raw[g_stage], COLLECT_N, m, sd);
        float mn = g_raw[g_stage][0], mx = mn;
        for (int i = 1; i < COLLECT_N; i++) { mn = min(mn, g_raw[g_stage][i]); mx = max(mx, g_raw[g_stage][i]); }
        Serial.printf("== %s ครบ %d ครั้ง: C เฉลี่ย %.3f pF, SD %.3f, ต่ำสุด %.3f, สูงสุด %.3f\n",
                      stageName(g_stage).c_str(), COLLECT_N, m, sd, mn, mx);
        if (g_stage == 0) {
            g_c0 = m;                               // ใช้ C0 ที่เพิ่งวัดคำนวณ εr ของถุงต่อจากนี้
            Serial.printf("== ใช้ C0 = %.3f pF คำนวณ εr ระหว่างเก็บค่า (สรุปสุดท้ายใช้เฉลี่ยเริ่ม+จบ)\n", g_c0);
        }
        Serial.println();
        g_stage++; g_idx = 0;
        if (g_stage == N_STAGE) printFinal();
    }
    promptNext();
}
#endif

void measure() {
    uint32_t t0 = millis();
    CapReading r;
    do {
        r = Cap::read(CAP_SAMPLES);
    } while (isnan(r.pF) && millis() - t0 < RETRY_MS);

    Serial.println("\"" TEAM_ID "\"");
    if (isnan(r.pF)) Serial.println("Moisture Level = ERROR");
    else             Serial.printf("Moisture Level = %.2f %%\n", erToMoisture(toEr(r.pF)));
#if COLLECT
    collect(r);
#else
    Serial.println();
#endif
}

void setup() {
    Serial.begin(9600);
    pinMode(PIN_START, INPUT);

    esp_timer_create_args_t ta = {};
    ta.callback = &ledOff;
    ta.name     = "led";
    esp_timer_create(&ta, &g_ledTimer);
    rgbLedWrite(PIN_LED, 0, 0, 0);

    CapConfig cfg;
    cfg.calGain     = CAL_GAIN;
    cfg.calOffsetPf = CAL_OFFSET;
    Cap::begin(cfg);

    fitLine(CAL_ER, CAL_MC, CAL_N, g_slope, g_intercept);
#if COLLECT
    Serial.println();
    Serial.println("===== โหมดเก็บค่าความชื้น (COLLECT 1) =====");
    Serial.println("RAW,ตัวอย่าง,ครั้ง,t_us,C_pF,er,valid");
    promptNext();
#endif
}

void loop() {
    static bool     armed    = true;
    static uint32_t lowSince = 0, highSince = 0;
    uint32_t now = millis();

    if (digitalRead(PIN_START) == LOW) {
        highSince = 0;
        if (!lowSince) lowSince = now;
        if (armed && now - lowSince >= PRESS_MS) {
            armed = false;
            ledFlash();
            measure();
        }
    } else {
        lowSince = 0;
        if (!highSince) highSince = now;
        if (!armed && now - highSince >= RELEASE_MS) armed = true;
    }
}
