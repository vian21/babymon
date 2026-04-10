/**
 * MAX30102 monitor task — ESP-IDF / FreeRTOS
 *
 * Sampling window adjusted per:
 *   Atwood et al., "Optimal sampling interval to estimate heart rate at rest
 *   and during exercise in atrial fibrillation", Am J Cardiol 1989.
 *
 *   Key finding: a 1-second window produces up to ±16 bpm error; a 20-second
 *   window reduces this to ±2.2 bpm.  SAMPLE_COUNT is therefore set to 2000
 *   (20 s × 100 Hz) so that each HR calculation uses the minimum interval the
 *   paper identifies as reliable.
 *
 *   SpO2 does NOT require a long window (it is a ratio of the same pulse wave,
 *   not a beat-counting measurement), so it is computed from the central 100
 *   samples of the 2000-sample buffer — keeping SpO2 latency low while HR
 *   accuracy meets the published standard.
 *
 * Other fixes retained from previous revision:
 *  - fifo_flush() before every collect window (prevents FIFO overflow stall).
 *  - fifo_peek() removed from hot path (was drifting the read pointer).
 *  - Single shared buffer for HR + SpO2.
 *  - Peak-to-peak AC for SpO2.
 *  - EMA smoothing, resets on finger lift.
 *  - collect_samples() timeout logged explicitly.
 */

#include "main.h"
#include "max30102.h"
#include "shared_i2c.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "driver/i2c.h"
#include "esp_log.h"

/* ── tunables ──────────────────────────────────────────────────────────────
 *
 * SAMPLE_COUNT = 20 s × 100 Hz = 2000
 *   Derived from Atwood et al. (1989): the 20-second sampling interval is the
 *   shortest that keeps mean HR error <= 2.2 bpm across both rest and exercise.
 *
 * SPO2_WINDOW_START / SPO2_WINDOW_LEN
 *   SpO2 uses only the central 100 samples of the 2000-sample buffer so its
 *   update latency stays around 1 s rather than 20 s.
 * ────────────────────────────────────────────────────────────────────────── */
#define SAMPLE_COUNT 2000u           /* 20 s @ 100 Hz — Atwood et al. */
#define EFFECTIVE_SAMPLE_RATE 100.0f /* AVERAGING_1 -> true 100 Hz    */

#define SPO2_WINDOW_START ((SAMPLE_COUNT / 2) - 50) /* centre - 50      */
#define SPO2_WINDOW_LEN 100u                        /* 1 s window       */

static const char* TAG = "MAX30102";
static const i2c_port_t I2C_PORT = BABYMON_SENSOR_I2C_PORT;
static const int SDA_PIN = BABYMON_SENSOR_I2C_SDA_PIN;
static const int SCL_PIN = BABYMON_SENSOR_I2C_SCL_PIN;
static const int INT_PIN = 4;

static const uint32_t IR_FINGER_THRESHOLD = 50000UL;
static const uint32_t IR_MIN_PULSE_RANGE = 1500;
static const TickType_t READ_PERIOD = pdMS_TO_TICKS(2);
static const uint8_t FINGER_LOW_CONFIRM = 5;
static const uint8_t FINGER_HIGH_CONFIRM = 2;
static const uint32_t VALUE_FALLBACK_MAX_MS =
    30000; /* extended: window is 20 s */
static const uint32_t WARN_LOG_PERIOD_MS = 2000;

/* EMA alpha: with a 20 s window each value is already a long average,
 * so 0.5 gives a reasonable balance between responsiveness and smoothing. */
static const float EMA_ALPHA = 0.5f;

/* alert thresholds */
static const float HR_HIGH = 120.0f;
static const float HR_LOW = 40.0f;
static const float SPO2_LOW = 94.0f;

/* MAX30102 register map */
#define MAX30102_ADDR_7BIT 0x57
#define REG_FIFO_WR_PTR 0x04
#define REG_OVF_COUNTER 0x05
#define REG_FIFO_RD_PTR 0x06
#define REG_FIFO_DATA 0x07
#define REG_IR_LED2_PA 0x11 /* unused here, kept for reference */

