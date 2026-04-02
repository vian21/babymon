/**
 * max30205.cpp
 * MAX30205MTA – Human Body Temperature Sensor
 *
 * Register map (relevant):
 *   0x00  Temperature  (16-bit, MSB first, 0.00390625 °C/LSB)
 *   0x01  Configuration
 *   0x02  THYST  (hysteresis)
 *   0x03  TOS    (over-temp shutdown)
 *
 * Wiring (ESP32 default I2C):
 *   SDA → GPIO 21
 *   SCL → GPIO 22
 *   VCC → 3.3 V
 *   GND → GND
 *   A0, A1, A2 → GND  (address 0x48)
 */

#include "main.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include "driver/i2c.h"
#include "esp_log.h"

// ── Constants ────────────────────────────────────────────────
static const char* TAG = "MAX30205";
static const uint8_t I2C_ADDR = 0x48;
static const uint8_t REG_TEMP = 0x00;
static const uint8_t REG_CONFIG = 0x01;
static const uint8_t I2C_PORT = I2C_NUM_0;
static const int SDA_PIN = 21;
static const int SCL_PIN = 22;
static const uint32_t I2C_FREQ_HZ = 400000; // 400 kHz fast-mode

// Alert thresholds
static const float TEMP_HIGH_C = 37.8f; // fever threshold
static const float TEMP_LOW_C = 35.0f;  // hypothermia threshold

// Task timing
static const TickType_t READ_PERIOD = pdMS_TO_TICKS(1000); // 1 s

// ── I2C helpers ───────────────────────────────────────────────
static esp_err_t i2c_read_reg16(uint8_t reg, uint16_t* out) {
    uint8_t buf[2] = {0};

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd); // repeated start
    i2c_master_write_byte(cmd, (I2C_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read(cmd, buf, 2, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(cmd);

    if (ret == ESP_OK) {
        *out = (buf[0] << 8) | buf[1];
    }
    return ret;
}

static float raw_to_celsius(uint16_t raw) {
    // Two's complement, 16-bit, resolution 0.00390625 °C
    int16_t signed_raw = (int16_t)raw;
    return signed_raw * 0.00390625f;
}

static bool read_temperature(float* temperature_c) {
    uint16_t raw = 0;
    if (i2c_read_reg16(REG_TEMP, &raw) != ESP_OK) {
        return false;
    }

    *temperature_c = raw_to_celsius(raw);
    return true;
}

static bool verify_temperature(float temperature_c) {
    return (temperature_c >= 25.0f && temperature_c <= 45.0f);
}

// ── Initialisation ────────────────────────────────────────────
static void MAX30205_Init(void) {
    // Configure I2C master (shared bus – init only once across sensors)
    i2c_config_t conf = {};
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = SDA_PIN;
    conf.scl_io_num = SCL_PIN;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = I2C_FREQ_HZ;

    i2c_param_config(I2C_PORT, &conf);
    i2c_driver_install(I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);

    // Set continuous conversion mode (config register = 0x00)
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, REG_CONFIG, true);
    i2c_master_write_byte(cmd, 0x00, true); // continuous, comparator mode
    i2c_master_stop(cmd);
    i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(cmd);
    ESP_LOGI(TAG, "MAX30205 initialised (addr=0x%02X)", I2C_ADDR);
}

// ── Monitor task ──────────────────────────────────────────────
void task_max30205_monitor(void* pvParameters) {
    (void)pvParameters;

    char buf[80];

    MAX30205_Init();

    while (true) {
        float temperature_c = 0.0f;
        uint32_t timestamp_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;

        if (!read_temperature(&temperature_c)) {
            ESP_LOGE(TAG, "I2C read failed.");
            vTaskDelay(READ_PERIOD);
            continue;
        }

        if (!verify_temperature(temperature_c)) {
            ESP_LOGW(TAG,
                     "[%lu ms] Invalid temperature value: %.2f C",
                     timestamp_ms,
                     temperature_c);
            vTaskDelay(READ_PERIOD);
            continue;
        }

        ESP_LOGI(
            TAG, "[%lu ms] Temperature: %.3f C", timestamp_ms, temperature_c);

        if (temperature_c >= TEMP_HIGH_C) {
            int len =
                snprintf(buf,
                         sizeof(buf),
                         "[MAX30205] HIGH TEMP: %.2f C (threshold %.1f C)",
                         temperature_c,
                         TEMP_HIGH_C);

            send_sms(WARNING, buf, len);

        } else if (temperature_c <= TEMP_LOW_C) {
            int len = snprintf(buf,
                               sizeof(buf),
                               "[MAX30205] LOW TEMP: %.2f C (threshold %.1f C)",
                               temperature_c,
                               TEMP_LOW_C);
            send_sms(ALARM, buf, len);
        }

        vTaskDelay(READ_PERIOD);
    }
}