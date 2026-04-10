#include "main.h"

#include "driver_max30205.h"
#include "max30205_platform.h"
#include "telemetry.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdbool.h>
#include <stdio.h>

static const char *TAG = "MAX30205";
static const float TEMP_SENSOR_MIN_C = 0.0f;
static const float TEMP_SENSOR_MAX_C = 50.0f;
#define TREND_WINDOW_SAMPLES 10
#define LONG_TREND_WINDOW_SAMPLES 30
static const float CONTACT_ENTER_TEMP_C = 30.0f;
static const float CONTACT_EXIT_TEMP_C = 28.0f;
static const float CONTACT_RISING_TEMP_C = 28.0f;
static const float STABLE_SLOPE_THRESHOLD_C_PER_S = 0.05f;
static const float STABLE_ABS_SLOPE_THRESHOLD_C_PER_S = 0.02f;
static const float STABLE_WINDOW_RANGE_C = 0.20f;
static const float LONG_STABLE_ABS_SLOPE_THRESHOLD_C_PER_S = 0.01f;
static const float LONG_STABLE_WINDOW_RANGE_C = 0.35f;
static const float TEMP_HIGH_C = 37.8f;
static const float TEMP_LOW_C = 35.0f;
static const TickType_t READ_PERIOD = pdMS_TO_TICKS(5000);
static const TickType_t BODY_CONTACT_STABILIZE_PERIOD = pdMS_TO_TICKS(60000);
static const TickType_t ALERT_COOLDOWN_PERIOD = pdMS_TO_TICKS(60000);
static const uint8_t REQUIRED_STABLE_WINDOWS = 10;

static max30205_handle_t g_max30205;

static bool is_plausible_sensor_temperature(float temperature_c);
static bool is_body_temperature(float temperature_c);
static float compute_temperature_slope_c_per_s(const float *samples,
                                               size_t count,
                                               TickType_t sample_period_ticks);
static float compute_temperature_range_c(const float *samples, size_t count);
static bool infer_contact(float temperature_c, float slope_c_per_s, bool was_in_contact);

static bool max30205_scan_and_log(void)
{
    bool found = false;

    for (uint8_t addr = 0x08; addr < 0x78; ++addr) {
        if (max30205_i2c_probe(addr)) {
            ESP_LOGI(TAG, "I2C device responded at 0x%02X", addr);
            found = true;
        }
    }

    return found;
}

static bool max30205_driver_setup(void)
{
    uint8_t conf = 0;
    uint8_t status = 0;

    DRIVER_MAX30205_LINK_INIT(&g_max30205, max30205_handle_t);
    DRIVER_MAX30205_LINK_IIC_INIT(&g_max30205, max30205_i2c_init);
    DRIVER_MAX30205_LINK_IIC_DEINIT(&g_max30205, max30205_i2c_deinit);
    DRIVER_MAX30205_LINK_IIC_READ(&g_max30205, max30205_i2c_read);
    DRIVER_MAX30205_LINK_IIC_WRITE(&g_max30205, max30205_i2c_write);
    DRIVER_MAX30205_LINK_DELAY_MS(&g_max30205, max30205_delay_ms);
    DRIVER_MAX30205_LINK_DEBUG_PRINT(&g_max30205, max30205_debug_print);

    status = max30205_set_addr_7bit(&g_max30205, MAX30205_DEFAULT_ADDRESS);
    if (status != 0) {
        ESP_LOGE(TAG, "Failed to set MAX30205 address: %u", status);
        return false;
    }

    status = max30205_init(&g_max30205);
    if (status != 0) {
        ESP_LOGE(TAG, "MAX30205 init failed: %u", status);
        return false;
    }

    status = max30205_get_reg(&g_max30205, 0x01, &conf, 1);
    if (status != 0) {
        ESP_LOGE(TAG,
                 "MAX30205 config register probe failed at 0x%02X: %u",
                 MAX30205_DEFAULT_ADDRESS,
                 status);
        return false;
    }

    ESP_LOGI(TAG,
             "MAX30205 detected at 0x%02X, config=0x%02X",
             MAX30205_DEFAULT_ADDRESS,
             conf);

    status = max30205_set_data_format(&g_max30205, MAX30205_DATA_FORMAT_NORMAL);
    if (status != 0) {
        ESP_LOGE(TAG, "Failed to set normal format: %u", status);
        return false;
    }

    status = max30205_set_bus_timeout(&g_max30205, MAX30205_BUS_TIMEOUT_ENABLE);
    if (status != 0) {
        ESP_LOGE(TAG, "Failed to enable bus timeout: %u", status);
        return false;
    }

    status = max30205_start_continuous_read(&g_max30205);
    if (status != 0) {
        ESP_LOGE(TAG, "Failed to start continuous conversion: %u", status);
        return false;
    }

    vTaskDelay(pdMS_TO_TICKS(60));

    status = max30205_get_reg(&g_max30205, 0x01, &conf, 1);
    if (status != 0) {
        ESP_LOGE(TAG, "Failed to read config after startup: %u", status);
        return false;
    }

    g_max30205.reg = conf;
    ESP_LOGI(TAG, "MAX30205 running config=0x%02X", conf);

    return true;
}

