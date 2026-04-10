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
#define SOUND_I2S_DATA_GPIO 34

#define SOUND_SAMPLE_RATE_HZ 16000
#define SOUND_WINDOW_FRAMES 1024
#define SOUND_FRAME_SLOT_COUNT 2
#define SOUND_SAMPLE_SHIFT_BITS 8
#define SOUND_DMA_BUFFER_COUNT 4
#define SOUND_DMA_BUFFER_LENGTH 256
#define SOUND_WINDOW_TIMEOUT_MS 200
#define SOUND_ERROR_RETRY_DELAY_MS 250
#define SOUND_LOG_PERIOD_MS 1000
#define SOUND_WIRING_HINT_PERIOD_MS 3000
#define SOUND_LOOP_DELAY_MS 100

#define SOUND_THRESHOLD_MARGIN_DB 10.0f
#define SOUND_MIN_TRIGGER_RMS 8000.0f
#define SOUND_BASELINE_WARMUP_WINDOWS 8
#define SOUND_CONSECUTIVE_LOUD_WINDOWS 4
#define SOUND_BASELINE_RISE_ALPHA 0.02f
#define SOUND_BASELINE_FALL_ALPHA 0.10f
#define SOUND_SMS_COOLDOWN_MS (2 * 60 * 1000)

static const char* TAG = "sound_mon";
static bool s_i2s_ready;
static int32_t s_sound_raw_samples[SOUND_WINDOW_FRAMES * 2];

typedef struct {
    float rms;
    size_t nonzero_samples;
} sound_window_stats_t;

typedef struct {
    TickType_t last_sms_tick;
    TickType_t last_log_tick;
    TickType_t last_wiring_hint_tick;
    float baseline_rms;
    float threshold_ratio;
    size_t warmup_windows;
    size_t consecutive_loud_windows;
} sound_mon_state_t;

typedef enum {
    SOUND_SLOT_LEFT = 0,
    SOUND_SLOT_RIGHT = 1,
} sound_slot_t;

static int32_t sound_mon_get_sample(size_t frame_index, sound_slot_t slot) {
    return s_sound_raw_samples[(frame_index * SOUND_FRAME_SLOT_COUNT) + slot] >>
           SOUND_SAMPLE_SHIFT_BITS;
}

static float sound_mon_update_baseline(float baseline_rms,
                                       float rms,
                                       float threshold) {
    if (baseline_rms <= 0.0f) {
        return rms;
    }

    if (rms < baseline_rms) {
        // Let the baseline fall quickly when the room gets quieter again.
        return (baseline_rms * (1.0f - SOUND_BASELINE_FALL_ALPHA)) +
               (rms * SOUND_BASELINE_FALL_ALPHA);
    }

    if (rms < threshold) {
        // Only learn louder background noise while we are still below "LOUD".
        return (baseline_rms * (1.0f - SOUND_BASELINE_RISE_ALPHA)) +
               (rms * SOUND_BASELINE_RISE_ALPHA);
    }

    return baseline_rms;
}

static bool sound_mon_sms_cooldown_active(const sound_mon_state_t* state,
                                          TickType_t now) {
    return state->last_sms_tick != 0 &&
           (now - state->last_sms_tick) < pdMS_TO_TICKS(SOUND_SMS_COOLDOWN_MS);
}

