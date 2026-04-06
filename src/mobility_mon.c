#include <string.h>
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "main.h"

#ifndef ZIGBEE2MQTT_HOST
#error "ZIGBEE2MQTT_HOST must be defined"
#endif

#ifndef ZIGBEE2MQTT_PORT
#error "ZIGBEE2MQTT_PORT must be defined"
#endif

#ifndef MOTION_SENSOR_NAME
#error "MOTION_SENSOR_NAME must be defined"
#endif

#define NO_MOTION_WARN_TIMEOUT_MIN 40
#define NO_MOTION_ALARM_TIMEOUT_MIN 60

static const char* TAG = "mobility_mon";

typedef struct {
    bool motion_detected;
    uint32_t last_motion_time;
} motion_state_t;

static motion_state_t motion_state = {0};

static char* response_buffer = NULL;
static size_t response_len = 0;

static esp_err_t http_event_handler(esp_http_client_event_t* evt) {
    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA:
        if (evt->user_data && evt->data_len > 0) {
            char** buf = (char**)evt->user_data;
            char* new_buf = realloc(*buf, response_len + evt->data_len + 1);
            if (new_buf) {
                *buf = new_buf;
                memcpy(*buf + response_len, evt->data, evt->data_len);
                response_len += evt->data_len;
                (*buf)[response_len] = '\0';
            }
        }
        break;
    default:
        break;
    }
    return ESP_OK;
}

static void url_encode_spaces(const char* src, char* dest, size_t dest_size) {
    int j = 0;
    for (int i = 0; src[i] != '\0' && j < dest_size - 3; i++) {
        if (src[i] == ' ') {
            dest[j++] = '%';
            dest[j++] = '2';
            dest[j++] = '0';
        } else {
            dest[j++] = src[i];
        }
    }
    dest[j] = '\0';
}

static bool query_motion_state(void) {
    char url[256];
    char encoded_name[128] = {0};

    url_encode_spaces(MOTION_SENSOR_NAME, encoded_name, sizeof(encoded_name));

    snprintf(url,
             sizeof(url),
             "http://" ZIGBEE2MQTT_HOST ":" ZIGBEE2MQTT_PORT "/api/device/%s",
             encoded_name);

    free(response_buffer);
    response_buffer = NULL;
    response_len = 0;

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .user_data = &response_buffer,
        .timeout_ms = 5000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        return false;
    }

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return false;
    }

    int status = esp_http_client_get_status_code(client);
    if (status != 200) {
        ESP_LOGW(TAG, "HTTP status: %d", status);
        esp_http_client_cleanup(client);
        return false;
    }

    esp_http_client_cleanup(client);

    if (!response_buffer) {
        return false;
    }

    char* occupancy = strstr(response_buffer, "\"occupancy\":true");
    if (occupancy) {
        return true;
    }

    return false;
}

void mobility_task(void* arguments) {
    const uint32_t poll_interval_ms = 5 * MINUTE_MS; // 5 minutes
    uint32_t last_motion_tick =
        xTaskGetTickCount(); // Initialize to current tick
    bool was_motion_detected = false;
    bool warning_sent = false;
    bool alarm_sent = false;

    vTaskDelay(pdMS_TO_TICKS(10000));

    while (1) {
        bool motion = query_motion_state();

        if (motion) {
            last_motion_tick = xTaskGetTickCount();
            motion_state.motion_detected = true;
            motion_state.last_motion_time = xTaskGetTickCount();
            warning_sent = false;
            alarm_sent = false;
        } else {
            uint32_t elapsed_sec = (xTaskGetTickCount() - last_motion_tick) *
                                   portTICK_PERIOD_MS / 1000;

            if (elapsed_sec > (NO_MOTION_ALARM_TIMEOUT_MIN * 60)) {
                motion_state.motion_detected = false;
                if (!alarm_sent) {
                    ESP_LOGE(TAG, "ALARM: No motion detected for >60 mins!");
                    char msg[] =
                        "ALARM: Aucun mouvement detecte depuis plus de 60 min!";
                    send_sms(ALARM, msg, strlen(msg));
                    alarm_sent = true;
                }
            } else if (elapsed_sec > (NO_MOTION_WARN_TIMEOUT_MIN * 60)) {
                if (!warning_sent) {
                    ESP_LOGW(TAG, "WARNING: No motion detected for >40 mins.");
                    char msg[] =
                        "ATTENTION: Aucun mouvement detecte depuis plus de 40 "
                        "min.";
                    send_sms(WARNING, msg, strlen(msg));
                    warning_sent = true;
                }
            }
        }

        if (motion && !was_motion_detected) {
            ESP_LOGI(TAG, "Motion detected!");
        }
        was_motion_detected = motion;

        vTaskDelay(pdMS_TO_TICKS(poll_interval_ms));
    }
}
