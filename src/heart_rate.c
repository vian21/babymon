/**
 * MAX30102 monitor task — simplified & corrected for ESP-IDF/FreeRTOS.
 *
 * Key fixes vs. original:
 *  1. Single collect_samples() call per loop iteration; HR and SpO2 share the
 *     same buffers — no double I2C collection, no stale-data mismatch.
 *  2. Effective sample rate corrected to 25 Hz (100 Hz / averaging-4).
 *  3. SpO2 uses peak-to-peak (AC) rather than RMS to better track the
 *     pulsatile component and reduce noise.
 *  4. Simple exponential smoothing on both outputs tames per-window jitter.
 *  5. goto removed; control flow uses continue.
 *  6. FIFO averaging changed to 1 so the 100 Hz assumption holds, or you can
 *     keep averaging=4 and use EFFECTIVE_SAMPLE_RATE_HZ = 25.0f (pick one).
 */

#include "main.h"
#include "max30102.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include "driver/i2c.h"
#include "esp_log.h"

/* ── tunables ──────────────────────────────────────────────── */
#define SAMPLE_COUNT          100u   /* samples per window                  */
#define EFFECTIVE_SAMPLE_RATE 100.0f /* match MAX30102_SAMPLE_AVERAGING_1   */
                                     /* use 25.0f if AVERAGING_4 is kept    */

static const char    *TAG              = "MAX30102";
static const uint8_t  I2C_PORT         = I2C_NUM_1;
static const int      SDA_PIN          = 18;
static const int      SCL_PIN          = 21;
static const int      INT_PIN          = 4;
static const uint32_t I2C_FREQ_HZ      = 400000;

static const uint32_t IR_FINGER_THRESHOLD    = 50000UL;
static const uint32_t IR_MIN_PULSE_RANGE     = 1500;
static const TickType_t READ_PERIOD          = pdMS_TO_TICKS(10);
static const uint8_t  FINGER_LOW_CONFIRM     = 5;
static const uint8_t  FINGER_HIGH_CONFIRM    = 2;
static const uint32_t VALUE_FALLBACK_MAX_MS  = 4000;
static const uint32_t WARN_LOG_PERIOD_MS     = 2000;

/* smoothing factor: 0 = no smoothing, closer to 1 = very heavy smoothing  */
static const float    EMA_ALPHA              = 0.3f;

/* alerts */
static const float HR_HIGH   = 120.0f;
static const float HR_LOW    =  40.0f;
static const float SPO2_LOW  =  94.0f;

#define MAX30102_ADDR_7BIT   0x57
#define MAX30102_FIFO_DATA   0x07

/* ── driver state ──────────────────────────────────────────── */
static max30102_handle_t s_dev;
static bool     s_i2c_ready        = false;
static bool     s_finger_present   = false;
static uint8_t  s_finger_low_cnt   = 0;
static uint8_t  s_finger_high_cnt  = 0;

/* ── forward declaration ───────────────────────────────────── */
static uint8_t max30102_iic_read(uint8_t addr, uint8_t reg,
                                  uint8_t *buf, uint16_t len);

/* ══════════════════════════════════════════════════════════════
 * I2C platform callbacks
 * ══════════════════════════════════════════════════════════════ */
static esp_err_t i2c_probe(uint8_t addr7) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (!cmd) return ESP_ERR_NO_MEM;
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr7 << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(cmd);
    esp_err_t r = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(cmd);
    return r;
}

static uint8_t max30102_iic_init(void) {
    if (s_i2c_ready) return 0;
    i2c_config_t c = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = SDA_PIN,
        .scl_io_num       = SCL_PIN,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_FREQ_HZ,
    };
    esp_err_t r = i2c_param_config(I2C_PORT, &c);
    if (r != ESP_OK && r != ESP_ERR_INVALID_STATE) return 1;

    r = i2c_driver_install(I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);
    if (r != ESP_OK && r != ESP_ERR_INVALID_STATE) return 1;

    if (i2c_probe(MAX30102_ADDR_7BIT) != ESP_OK)
        ESP_LOGW(TAG, "MAX30102 not ACKing on 0x57 (SDA=%d SCL=%d INT=%d)",
                 SDA_PIN, SCL_PIN, INT_PIN);

    s_i2c_ready = true;
    return 0;
}

static uint8_t max30102_iic_deinit(void) {
    s_i2c_ready = false;
    return 0;
}

static uint8_t max30102_iic_write(uint8_t addr, uint8_t reg,
                                   uint8_t *buf, uint16_t len) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (!cmd) return 1;
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, addr, true);
    i2c_master_write_byte(cmd, reg, true);
    if (len) i2c_master_write(cmd, buf, len, true);
    i2c_master_stop(cmd);
    esp_err_t r = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(cmd);
    return (r == ESP_OK) ? 0 : 1;
}