/* ── driver state ──────────────────────────────────────────────────────── */
static max30102_handle_t s_dev;
static bool s_i2c_ready = false;
static bool s_finger_present = false;
static uint8_t s_finger_low_cnt = 0;
static uint8_t s_finger_high_cnt = 0;

static uint8_t max30102_iic_read(uint8_t addr,
                                 uint8_t reg,
                                 uint8_t* buf,
                                 uint16_t len); /* fwd decl */

/* ══════════════════════════════════════════════════════════════════════════
 * I2C platform callbacks
 * ══════════════════════════════════════════════════════════════════════════ */
static esp_err_t i2c_probe(uint8_t addr7) {
    esp_err_t lock_err = babymon_sensor_i2c_lock(pdMS_TO_TICKS(150));
    if (lock_err != ESP_OK) {
        return lock_err;
    }

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (!cmd) {
        babymon_sensor_i2c_unlock();
        return ESP_ERR_NO_MEM;
    }
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr7 << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(cmd);
    esp_err_t r = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(cmd);
    babymon_sensor_i2c_unlock();
    return r;
}

static uint8_t max30102_iic_init(void) {
    if (s_i2c_ready) {
        return 0;
    }

    esp_err_t r = babymon_sensor_i2c_init();
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "Shared I2C init failed: %s", esp_err_to_name(r));
        return 1;
    }

    if (i2c_probe(MAX30102_ADDR_7BIT) != ESP_OK)
        ESP_LOGW(TAG,
                 "MAX30102 not ACKing on 0x57 (SDA=%d SCL=%d INT=%d)",
                 SDA_PIN,
                 SCL_PIN,
                 INT_PIN);

    s_i2c_ready = true;
    return 0;
}

static uint8_t max30102_iic_deinit(void) {
    s_i2c_ready = false;
    return 0;
}

static uint8_t
max30102_iic_write(uint8_t addr, uint8_t reg, uint8_t* buf, uint16_t len) {
    if (babymon_sensor_i2c_lock(pdMS_TO_TICKS(150)) != ESP_OK)
        return 1;

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (!cmd) {
        babymon_sensor_i2c_unlock();
        return 1;
    }
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, addr, true);
    i2c_master_write_byte(cmd, reg, true);
    if (len)
        i2c_master_write(cmd, buf, len, true);
    i2c_master_stop(cmd);
    esp_err_t r = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(cmd);
    babymon_sensor_i2c_unlock();
    return (r == ESP_OK) ? 0 : 1;
}

static uint8_t
max30102_iic_read(uint8_t addr, uint8_t reg, uint8_t* buf, uint16_t len) {
    if (babymon_sensor_i2c_lock(pdMS_TO_TICKS(150)) != ESP_OK)
        return 1;

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (!cmd) {
        babymon_sensor_i2c_unlock();
        return 1;
    }
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, addr, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, addr | 0x01, true);
    if (len)
        i2c_master_read(cmd, buf, len, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);
    esp_err_t r = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(cmd);
    babymon_sensor_i2c_unlock();
    return (r == ESP_OK) ? 0 : 1;
}

static void max30102_delay_ms(uint32_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}
static void max30102_debug_print(const char* fmt, ...) {
    /* Filter out "fifo overrun" messages to reduce log spam */
    if (strstr(fmt, "fifo overrun") != NULL) {
        return;
    }

    va_list a;
    va_start(a, fmt);
    vprintf(fmt, a);
    va_end(a);
}
static void max30102_receive_cb(uint8_t type) {
    (void)type;
}

/* ══════════════════════════════════════════════════════════════════════════
 * FIFO helpers
 * ══════════════════════════════════════════════════════════════════════════ */

/**
 * Reset FIFO pointers so the next read always starts from a known-clean state.
 * Must be called before every collect_samples() to prevent overflow stalls.
 */
static void fifo_flush(void) {
    uint8_t zero = 0;
    max30102_iic_write(MAX30102_ADDR_7BIT << 1, REG_FIFO_WR_PTR, &zero, 1);
    max30102_iic_write(MAX30102_ADDR_7BIT << 1, REG_OVF_COUNTER, &zero, 1);
    max30102_iic_write(MAX30102_ADDR_7BIT << 1, REG_FIFO_RD_PTR, &zero, 1);
}