static bool read_temperature(float *temperature_c)
{
    int16_t raw = 0;
    float sensor_c = 0.0f;
    float extended_c = 0.0f;
    uint8_t status = max30205_continuous_read(&g_max30205, &raw, &sensor_c);

    if (status != 0) {
        ESP_LOGE(TAG, "MAX30205 read failed: %u", status);
        return false;
    }

    extended_c = sensor_c + 64.0f;

    if (!is_plausible_sensor_temperature(sensor_c) &&
        is_plausible_sensor_temperature(extended_c)) {
        g_max30205.reg |= (1 << 5);
        *temperature_c = extended_c;
        ESP_LOGW(TAG,
                 "MAX30205 raw=0x%04X looked extended-format, using %.3f C",
                 (uint16_t)raw,
                 extended_c);
        return true;
    }

    *temperature_c = sensor_c;
    ESP_LOGD(TAG, "raw=0x%04X temp=%.3f C", (uint16_t)raw, sensor_c);
    return true;
}

static bool is_plausible_sensor_temperature(float temperature_c)
{
    return temperature_c >= TEMP_SENSOR_MIN_C &&
           temperature_c <= TEMP_SENSOR_MAX_C;
}

static bool is_body_temperature(float temperature_c)
{
    return temperature_c >= 25.0f && temperature_c <= 45.0f;
}

static float compute_temperature_slope_c_per_s(const float *samples,
                                               size_t count,
                                               TickType_t sample_period_ticks)
{
    if (count < 2 || sample_period_ticks == 0) {
        return 0.0f;
    }

    float elapsed_s =
        ((float)((count - 1) * sample_period_ticks * portTICK_PERIOD_MS)) / 1000.0f;
    if (elapsed_s <= 0.0f) {
        return 0.0f;
    }

    return (samples[count - 1] - samples[0]) / elapsed_s;
}

static float compute_temperature_range_c(const float *samples, size_t count)
{
    if (count == 0) {
        return 0.0f;
    }

    float min_temp = samples[0];
    float max_temp = samples[0];

    for (size_t i = 1; i < count; ++i) {
        if (samples[i] < min_temp) {
            min_temp = samples[i];
        }
        if (samples[i] > max_temp) {
            max_temp = samples[i];
        }
    }

    return max_temp - min_temp;
}

static bool infer_contact(float temperature_c, float slope_c_per_s, bool was_in_contact)
{
    if (was_in_contact) {
        return temperature_c >= CONTACT_EXIT_TEMP_C;
    }

    if (temperature_c >= CONTACT_ENTER_TEMP_C) {
        return true;
    }

    return temperature_c >= CONTACT_RISING_TEMP_C &&
           slope_c_per_s >= STABLE_SLOPE_THRESHOLD_C_PER_S;
}