static uint8_t max30102_iic_read(uint8_t addr, uint8_t reg,
                                  uint8_t *buf, uint16_t len) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (!cmd) return 1;
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, addr, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, addr | 0x01, true);
    if (len) i2c_master_read(cmd, buf, len, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);
    esp_err_t r = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(cmd);
    return (r == ESP_OK) ? 0 : 1;
}

static void max30102_delay_ms(uint32_t ms)              { vTaskDelay(pdMS_TO_TICKS(ms)); }
static void max30102_debug_print(const char *fmt, ...)  { va_list a; va_start(a,fmt); vprintf(fmt,a); va_end(a); }
static void max30102_receive_cb(uint8_t type)           { (void)type; }

/* ══════════════════════════════════════════════════════════════
 * Finger detection
 * ══════════════════════════════════════════════════════════════ */
static bool update_finger(uint32_t ir) {
    if (ir > IR_FINGER_THRESHOLD) {
        s_finger_low_cnt  = 0;
        if (s_finger_high_cnt < 255) s_finger_high_cnt++;
        if (s_finger_high_cnt >= FINGER_HIGH_CONFIRM) s_finger_present = true;
    } else {
        s_finger_high_cnt = 0;
        if (s_finger_low_cnt < 255) s_finger_low_cnt++;
        if (s_finger_low_cnt  >= FINGER_LOW_CONFIRM)  s_finger_present = false;
    }
    return s_finger_present;
}

/* ══════════════════════════════════════════════════════════════
 * FIFO helpers
 * ══════════════════════════════════════════════════════════════ */
static bool fifo_peek(uint32_t *red_out, uint32_t *ir_out) {
    uint8_t b[6] = {0};
    /* LibDriver expects the 8-bit write address */
    if (max30102_iic_read(MAX30102_ADDR_7BIT << 1,
                          MAX30102_FIFO_DATA, b, 6) != 0)
        return false;
    *red_out = ((uint32_t)b[0]<<16 | (uint32_t)b[1]<<8 | b[2]) & 0x3FFFF;
    *ir_out  = ((uint32_t)b[3]<<16 | (uint32_t)b[4]<<8 | b[5]) & 0x3FFFF;
    return true;
}

/**
 * Collect exactly `n` samples into caller-supplied buffers.
 * Returns true only when all n samples were received.
 */
static bool collect_samples(uint32_t *red, uint32_t *ir, int n) {
    int collected = 0;
    const int max_attempts = n * 8;
    for (int attempt = 0; collected < n && attempt < max_attempts; attempt++) {
        uint8_t want = (uint8_t)((n - collected) > 8 ? 8 : (n - collected));
        uint8_t got  = want;
        uint8_t ret  = max30102_read(&s_dev,
                                      &red[collected], &ir[collected], &got);
        /* ret 1/2/3/5 = fatal driver errors */
        if (ret == 1 || ret == 2 || ret == 3 || ret == 5) return false;
        if (got > 0) collected += got;
        else         vTaskDelay(READ_PERIOD);
    }
    return (collected == n);
}

/* ══════════════════════════════════════════════════════════════
 * Signal quality
 * ══════════════════════════════════════════════════════════════ */
static uint32_t signal_range(const uint32_t *s, int n) {
    uint32_t lo = s[0], hi = s[0];
    for (int i = 1; i < n; i++) {
        if (s[i] < lo) lo = s[i];
        if (s[i] > hi) hi = s[i];
    }
    return hi - lo;
}

/* ══════════════════════════════════════════════════════════════
 * Heart rate  —  peak counting on IR buffer
 * ══════════════════════════════════════════════════════════════ */
static float calc_hr(const uint32_t *ir, int n, float fs) {
    float sum = 0;
    for (int i = 0; i < n; i++) sum += ir[i];
    float mean = sum / n;

    float var = 0;
    for (int i = 0; i < n; i++) { float d = ir[i]-mean; var += d*d; }
    float threshold = mean + 0.5f * sqrtf(var / n);

    int peaks[32], npeaks = 0;
    int min_gap = (int)(fs * 0.35f);
    int last = -1000;

    for (int i = 1; i < n-1; i++) {
        if (ir[i] > threshold &&
            ir[i] > ir[i-1] && ir[i] >= ir[i+1] &&
            (i - last) >= min_gap) {
            if (npeaks < 32) peaks[npeaks++] = i;
            last = i;
        }
    }
    if (npeaks < 2) return 0.0f;

    float gap = 0;
    for (int i = 1; i < npeaks; i++) gap += peaks[i] - peaks[i-1];
    gap /= (npeaks - 1);
    return (gap > 0) ? (60.0f * fs / gap) : 0.0f;
}