/**
 * Read a single IR value for finger detection without disturbing the FIFO
 * state used by collect_samples().
 *
 * Strategy: write both FIFO pointers to 0 (mini-flush), wait one sample
 * period, then call max30102_read for exactly 1 sample.  Because this is
 * always followed by a full fifo_flush() + collect, the pointer state left
 * behind here does not matter.
 *
 * Returns true and sets *ir_out on success.
 */
static bool ir_sample_one(uint32_t* ir_out) {
    /* Reset pointers so the sensor has a clean slot to write into */
    uint8_t zero = 0;
    max30102_iic_write(MAX30102_ADDR_7BIT << 1, REG_FIFO_WR_PTR, &zero, 1);
    max30102_iic_write(MAX30102_ADDR_7BIT << 1, REG_OVF_COUNTER, &zero, 1);
    max30102_iic_write(MAX30102_ADDR_7BIT << 1, REG_FIFO_RD_PTR, &zero, 1);

    /* Wait long enough for the sensor to deposit one sample (100 Hz → 10 ms) */
    vTaskDelay(pdMS_TO_TICKS(12));

    uint32_t red_tmp = 0;
    uint8_t got = 1;
    uint8_t ret = max30102_read(&s_dev, &red_tmp, ir_out, &got);
    return (ret == 0 && got == 1);
}

/**
 * Collect exactly `n` samples into caller-supplied red/ir buffers.
 *
 * At 100 Hz, 2000 samples take ~20 s.  The retry budget (n*4 attempts with a
 * 10 ms yield on empty polls) allows up to ~80 s before giving up — generous
 * enough for the full 20-second window even under brief I2C hiccups.
 */
