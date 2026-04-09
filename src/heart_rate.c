/**
 * max30102.cpp
 * GY-MAX30102 – Pulse Oximeter & Heart-Rate Sensor
 *
 * Key registers:
 *   0x00  INT_STATUS1     0x01  INT_STATUS2
 *   0x02  INT_ENABLE1     0x03  INT_ENABLE2
 *   0x04  FIFO_WR_PTR     0x05  OVF_COUNTER  0x06 FIFO_RD_PTR
 *   0x07  FIFO_DATA       0x08  FIFO_CONFIG
 *   0x09  MODE_CONFIG     0x0A  SPO2_CONFIG  0x0C LED1_PA  0x0D LED2_PA
 *   0x11  MULTI_LED_CTRL1 0xFF  REV_ID       0xFE PART_ID (0x15)
 *
 * SpO2 algorithm:
 *   Simplified ratio-of-ratios:  ratio = (AC_red/DC_red) / (AC_ir/DC_ir)
 *   SpO2 ≈ 110 - 25 × ratio  (empirical, calibrate per device)
 *
 * Wiring (shared I2C bus with MAX30205):
 *   SDA → GPIO 21   SCL → GPIO 22
 *   INT → GPIO 34   (optional interrupt, active-low)
 */

#include "main.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <math.h>
#include <stdio.h>
#include "driver/i2c.h"
#include "esp_log.h"

#define SAMPLE_COUNT 100 // samples per calculation window

static const char* TAG = "MAX30102";
static const uint8_t I2C_ADDR = 0x57;
static const uint8_t I2C_PORT = I2C_NUM_0; // shared bus

// Register addresses
static const uint8_t REG_FIFO_WR = 0x04;
static const uint8_t REG_FIFO_OVF = 0x05;
static const uint8_t REG_FIFO_RD = 0x06;
static const uint8_t REG_FIFO_DATA = 0x07;
static const uint8_t REG_FIFO_CFG = 0x08;
static const uint8_t REG_MODE_CFG = 0x09;
static const uint8_t REG_SPO2_CFG = 0x0A;
static const uint8_t REG_LED1_PA = 0x0C;
static const uint8_t REG_LED2_PA = 0x0D;

// Sensor config
static const uint32_t IR_FINGER_THRESHOLD = 50000UL;
static const TickType_t READ_PERIOD = pdMS_TO_TICKS(10); // ~100 Hz polling

// Alerts
static const float HR_HIGH = 120.0f;
static const float HR_LOW = 40.0f;
static const float SPO2_LOW = 94.0f;

// ── Low-level I2C helpers ─────────────────────────────────────
static esp_err_t write_reg(uint8_t reg, uint8_t val) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write_byte(cmd, val, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(cmd);
    return ret;
}