/* ══════════════════════════════════════════════════════════════
 * SpO2  —  R = (ACred/DCred) / (ACir/DCir),  SpO2 ≈ 110 − 25·R
 *
 * AC = half peak-to-peak (more stable than RMS over short windows).
 * ══════════════════════════════════════════════════════════════ */
static float calc_spo2(const uint32_t *red, const uint32_t *ir, int n) {
    /* DC = mean */
    float sum_r = 0, sum_i = 0;
    for (int k = 0; k < n; k++) { sum_r += red[k]; sum_i += ir[k]; }
    float dc_r = sum_r / n, dc_i = sum_i / n;

    /* AC = (max − min) / 2  per channel */
    uint32_t min_r = red[0], max_r = red[0];
    uint32_t min_i = ir[0],  max_i = ir[0];
    for (int k = 1; k < n; k++) {
        if (red[k] < min_r) min_r = red[k];
        if (red[k] > max_r) max_r = red[k];
        if (ir[k]  < min_i) min_i = ir[k];
        if (ir[k]  > max_i) max_i = ir[k];
    }
    float ac_r = (max_r - min_r) * 0.5f;
    float ac_i = (max_i - min_i) * 0.5f;

    if (dc_r < 1.0f || dc_i < 1.0f || ac_i < 1.0f) return 0.0f;

    float R   = (ac_r / dc_r) / (ac_i / dc_i);
    float spo2 = 110.0f - 25.0f * R;

    if (spo2 > 100.0f) spo2 = 100.0f;
    if (spo2 <   0.0f) spo2 =   0.0f;
    return spo2;
}

/* ══════════════════════════════════════════════════════════════
 * Validation
 * ══════════════════════════════════════════════════════════════ */
static bool hr_valid(float v)   { return v >=  20.0f && v <= 220.0f; }
static bool spo2_valid(float v) { return v >=  70.0f && v <= 100.0f; }

/* ══════════════════════════════════════════════════════════════
 * Initialisation
 * ══════════════════════════════════════════════════════════════ */
static bool sensor_init(void) {
    DRIVER_MAX30102_LINK_INIT(&s_dev, max30102_handle_t);
    DRIVER_MAX30102_LINK_IIC_INIT(&s_dev,          max30102_iic_init);
    DRIVER_MAX30102_LINK_IIC_DEINIT(&s_dev,        max30102_iic_deinit);
    DRIVER_MAX30102_LINK_IIC_READ(&s_dev,          max30102_iic_read);
    DRIVER_MAX30102_LINK_IIC_WRITE(&s_dev,         max30102_iic_write);
    DRIVER_MAX30102_LINK_DELAY_MS(&s_dev,          max30102_delay_ms);
    DRIVER_MAX30102_LINK_DEBUG_PRINT(&s_dev,       max30102_debug_print);
    DRIVER_MAX30102_LINK_RECEIVE_CALLBACK(&s_dev,  max30102_receive_cb);

    if (max30102_init(&s_dev) != 0) {
        ESP_LOGE(TAG, "max30102_init failed"); return false;
    }

    bool ok =
        max30102_set_mode(&s_dev,                MAX30102_MODE_SPO2)           == 0 &&
        max30102_set_spo2_sample_rate(&s_dev,    MAX30102_SPO2_SAMPLE_RATE_100_HZ) == 0 &&
        max30102_set_adc_resolution(&s_dev,      MAX30102_ADC_RESOLUTION_18_BIT)   == 0 &&
        max30102_set_spo2_adc_range(&s_dev,      MAX30102_SPO2_ADC_RANGE_4096)     == 0 &&
        /* Averaging = 1 keeps effective rate at 100 Hz (matches EFFECTIVE_SAMPLE_RATE) */
        max30102_set_fifo_sample_averaging(&s_dev, MAX30102_SAMPLE_AVERAGING_1)    == 0 &&
        max30102_set_fifo_roll(&s_dev,           MAX30102_BOOL_TRUE)               == 0 &&
        max30102_set_led_red_pulse_amplitude(&s_dev, 0x24)                         == 0 &&
        max30102_set_led_ir_pulse_amplitude(&s_dev,  0x24)                         == 0;

    if (!ok) {
        ESP_LOGE(TAG, "max30102 config failed");
        max30102_deinit(&s_dev);
        return false;
    }

    ESP_LOGI(TAG, "MAX30102 ready");
    return true;
}

/* ══════════════════════════════════════════════════════════════
 * Monitor task
 * ══════════════════════════════════════════════════════════════ */
