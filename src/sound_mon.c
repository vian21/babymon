#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/i2s.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "main.h"

#define SOUND_I2S_PORT I2S_NUM_0
#define SOUND_I2S_BCLK_GPIO 26
#define SOUND_I2S_WS_GPIO 25
#define SOUND_I2S_DATA_GPIO 33

#define SOUND_SAMPLE_RATE_HZ 16000
#define SOUND_WINDOW_SAMPLES 1024
#define SOUND_DMA_BUFFER_COUNT 4
#define SOUND_DMA_BUFFER_LENGTH 256
#define SOUND_WINDOW_TIMEOUT_MS 200

#define SOUND_ABSOLUTE_RMS_THRESHOLD 120000.0f
#define SOUND_BASELINE_MULTIPLIER 3.5f
#define SOUND_CONSECUTIVE_WINDOWS 3
#define SOUND_SMS_COOLDOWN_MS (2 * 60 * 1000)

static const char* TAG = "sound_mon";
static bool s_i2s_ready;

static esp_err_t sound_mon_init(void) {
    if (s_i2s_ready) {
        return ESP_OK;
    }

    i2s_config_t config = {
        .mode = I2S_MODE_MASTER | I2S_MODE_RX,
        .sample_rate = SOUND_SAMPLE_RATE_HZ,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_I2S,
        .intr_alloc_flags = 0,
        .dma_buf_count = SOUND_DMA_BUFFER_COUNT,
        .dma_buf_len = SOUND_DMA_BUFFER_LENGTH,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0,
    };

    i2s_pin_config_t pins = {
        .bck_io_num = SOUND_I2S_BCLK_GPIO,
        .ws_io_num = SOUND_I2S_WS_GPIO,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = SOUND_I2S_DATA_GPIO,
    };

    ESP_RETURN_ON_ERROR(i2s_driver_install(SOUND_I2S_PORT, &config, 0, NULL),
                        TAG,
                        "Failed to install I2S driver");
    ESP_RETURN_ON_ERROR(i2s_set_pin(SOUND_I2S_PORT, &pins),
                        TAG,
                        "Failed to configure I2S pins");
    ESP_RETURN_ON_ERROR(i2s_zero_dma_buffer(SOUND_I2S_PORT),
                        TAG,
                        "Failed to clear I2S DMA buffer");

    s_i2s_ready = true;
    return ESP_OK;
}

static esp_err_t sound_mon_read_rms(float* rms_out) {
    int32_t raw_samples[SOUND_WINDOW_SAMPLES];
    size_t bytes_read = 0;
    double energy = 0.0;

    if (!rms_out) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_i2s_ready) {
        ESP_RETURN_ON_ERROR(sound_mon_init(), TAG, "I2S init failed");
    }

    esp_err_t err = i2s_read(SOUND_I2S_PORT,
                             raw_samples,
                             sizeof(raw_samples),
                             &bytes_read,
                             pdMS_TO_TICKS(SOUND_WINDOW_TIMEOUT_MS));
    if (err != ESP_OK) {
        return err;
    }

    size_t sample_count = bytes_read / sizeof(raw_samples[0]);
    if (sample_count == 0) {
        return ESP_ERR_TIMEOUT;
    }

    for (size_t i = 0; i < sample_count; ++i) {
        int32_t sample = raw_samples[i] >> 8;
        energy += (double)sample * (double)sample;
    }

    *rms_out = (float)sqrt(energy / (double)sample_count);
    return ESP_OK;
}

void sound_mon_task(void* arguments) {
    TickType_t last_sms_tick = 0;
    float baseline_rms = 0.0f;
    int loud_windows = 0;

    (void)arguments;

    for (;;) {
        float rms = 0.0f;
        char message[160];
        esp_err_t err = sound_mon_read_rms(&rms);

        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Sound monitor read failed: %s", esp_err_to_name(err));
            if ((xTaskGetTickCount() - last_sms_tick) >=
                pdMS_TO_TICKS(SOUND_SMS_COOLDOWN_MS)) {
                snprintf(message,
                         sizeof(message),
                         "Sound monitor sensor failure: %s",
                         esp_err_to_name(err));
                send_sms(ALARM, message, strlen(message));
                last_sms_tick = xTaskGetTickCount();
            }
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }

        if (baseline_rms == 0.0f) {
            baseline_rms = rms;
        }

        float dynamic_threshold = baseline_rms * SOUND_BASELINE_MULTIPLIER;
        float threshold = dynamic_threshold > SOUND_ABSOLUTE_RMS_THRESHOLD
                              ? dynamic_threshold
                              : SOUND_ABSOLUTE_RMS_THRESHOLD;

        if (rms > threshold) {
            ++loud_windows;
        } else {
            loud_windows = 0;
            baseline_rms = (baseline_rms * 0.98f) + (rms * 0.02f);
        }

        ESP_LOGI(TAG,
                 "Sound RMS=%.2f baseline=%.2f threshold=%.2f loud_windows=%d",
                 rms,
                 baseline_rms,
                 threshold,
                 loud_windows);

        if (loud_windows >= SOUND_CONSECUTIVE_WINDOWS &&
            (xTaskGetTickCount() - last_sms_tick) >=
                pdMS_TO_TICKS(SOUND_SMS_COOLDOWN_MS)) {
            snprintf(message,
                     sizeof(message),
                     "Sustained loud sound detected (RMS %.0f), possible baby crying",
                     rms);
            send_sms(WARNING, message, strlen(message));
            last_sms_tick = xTaskGetTickCount();
            loud_windows = 0;
            baseline_rms = rms;
        }
    }
}