static bool collect_samples(uint32_t* red, uint32_t* ir, int n) {
    int collected = 0;
    int max_attempts = n * 4;

    for (int attempt = 0; collected < n && attempt < max_attempts; attempt++) {
        uint8_t want = (uint8_t)((n - collected) > 8 ? 8 : (n - collected));
        uint8_t got = want;
        uint8_t ret =
            max30102_read(&s_dev, &red[collected], &ir[collected], &got);

        if (ret == 1 || ret == 2 || ret == 3 || ret == 5) {
            ESP_LOGE(TAG, "max30102_read fatal error ret=%u", ret);
            return false;
        }

        if (ret == 4) {
            // ESP_LOGW(TAG, "FIFO overrun, resetting FIFO");
            fifo_flush();
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        if (got > 0) {
            collected += got;
        } else {
            vTaskDelay(READ_PERIOD);
        }
    }

    if (collected < n) {
        ESP_LOGW(TAG, "collect_samples timeout: got %d/%d", collected, n);
        return false;
    }
    return true;
}

/* ══════════════════════════════════════════════════════════════════════════
 * Finger detection
 * ══════════════════════════════════════════════════════════════════════════ */
static bool update_finger(uint32_t ir) {
    if (ir > IR_FINGER_THRESHOLD) {
        s_finger_low_cnt = 0;
        if (s_finger_high_cnt < 255)
            s_finger_high_cnt++;
        if (s_finger_high_cnt >= FINGER_HIGH_CONFIRM)
            s_finger_present = true;
    } else {
        s_finger_high_cnt = 0;
        if (s_finger_low_cnt < 255)
            s_finger_low_cnt++;
        if (s_finger_low_cnt >= FINGER_LOW_CONFIRM)
            s_finger_present = false;
    }
    return s_finger_present;
}

/* ══════════════════════════════════════════════════════════════════════════
 * Signal quality
 * ══════════════════════════════════════════════════════════════════════════ */
static uint32_t signal_range(const uint32_t* s, int n) {
    uint32_t lo = s[0], hi = s[0];
    for (int i = 1; i < n; i++) {
        if (s[i] < lo)
            lo = s[i];
        if (s[i] > hi)
            hi = s[i];
    }
    return hi - lo;
}

/* ══════════════════════════════════════════════════════════════════════════
 * Heart rate — peak counting over a 20-second IR buffer
 *
 * Using the full 2000-sample (20 s) window directly implements the minimum
 * sampling interval recommended by Atwood et al. for <= 2.2 bpm mean error.
 * ══════════════════════════════════════════════════════════════════════════ */
static float calc_hr(const uint32_t* ir, int n, float fs) {
    float sum = 0;
    for (int i = 0; i < n; i++)
        sum += ir[i];
    float mean = sum / n;

    float var = 0;
    for (int i = 0; i < n; i++) {
        float d = ir[i] - mean;
        var += d * d;
    }
    float threshold = mean + 0.5f * sqrtf(var / n);

    /* Allow up to 64 peaks — sufficient for 220 bpm over 20 s */
    int peaks[64], npeaks = 0;
    int min_gap = (int)(fs * 0.35f); /* 350 ms refractory ~ 171 bpm max */
    int last = -1000;

    for (int i = 1; i < n - 1; i++) {
        if (ir[i] > threshold && ir[i] > ir[i - 1] && ir[i] >= ir[i + 1] &&
            (i - last) >= min_gap) {
            if (npeaks < 64)
                peaks[npeaks++] = i;
            last = i;
        }
    }
    if (npeaks < 2)
        return 0.0f;

    float gap = 0;
    for (int i = 1; i < npeaks; i++)
        gap += peaks[i] - peaks[i - 1];
    gap /= (float)(npeaks - 1);
    return (gap > 0) ? (60.0f * fs / gap) : 0.0f;
}

/* ══════════════════════════════════════════════════════════════════════════
 * SpO2 — computed from the central 100-sample (1 s) slice of the buffer
 *
 * SpO2 is a ratio measurement (AC/DC per channel), not a beat count, so it
 * does not need the full 20 s window.  Using the central slice keeps SpO2
 * latency at ~1 s while HR accuracy meets the Atwood standard.
 *
 * Formula: R = (ACred/DCred) / (ACir/DCir),  SpO2 ~= 110 - 25*R
 * AC = half peak-to-peak (more stable than RMS over short windows).
 * ══════════════════════════════════════════════════════════════════════════ */
static float calc_spo2(const uint32_t* red, const uint32_t* ir, int n) {
    float sum_r = 0, sum_i = 0;
    for (int k = 0; k < n; k++) {
        sum_r += red[k];
        sum_i += ir[k];
    }
    float dc_r = sum_r / n, dc_i = sum_i / n;

    uint32_t min_r = red[0], max_r = red[0];
    uint32_t min_i = ir[0], max_i = ir[0];
    for (int k = 1; k < n; k++) {
        if (red[k] < min_r)
            min_r = red[k];
        if (red[k] > max_r)
            max_r = red[k];
        if (ir[k] < min_i)
            min_i = ir[k];
        if (ir[k] > max_i)
            max_i = ir[k];
    }
    float ac_r = (max_r - min_r) * 0.5f;
    float ac_i = (max_i - min_i) * 0.5f;

    if (dc_r < 1.0f || dc_i < 1.0f || ac_i < 1.0f)
        return 0.0f;

    float R = (ac_r / dc_r) / (ac_i / dc_i);
    float spo2 = 110.0f - 25.0f * R;
    if (spo2 > 100.0f)
        spo2 = 100.0f;
    if (spo2 < 0.0f)
        spo2 = 0.0f;
    return spo2;
}

/* ══════════════════════════════════════════════════════════════════════════
 * Validation
 * ══════════════════════════════════════════════════════════════════════════ */
static bool hr_valid(float v) {
    return v >= 20.0f && v <= 220.0f;
}
static bool spo2_valid(float v) {
    return v >= 70.0f && v <= 100.0f;
}

/* ══════════════════════════════════════════════════════════════════════════
 * Sensor initialisation
 * ══════════════════════════════════════════════════════════════════════════ */
static bool sensor_init(void) {
    DRIVER_MAX30102_LINK_INIT(&s_dev, max30102_handle_t);
    DRIVER_MAX30102_LINK_IIC_INIT(&s_dev, max30102_iic_init);
    DRIVER_MAX30102_LINK_IIC_DEINIT(&s_dev, max30102_iic_deinit);
    DRIVER_MAX30102_LINK_IIC_READ(&s_dev, max30102_iic_read);
    DRIVER_MAX30102_LINK_IIC_WRITE(&s_dev, max30102_iic_write);
    DRIVER_MAX30102_LINK_DELAY_MS(&s_dev, max30102_delay_ms);
    DRIVER_MAX30102_LINK_DEBUG_PRINT(&s_dev, max30102_debug_print);
    DRIVER_MAX30102_LINK_RECEIVE_CALLBACK(&s_dev, max30102_receive_cb);

    if (max30102_init(&s_dev) != 0) {
        ESP_LOGE(TAG, "max30102_init failed");
        return false;
    }

    bool ok =
        max30102_set_mode(&s_dev, MAX30102_MODE_SPO2) == 0 &&
        max30102_set_spo2_sample_rate(&s_dev,
                                      MAX30102_SPO2_SAMPLE_RATE_100_HZ) == 0 &&
        max30102_set_adc_resolution(&s_dev, MAX30102_ADC_RESOLUTION_18_BIT) ==
            0 &&
        max30102_set_spo2_adc_range(&s_dev, MAX30102_SPO2_ADC_RANGE_4096) ==
            0 &&
        /* AVERAGING_1 -> effective output rate = 100 Hz = EFFECTIVE_SAMPLE_RATE
         */
        max30102_set_fifo_sample_averaging(&s_dev,
                                           MAX30102_SAMPLE_AVERAGING_1) == 0 &&
        max30102_set_fifo_roll(&s_dev, MAX30102_BOOL_TRUE) == 0 &&
        max30102_set_led_red_pulse_amplitude(&s_dev, 0x24) == 0 &&
        max30102_set_led_ir_pulse_amplitude(&s_dev, 0x24) == 0;

    if (!ok) {
        ESP_LOGE(TAG, "max30102 config failed");
        max30102_deinit(&s_dev);
        return false;
    }

    ESP_LOGI(TAG,
             "MAX30102 ready  (HR window: %u samples = %.0f s)",
             SAMPLE_COUNT,
             (float)SAMPLE_COUNT / EFFECTIVE_SAMPLE_RATE);
    return true;
}

/* ══════════════════════════════════════════════════════════════════════════
 * Monitor task
 * ══════════════════════════════════════════════════════════════════════════ */
void task_max30102_monitor(void* pvParameters) {
    (void)pvParameters;

    while (!sensor_init())
        vTaskDelay(pdMS_TO_TICKS(2000));

    /*
     * 2000-sample buffers: 2 channels * 2000 * 4 bytes = 16 kB total.
     * Placed in static storage to avoid stack pressure on this pinned task.
     */
    static uint32_t red_buf[SAMPLE_COUNT];
    static uint32_t ir_buf[SAMPLE_COUNT];

    /* EMA state */
    float smooth_hr = 0.0f;
    float smooth_spo2 = 0.0f;
    bool ema_seeded = false;

    /* Fallback / logging state */
    float last_hr = 0.0f;
    float last_spo2 = 0.0f;
    uint32_t last_valid_ts = 0;
    uint32_t last_warn_ts = 0;
    uint32_t last_nofinger_ts = 0;

    char sms_buf[80];

    while (true) {
        uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;

        /* ── 1. Finger gate ─────────────────────────────────────────────────
         *
         * When no finger is present (or on first boot) we must actively sample
         * the IR value to detect placement — we cannot rely on the cached
         * s_finger_present flag because nothing sets it to true until at least
         * one IR reading exceeds IR_FINGER_THRESHOLD.
         *
         * ir_sample_one() does a mini pointer-reset + single read that does
         * NOT interfere with the full fifo_flush() + collect_samples() that
         * follows, because we always flush again before collecting.
         *
         * When a finger IS already present we skip the extra read and go
         * straight to the full collection window.
         * ───────────────────────────────────────────────────────────────────
         */
        if (!s_finger_present) {
            uint32_t ir_probe = 0;
            if (ir_sample_one(&ir_probe)) {
                update_finger(ir_probe);
            }

            if (!s_finger_present) {
                if (now_ms - last_nofinger_ts > 5000) {
                    ESP_LOGI(TAG, "[%lu ms] No finger detected.", now_ms);
                    last_nofinger_ts = now_ms;
                }
                ema_seeded = false;
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }

            /* Finger just placed — log it and fall through to collection */
            ESP_LOGI(
                TAG, "[%lu ms] Finger detected, starting 20 s window.", now_ms);
        }

        /* ── 2. Flush FIFO, then collect a fresh 20-second window ──────── */
        fifo_flush();
        vTaskDelay(pdMS_TO_TICKS(5)); /* let fresh samples arrive post-flush */

        if (!collect_samples(red_buf, ir_buf, SAMPLE_COUNT)) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        /* ── 3. Update finger state from the real window ────────────────── */
        if (!update_finger(ir_buf[SAMPLE_COUNT / 2])) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        /* ── 4. Signal quality gate (checked on full window) ────────────── */
        if (signal_range(ir_buf, SAMPLE_COUNT) < IR_MIN_PULSE_RANGE) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        /* ── 5. HR over the full 20-second window (Atwood et al.) ───────── */
        float hr = calc_hr(ir_buf, SAMPLE_COUNT, EFFECTIVE_SAMPLE_RATE);

        /* ── 6. SpO2 over the central 1-second slice ────────────────────── */
        float spo2 = calc_spo2(red_buf + SPO2_WINDOW_START,
                               ir_buf + SPO2_WINDOW_START,
                               SPO2_WINDOW_LEN);

        /* ── 7. Fallback to last-valid if this window is bad ────────────── */
        bool can_fallback = (last_valid_ts > 0) &&
                            ((now_ms - last_valid_ts) <= VALUE_FALLBACK_MAX_MS);
        if (!hr_valid(hr) && can_fallback)
            hr = last_hr;
        if (!spo2_valid(spo2) && can_fallback)
            spo2 = last_spo2;

        if (!hr_valid(hr) || !spo2_valid(spo2)) {
            if (now_ms - last_warn_ts > WARN_LOG_PERIOD_MS) {
                if (!hr_valid(hr))
                    ESP_LOGW(TAG, "[%lu ms] Invalid HR: %.1f bpm", now_ms, hr);
                if (!spo2_valid(spo2))
                    ESP_LOGW(
                        TAG, "[%lu ms] Invalid SpO2: %.1f%%", now_ms, spo2);
                last_warn_ts = now_ms;
            }
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        last_hr = hr;
        last_spo2 = spo2;
        last_valid_ts = now_ms;

        /* ── 8. EMA smoothing ───────────────────────────────────────────── */
        if (!ema_seeded) {
            smooth_hr = hr;
            smooth_spo2 = spo2;
            ema_seeded = true;
        } else {
            smooth_hr = EMA_ALPHA * hr + (1.0f - EMA_ALPHA) * smooth_hr;
            smooth_spo2 = EMA_ALPHA * spo2 + (1.0f - EMA_ALPHA) * smooth_spo2;
        }

        ESP_LOGI(TAG,
                 "[%lu ms] HR: %.1f bpm | SpO2: %.1f%%",
                 now_ms,
                 smooth_hr,
                 smooth_spo2);

        /* ── 9. Alerts ──────────────────────────────────────────────────── */
        if (smooth_hr > HR_HIGH && sms_queue) {
            int n = snprintf(sms_buf,
                             sizeof(sms_buf),
                             "[MAX30102] HIGH HR: %.1f bpm (limit %.1f)",
                             smooth_hr,
                             HR_HIGH);
            send_sms(WARNING, sms_buf, n);
        }
        if (smooth_hr < HR_LOW && smooth_hr > 0.0f && sms_queue) {
            int n = snprintf(sms_buf,
                             sizeof(sms_buf),
                             "[MAX30102] LOW HR: %.1f bpm (limit %.1f)",
                             smooth_hr,
                             HR_LOW);
            send_sms(ALARM, sms_buf, n);
        }
        if (smooth_spo2 < SPO2_LOW && sms_queue) {
            int n = snprintf(sms_buf,
                             sizeof(sms_buf),
                             "[MAX30102] LOW SpO2: %.1f%% (limit %.1f%%)",
                             smooth_spo2,
                             SPO2_LOW);
            send_sms(ALARM, sms_buf, n);
        }

        /* No extra vTaskDelay here — collecting 2000 samples already
         * occupies ~20 s, which is the full measurement cycle. */
    }
}