// Read one 3-byte FIFO entry for each channel (red + IR = 6 bytes)
static esp_err_t read_fifo(uint32_t* red, uint32_t* ir) {
    uint8_t buf[6] = {0};
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, REG_FIFO_DATA, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (I2C_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read(cmd, buf, 6, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(cmd);
    if (ret == ESP_OK) {
        *red = ((uint32_t)buf[0] << 16 | buf[1] << 8 | buf[2]) & 0x3FFFF;
        *ir = ((uint32_t)buf[3] << 16 | buf[4] << 8 | buf[5]) & 0x3FFFF;
    }
    return ret;
}

// ── Simple AC/DC extractor for SpO2 ratio ────────────────────
typedef struct {
    float dc;
    float ac_rms;
} AcDc_t;

static AcDc_t calc_ac_dc(uint32_t* samples, int n) {
    float sum = 0;
    for (int i = 0; i < n; i++)
        sum += samples[i];
    float dc = sum / n;
    float rms = 0;
    for (int i = 0; i < n; i++) {
        float d = samples[i] - dc;
        rms += d * d;
    }

    AcDc_t result;
    result.dc = dc;
    result.ac_rms = sqrtf(rms / n);
    return result;
}

// ── Simple peak-counting heart rate ──────────────────────────
static float calc_heart_rate(uint32_t* ir_buf, int n, float sample_rate_hz) {
    // Count peaks (IR signal crosses mean upward)
    float sum = 0;
    for (int i = 0; i < n; i++)
        sum += ir_buf[i];
    float mean = sum / n;
    int peaks = 0;
    for (int i = 1; i < n - 1; i++) {
        if (ir_buf[i] > mean && ir_buf[i] > ir_buf[i - 1] &&
            ir_buf[i] > ir_buf[i + 1]) {
            peaks++;
        }
    }
    float duration_s = n / sample_rate_hz;
    return (peaks / duration_s) * 60.0f;
}

static bool collect_samples(uint32_t* red_buf, uint32_t* ir_buf, int n) {
    for (int i = 0; i < n; i++) {
        uint32_t red = 0;
        uint32_t ir = 0;

        if (read_fifo(&red, &ir) != ESP_OK) {
            return false;
        }

        red_buf[i] = red;
        ir_buf[i] = ir;
        vTaskDelay(READ_PERIOD);
    }

    return true;
}

static bool read_heart_rate(float* heart_rate_bpm, bool* finger_detected) {
    uint32_t red_buf[SAMPLE_COUNT] = {0};
    uint32_t ir_buf[SAMPLE_COUNT] = {0};

    *heart_rate_bpm = 0.0f;
    *finger_detected = false;

    if (!collect_samples(red_buf, ir_buf, SAMPLE_COUNT)) {
        return false;
    }

    *finger_detected = (ir_buf[SAMPLE_COUNT / 2] > IR_FINGER_THRESHOLD);
    if (!*finger_detected) {
        return true;
    }

    float sample_rate = 1000.0f / (READ_PERIOD * portTICK_PERIOD_MS);
    *heart_rate_bpm = calc_heart_rate(ir_buf, SAMPLE_COUNT, sample_rate);
    return true;
}

static bool read_spo2(float* spo2_percent, bool* finger_detected) {
    uint32_t red_buf[SAMPLE_COUNT] = {0};
    uint32_t ir_buf[SAMPLE_COUNT] = {0};

    *spo2_percent = 0.0f;
    *finger_detected = false;

    if (!collect_samples(red_buf, ir_buf, SAMPLE_COUNT)) {
        return false;
    }

    *finger_detected = (ir_buf[SAMPLE_COUNT / 2] > IR_FINGER_THRESHOLD);
    if (!*finger_detected) {
        return true;
    }

    AcDc_t red = calc_ac_dc(red_buf, SAMPLE_COUNT);
    AcDc_t ir = calc_ac_dc(ir_buf, SAMPLE_COUNT);
    float ratio = (red.ac_rms / red.dc) / (ir.ac_rms / ir.dc + 1e-6f);
    *spo2_percent = 110.0f - 25.0f * ratio;

    if (*spo2_percent > 100.0f)
        *spo2_percent = 100.0f;
    if (*spo2_percent < 0.0f)
        *spo2_percent = 0.0f;

    return true;
}

static bool verify_heart_rate(float heart_rate_bpm) {
    return (heart_rate_bpm >= 20.0f && heart_rate_bpm <= 220.0f);
}

static bool verify_spo2(float spo2_percent) {
    return (spo2_percent >= 70.0f && spo2_percent <= 100.0f);
}

// ── Initialisation ────────────────────────────────────────────
void MAX30102_Init(void) {
    // I2C driver already installed by MAX30205_Init()
    write_reg(REG_MODE_CFG, 0x40); // reset
    vTaskDelay(pdMS_TO_TICKS(10));
    write_reg(REG_MODE_CFG, 0x03); // SpO2 mode (red + IR)
    write_reg(REG_SPO2_CFG, 0x27); // 100 sps, 18-bit, 411 µs pulse
    write_reg(REG_LED1_PA, 0x24);  // Red LED ~7 mA
    write_reg(REG_LED2_PA, 0x24);  // IR  LED ~7 mA
    write_reg(REG_FIFO_CFG, 0x4F); // 4-sample avg, FIFO rollover on
    // Clear FIFO pointers
    write_reg(REG_FIFO_WR, 0x00);
    write_reg(REG_FIFO_OVF, 0x00);
    write_reg(REG_FIFO_RD, 0x00);
    ESP_LOGI(TAG, "MAX30102 initialised (addr=0x%02X)", I2C_ADDR);
}

// ── Monitor task ──────────────────────────────────────────────
void task_max30102_monitor(void* pvParameters) {
    (void)pvParameters;

    char buf[80];

    MAX30102_Init();

    while (true) {
        float heart_rate_bpm = 0.0f;
        float spo2_percent = 0.0f;
        bool finger_hr = false;
        bool finger_spo2 = false;
        uint32_t timestamp_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;

        bool hr_read_ok = read_heart_rate(&heart_rate_bpm, &finger_hr);
        bool spo2_read_ok = read_spo2(&spo2_percent, &finger_spo2);

        if (!hr_read_ok || !spo2_read_ok) {
            // ESP_LOGE(TAG, "Failed to read MAX30102 data.");
            goto end;
        }

        if (!finger_hr || !finger_spo2) {
            // Only log occasionally to reduce spam
            static uint32_t last_no_finger_log = 0;
            if (timestamp_ms - last_no_finger_log >
                5000) { // Log every 5 seconds
                ESP_LOGI(TAG, "[%lu ms] No finger detected.", timestamp_ms);
                last_no_finger_log = timestamp_ms;
            }
            goto end;
        }

        bool hr_valid = verify_heart_rate(heart_rate_bpm);
        bool spo2_valid = verify_spo2(spo2_percent);

        if (!hr_valid) {
            ESP_LOGW(TAG,
                     "[%lu ms] Invalid heart rate value: %.1f bpm",
                     timestamp_ms,
                     heart_rate_bpm);
        }
        if (!spo2_valid) {
            ESP_LOGW(TAG,
                     "[%lu ms] Invalid SpO2 value: %.1f%%",
                     timestamp_ms,
                     spo2_percent);
        }
        if (!hr_valid || !spo2_valid) {
            goto end;
        }

        ESP_LOGI(TAG,
                 "[%lu ms] HR: %.1f bpm | SpO2: %.1f%%",
                 timestamp_ms,
                 heart_rate_bpm,
                 spo2_percent);

        if (heart_rate_bpm > HR_HIGH) {
            int len = snprintf(buf,
                               sizeof(buf),
                               "[MAX30102] HIGH HR: %.1f bpm (threshold %.1f)",
                               heart_rate_bpm,
                               HR_HIGH);
            send_sms(WARNING, buf, len);
        }
        if (heart_rate_bpm < HR_LOW && heart_rate_bpm > 0) {
            int len = snprintf(buf,
                               sizeof(buf),
                               "[MAX30102] LOW HR: %.1f bpm (threshold %.1f)",
                               heart_rate_bpm,
                               HR_LOW);
            send_sms(ALARM, buf, len);
        }
        if (spo2_percent < SPO2_LOW) {
            int len = snprintf(buf,
                               sizeof(buf),
                               "[MAX30102] LOW SpO2: %.1f%% (threshold %.1f%%)",
                               spo2_percent,
                               SPO2_LOW);
            send_sms(ALARM, buf, len);
        }

        end:
        // Yield to other tasks
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}