static esp_err_t sound_mon_init(void) {
    if (s_i2s_ready) {
        return ESP_OK;
    }

    i2s_config_t config = {
        .mode = I2S_MODE_MASTER | I2S_MODE_RX,
        .sample_rate = SOUND_SAMPLE_RATE_HZ,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = 0,
        .dma_buf_count = SOUND_DMA_BUFFER_COUNT,
        .dma_buf_len = SOUND_DMA_BUFFER_LENGTH,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0,
    };

    i2s_pin_config_t pins = {
        .mck_io_num = I2S_PIN_NO_CHANGE,
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

// Log a read failure and queue one alarm SMS if the cooldown window allows it.
// This keeps repeated sensor errors from spamming the queue every loop.
static void sound_mon_handle_read_error(sound_mon_state_t* state,
                                        TickType_t now,
                                        esp_err_t err) {
    char message[MSG_LEN];

    ESP_LOGE(TAG, "Sound monitor read failed: %s", esp_err_to_name(err));

    // Repeated read failures can happen in a tight loop, so rate-limit alarms.
    if (sound_mon_sms_cooldown_active(state, now)) {
        return;
    }

    snprintf(message,
             sizeof(message),
             "Sound monitor sensor failure: %s",
             esp_err_to_name(err));
    send_sms(ALARM, message, strlen(message));
    state->last_sms_tick = now;
}

// If neither slot is returning audio for a while, print a wiring hint.
static void sound_mon_maybe_log_wiring_hint(const sound_window_stats_t* window,
                                            sound_mon_state_t* state,
                                            TickType_t now) {
    if (window->nonzero_samples != 0 ||
        (now - state->last_wiring_hint_tick) <
            pdMS_TO_TICKS(SOUND_WIRING_HINT_PERIOD_MS)) {
        return;
    }

    ESP_LOGW(TAG,
             "Mic data is silent on both I2S slots. Check VDD->3.3V, GND->GND, "
             "BCLK->GPIO26, LRCLK->GPIO25, DOUT->GPIO34, and tie SEL "
             "to GND.");
    state->last_wiring_hint_tick = now;
}

// During startup, learn the noise floor from the quietest early windows before
// we allow any sound to count as "LOUD".
static void sound_mon_warmup_baseline(sound_mon_state_t* state, float rms) {
    if (state->warmup_windows >= SOUND_BASELINE_WARMUP_WINDOWS) {
        return;
    }

    // Seed the baseline from the quietest startup windows to avoid boot noise.
    state->baseline_rms =
        state->baseline_rms == 0.0f ? rms : fminf(state->baseline_rms, rms);
    ++state->warmup_windows;
}

// Keep tracking the room's background level, but do not learn from windows that
// are already considered loud or they would raise the threshold too aggressively.
static void sound_mon_update_runtime_baseline(sound_mon_state_t* state,
                                              float rms,
                                              float threshold,
                                              bool threshold_armed,
                                              bool is_loud) {
    if (!threshold_armed) {
        state->baseline_rms = state->baseline_rms == 0.0f
                                  ? rms
                                  : sound_mon_update_baseline(state->baseline_rms,
                                                              rms,
                                                              threshold);
        return;
    }

    if (!is_loud) {
        state->baseline_rms =
            sound_mon_update_baseline(state->baseline_rms, rms, threshold);
    }
}

// Print a compact status line for humans without flooding the serial monitor.
static void sound_mon_log_status(sound_mon_state_t* state,
                                 TickType_t now,
                                 float rms,
                                 float threshold,
                                 bool threshold_armed,
                                 bool is_loud) {
    ESP_LOGI(TAG,
             "value=%.2f threshold=%.2f status=%s loud_count=%u/%u",
             rms,
             threshold,
             threshold_armed ? (is_loud ? "LOUD" : "quiet") : "warming",
             (unsigned int)state->consecutive_loud_windows,
             (unsigned int)SOUND_CONSECUTIVE_LOUD_WINDOWS);
    state->last_log_tick = now;
}

// Queue one warning SMS for a loud event if the current window is above the
// threshold and the cooldown has expired. "maybe" is literal here: most loops
// do nothing because the sound is quiet or we are still inside the cooldown.
static void sound_mon_maybe_send_loud_sms(sound_mon_state_t* state,
                                          TickType_t now,
                                          float rms,
                                          float threshold,
                                          bool is_loud) {
    char message[MSG_LEN];

    if (!is_loud) {
        return;
    }

    if (state->consecutive_loud_windows < SOUND_CONSECUTIVE_LOUD_WINDOWS ||
        sound_mon_sms_cooldown_active(state, now)) {
        return;
    }

    snprintf(message,
             sizeof(message),
             "Loud sound detected after %u consecutive windows (value %.0f > "
             "threshold %.0f)",
             SOUND_CONSECUTIVE_LOUD_WINDOWS,
             rms,
             threshold);
    send_sms(WARNING, message, strlen(message));
    state->last_sms_tick = now;
    state->consecutive_loud_windows = 0;
}

static esp_err_t sound_mon_read_window(sound_window_stats_t* stats_out) {
    size_t bytes_read = 0;
    sound_window_stats_t stats = {0};

    if (!stats_out) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_i2s_ready) {
        ESP_RETURN_ON_ERROR(sound_mon_init(), TAG, "I2S init failed");
    }

    esp_err_t err = i2s_read(SOUND_I2S_PORT,
                             s_sound_raw_samples,
                             sizeof(s_sound_raw_samples),
                             &bytes_read,
                             pdMS_TO_TICKS(SOUND_WINDOW_TIMEOUT_MS));
    if (err != ESP_OK) {
        return err;
    }

    size_t frame_count =
        bytes_read / (sizeof(s_sound_raw_samples[0]) * SOUND_FRAME_SLOT_COUNT);
    if (frame_count == 0) {
        return ESP_ERR_TIMEOUT;
    }

    int64_t sample_sum[SOUND_FRAME_SLOT_COUNT] = {0};
    size_t nonzero_samples[SOUND_FRAME_SLOT_COUNT] = {0};

    for (size_t i = 0; i < frame_count; ++i) {
        for (size_t slot = 0; slot < SOUND_FRAME_SLOT_COUNT; ++slot) {
            int32_t sample = sound_mon_get_sample(i, (sound_slot_t)slot);

            if (sample != 0) {
                ++nonzero_samples[slot];
            }

            sample_sum[slot] += sample;
        }
    }

    double energy[SOUND_FRAME_SLOT_COUNT] = {0};
    int32_t sample_mean[SOUND_FRAME_SLOT_COUNT] = {
        (int32_t)(sample_sum[SOUND_SLOT_LEFT] / (int64_t)frame_count),
        (int32_t)(sample_sum[SOUND_SLOT_RIGHT] / (int64_t)frame_count),
    };

    for (size_t i = 0; i < frame_count; ++i) {
        for (size_t slot = 0; slot < SOUND_FRAME_SLOT_COUNT; ++slot) {
            double sample =
                (double)sound_mon_get_sample(i, (sound_slot_t)slot) -
                (double)sample_mean[slot];

            energy[slot] += sample * sample;
        }
    }

    float slot_rms[SOUND_FRAME_SLOT_COUNT] = {
        (float)sqrt(energy[SOUND_SLOT_LEFT] / (double)frame_count),
        (float)sqrt(energy[SOUND_SLOT_RIGHT] / (double)frame_count),
    };

    sound_slot_t active_slot = SOUND_SLOT_LEFT;
    if (nonzero_samples[SOUND_SLOT_RIGHT] > nonzero_samples[SOUND_SLOT_LEFT] &&
        slot_rms[SOUND_SLOT_RIGHT] >= slot_rms[SOUND_SLOT_LEFT]) {
        active_slot = SOUND_SLOT_RIGHT;
    }

    stats.nonzero_samples = nonzero_samples[active_slot];
    stats.rms = slot_rms[active_slot];
    *stats_out = stats;
    return ESP_OK;
}

void sound_mon_task(void* arguments) {
    sound_mon_state_t state = {
        .threshold_ratio = powf(10.0f, SOUND_THRESHOLD_MARGIN_DB / 20.0f),
    };

    (void)arguments;

    for (;;) {
        sound_window_stats_t window = {0};
        TickType_t now = xTaskGetTickCount();
        esp_err_t err = sound_mon_read_window(&window);

        if (err != ESP_OK) {
            sound_mon_handle_read_error(&state, now, err);
            vTaskDelay(pdMS_TO_TICKS(SOUND_ERROR_RETRY_DELAY_MS));
            continue;
        }

        float rms = window.rms;
        sound_mon_maybe_log_wiring_hint(&window, &state, now);
        sound_mon_warmup_baseline(&state, rms);

        // The threshold is whichever is higher: the learned noise floor plus
        // margin, or the fixed minimum floor.
        float threshold =
            fmaxf(state.baseline_rms * state.threshold_ratio, SOUND_MIN_TRIGGER_RMS);
        bool threshold_armed =
            state.warmup_windows >= SOUND_BASELINE_WARMUP_WINDOWS;
        bool is_loud = threshold_armed && rms > threshold;

        sound_mon_update_runtime_baseline(&state,
                                          rms,
                                          threshold,
                                          threshold_armed,
                                          is_loud);

        if ((now - state.last_log_tick) >= pdMS_TO_TICKS(SOUND_LOG_PERIOD_MS)) {
            if (!is_loud) {
                state.consecutive_loud_windows = 0;
            } else {
                ++state.consecutive_loud_windows;
            }

            sound_mon_log_status(&state,
                                 now,
                                 rms,
                                 threshold,
                                 threshold_armed,
                                 is_loud);
            sound_mon_maybe_send_loud_sms(&state, now, rms, threshold, is_loud);
        }

        vTaskDelay(pdMS_TO_TICKS(SOUND_LOOP_DELAY_MS));
    }
}