void task_max30205_monitor(void *pvParameters)
{
    (void)pvParameters;

    char buf[80];
    char telemetry_buf[MSG_LEN];
    float temp_history[TREND_WINDOW_SAMPLES] = {0};
    float long_temp_history[LONG_TREND_WINDOW_SAMPLES] = {0};
    size_t sample_count = 0;
    size_t long_sample_count = 0;
    TickType_t body_contact_started_at = 0;
    TickType_t last_alert_tick = 0;
    bool contact_detected = false;
    uint8_t stable_window_count = 0;

    if (!max30205_driver_setup()) {
        ESP_LOGW(TAG, "MAX30205 init failed, scanning I2C bus on GPIO21/GPIO22");
        max30205_scan_and_log();
    }

    while (true) {
        float temperature_c = 0.0f;
        uint32_t timestamp_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;

        if (!g_max30205.inited) {
            if (!max30205_driver_setup()) {
                ESP_LOGW(TAG, "Retrying MAX30205 init");
                vTaskDelay(pdMS_TO_TICKS(READ_PERIOD));
                continue;
            }
        }

        if (!read_temperature(&temperature_c)) {
            g_max30205.inited = 0;
            vTaskDelay(pdMS_TO_TICKS(READ_PERIOD));
            continue;
        }

        if (!is_plausible_sensor_temperature(temperature_c)) {
            sample_count = 0;
            long_sample_count = 0;
            contact_detected = false;
            body_contact_started_at = 0;
            stable_window_count = 0;
            ESP_LOGW(TAG,
                     "[%lu ms] Physically invalid temperature from MAX30205: %.2f C",
                     (unsigned long)timestamp_ms,
                     temperature_c);
            vTaskDelay(READ_PERIOD);
            continue;
        }

        ESP_LOGI(TAG,
                 "[%lu ms] Body temperature: %.3f C",
                 (unsigned long)timestamp_ms,
                 temperature_c);

        snprintf(telemetry_buf, sizeof(telemetry_buf), "%.3f", temperature_c);
        send_telemetry(BODY_TEMPERATURE, telemetry_buf);

        if (sample_count < TREND_WINDOW_SAMPLES) {
            temp_history[sample_count++] = temperature_c;
        } else {
            for (size_t i = 1; i < TREND_WINDOW_SAMPLES; ++i) {
                temp_history[i - 1] = temp_history[i];
            }
            temp_history[TREND_WINDOW_SAMPLES - 1] = temperature_c;
        }

        if (long_sample_count < LONG_TREND_WINDOW_SAMPLES) {
            long_temp_history[long_sample_count++] = temperature_c;
        } else {
            for (size_t i = 1; i < LONG_TREND_WINDOW_SAMPLES; ++i) {
                long_temp_history[i - 1] = long_temp_history[i];
            }
            long_temp_history[LONG_TREND_WINDOW_SAMPLES - 1] = temperature_c;
        }

        float slope_c_per_s = compute_temperature_slope_c_per_s(
            temp_history, sample_count, READ_PERIOD);
        float long_slope_c_per_s = compute_temperature_slope_c_per_s(
            long_temp_history, long_sample_count, READ_PERIOD);
        float range_c = compute_temperature_range_c(temp_history, sample_count);
        float long_range_c =
            compute_temperature_range_c(long_temp_history, long_sample_count);
        contact_detected =
            infer_contact(temperature_c, slope_c_per_s, contact_detected);

        if (!contact_detected) {
            body_contact_started_at = 0;
            stable_window_count = 0;
            ESP_LOGI(TAG,
                     "[%lu ms] No body contact inferred yet (temp %.2f C, slope %.3f C/s)",
                     (unsigned long)timestamp_ms,
                     temperature_c,
                     slope_c_per_s);
        } else {
            TickType_t now = xTaskGetTickCount();

            if (body_contact_started_at == 0) {
                body_contact_started_at = now;
                stable_window_count = 0;
                ESP_LOGI(TAG,
                         "[%lu ms] Body contact inferred, waiting %lu ms to stabilize",
                         (unsigned long)timestamp_ms,
                         (unsigned long)(BODY_CONTACT_STABILIZE_PERIOD * portTICK_PERIOD_MS));
            }

            bool time_ready =
                (now - body_contact_started_at) >= BODY_CONTACT_STABILIZE_PERIOD;
            bool window_stable =
                sample_count == TREND_WINDOW_SAMPLES &&
                slope_c_per_s <= STABLE_SLOPE_THRESHOLD_C_PER_S &&
                slope_c_per_s >= -STABLE_ABS_SLOPE_THRESHOLD_C_PER_S &&
                range_c <= STABLE_WINDOW_RANGE_C;
            bool long_window_stable =
                long_sample_count == LONG_TREND_WINDOW_SAMPLES &&
                long_slope_c_per_s <= LONG_STABLE_ABS_SLOPE_THRESHOLD_C_PER_S &&
                long_slope_c_per_s >= -LONG_STABLE_ABS_SLOPE_THRESHOLD_C_PER_S &&
                long_range_c <= LONG_STABLE_WINDOW_RANGE_C;

            if (window_stable && long_window_stable) {
                if (stable_window_count < REQUIRED_STABLE_WINDOWS) {
                    ++stable_window_count;
                }
            } else {
                stable_window_count = 0;
            }

            if (!time_ready || stable_window_count < REQUIRED_STABLE_WINDOWS) {
                ESP_LOGI(TAG,
                         "[%lu ms] Temperature stabilizing before alerts (short slope %.3f C/s, short range %.3f C, long slope %.3f C/s, long range %.3f C, stable windows %u/%u)",
                         (unsigned long)timestamp_ms,
                         slope_c_per_s,
                         range_c,
                         long_slope_c_per_s,
                         long_range_c,
                         stable_window_count,
                         REQUIRED_STABLE_WINDOWS);
                vTaskDelay(READ_PERIOD);
                continue;
            }

            if ((now - last_alert_tick) < ALERT_COOLDOWN_PERIOD) {
                vTaskDelay(READ_PERIOD);
                continue;
            }

            if (!is_body_temperature(temperature_c)) {
                ESP_LOGI(TAG,
                         "[%lu ms] Contact detected but temperature is outside body range",
                         (unsigned long)timestamp_ms);
            } else if (temperature_c >= TEMP_HIGH_C) {
                int len = snprintf(buf,
                                   sizeof(buf),
                                   "[MAX30205] HIGH TEMP: %.2f C (threshold %.1f C)",
                                   temperature_c,
                                   TEMP_HIGH_C);
                send_sms(WARNING, buf, len);
                last_alert_tick = now;
            } else if (temperature_c <= TEMP_LOW_C) {
                int len = snprintf(buf,
                                   sizeof(buf),
                                   "[MAX30205] LOW TEMP: %.2f C (threshold %.1f C)",
                                   temperature_c,
                                   TEMP_LOW_C);
                send_sms(ALARM, buf, len);
                last_alert_tick = now;
            }
        }

        vTaskDelay(READ_PERIOD);
    }
}