void task_max30102_monitor(void *pv) {
    (void)pv;

    while (!sensor_init()) vTaskDelay(pdMS_TO_TICKS(2000));

    /* Shared sample buffers — allocated once, reused every iteration */
    static uint32_t red_buf[SAMPLE_COUNT];
    static uint32_t ir_buf[SAMPLE_COUNT];

    /* Smoothed outputs */
    float smooth_hr   = 0.0f;
    float smooth_spo2 = 0.0f;
    bool  ema_seeded  = false;

    /* Fallback state */
    float    last_hr = 0.0f, last_spo2 = 0.0f;
    uint32_t last_valid_ts = 0;
    uint32_t last_warn_ts  = 0;
    uint32_t last_no_finger_ts = 0;

    char sms_buf[80];

    while (true) {
        uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;

        /* ── 1. Quick finger check from a single raw FIFO read ── */
        {
            uint32_t r, ir;
            if (fifo_peek(&r, &ir)) update_finger(ir);
        }
        if (!s_finger_present) {
            if (now_ms - last_no_finger_ts > 5000) {
                ESP_LOGI(TAG, "[%lu ms] No finger detected.", now_ms);
                last_no_finger_ts = now_ms;
            }
            ema_seeded = false; /* reset smoothing when finger lifted */
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        /* ── 2. Collect ONE shared window of samples ─────────── */
        if (!collect_samples(red_buf, ir_buf, SAMPLE_COUNT)) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        /* Re-check finger with the middle sample of the real window */
        if (!update_finger(ir_buf[SAMPLE_COUNT / 2])) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        /* ── 3. Signal quality gate ───────────────────────────── */
        if (signal_range(ir_buf, SAMPLE_COUNT) < IR_MIN_PULSE_RANGE) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        /* ── 4. Compute HR and SpO2 from the SAME buffer ──────── */
        float hr   = calc_hr(ir_buf, SAMPLE_COUNT, EFFECTIVE_SAMPLE_RATE);
        float spo2 = calc_spo2(red_buf, ir_buf, SAMPLE_COUNT);

        /* ── 5. Fallback to last-valid if current window is bad ── */
        bool can_fallback = (last_valid_ts > 0) &&
                            ((now_ms - last_valid_ts) <= VALUE_FALLBACK_MAX_MS);
        if (!hr_valid(hr)   && can_fallback) hr   = last_hr;
        if (!spo2_valid(spo2) && can_fallback) spo2 = last_spo2;

        if (!hr_valid(hr) || !spo2_valid(spo2)) {
            if (now_ms - last_warn_ts > WARN_LOG_PERIOD_MS) {
                if (!hr_valid(hr))
                    ESP_LOGW(TAG, "[%lu ms] Invalid HR: %.1f bpm", now_ms, hr);
                if (!spo2_valid(spo2))
                    ESP_LOGW(TAG, "[%lu ms] Invalid SpO2: %.1f%%", now_ms, spo2);
                last_warn_ts = now_ms;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        last_hr = hr; last_spo2 = spo2; last_valid_ts = now_ms;

        /* ── 6. Exponential moving average to smooth output ───── */
        if (!ema_seeded) {
            smooth_hr   = hr;
            smooth_spo2 = spo2;
            ema_seeded  = true;
        } else {
            smooth_hr   = EMA_ALPHA * hr   + (1.0f - EMA_ALPHA) * smooth_hr;
            smooth_spo2 = EMA_ALPHA * spo2 + (1.0f - EMA_ALPHA) * smooth_spo2;
        }

        ESP_LOGI(TAG, "[%lu ms] HR: %.1f bpm | SpO2: %.1f%%",
                 now_ms, smooth_hr, smooth_spo2);

        /* ── 7. Alerts ────────────────────────────────────────── */
        if (smooth_hr > HR_HIGH && sms_queue) {
            int n = snprintf(sms_buf, sizeof(sms_buf),
                "[MAX30102] HIGH HR: %.1f bpm (limit %.1f)", smooth_hr, HR_HIGH);
            send_sms(WARNING, sms_buf, n);
        }
        if (smooth_hr < HR_LOW && smooth_hr > 0 && sms_queue) {
            int n = snprintf(sms_buf, sizeof(sms_buf),
                "[MAX30102] LOW HR: %.1f bpm (limit %.1f)", smooth_hr, HR_LOW);
            send_sms(ALARM, sms_buf, n);
        }
        if (smooth_spo2 < SPO2_LOW && sms_queue) {
            int n = snprintf(sms_buf, sizeof(sms_buf),
                "[MAX30102] LOW SpO2: %.1f%% (limit %.1f%%)", smooth_spo2, SPO2_LOW);
            send_sms(ALARM, sms_buf, n);
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}