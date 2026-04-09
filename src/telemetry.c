#include "telemetry.h"
#include <inttypes.h>
#include <string.h>
#include <time.h>
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "wifi_events.h"

#ifndef TELEMETRY_SERVER_IP
#error "TELEMETRY_SERVER_IP must be defined"
#endif

#ifndef TELEMETRY_SERVER_PORT
#error "TELEMETRY_SERVER_PORT must be defined"
#endif

static const char* TAG = "telemetry_task";

QueueHandle_t telemetry_queue;

const char* type_strings[] = {"BODY_TEMPERATURE",
                              "AMBIENT_TEMPERATURE",
                              "HEART_RATE",
                              "OXYGEN_SATURATION",
                              "CO2_LEVEL",
                              "HUMIDITY",
                              "MOVEMENT",
                              "SOUND_LEVEL",
                              "SMOKE_DETECTED",
                              "TELEMETRY_WARNING",
                              "TELEMETRY_ALERT"};

esp_err_t telemetry_http_event_handler(esp_http_client_event_t* evt) {
    switch (evt->event_id) {
    case HTTP_EVENT_ERROR:
        ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
        break;
    case HTTP_EVENT_HEADER_SENT:
        ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
        break;
    case HTTP_EVENT_ON_HEADER:
        ESP_LOGD(TAG,
                 "HTTP_EVENT_ON_HEADER, key=%s, value=%s",
                 evt->header_key,
                 evt->header_value);
        break;
    case HTTP_EVENT_ON_DATA:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
        if (!esp_http_client_is_chunked_response(evt->client)) {
            printf("%.*s", evt->data_len, (char*)evt->data);
        }
        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
        break;
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
        break;
    case HTTP_EVENT_REDIRECT:
        ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
        break;
    }
    return ESP_OK;
}

void send_telemetry(telemetry_type_t type, char* value) {
    telemetry_msg_t msg;
    msg.type = type;
    strncpy(msg.value, value, MSG_LEN - 1);
    msg.value[MSG_LEN - 1] = '\0';
    msg.timestamp = (uint32_t)time(NULL);
    if (xQueueSend(telemetry_queue, &msg, 0) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to send telemetry to queue");
    }
}

void telemetry_task(void* arg) {
    telemetry_msg_t msg;
    while (1) {
        // Wait for WiFi
        xEventGroupWaitBits(wifi_event_group,
                            WIFI_CONNECTED_BIT,
                            pdFALSE,
                            pdTRUE,
                            portMAX_DELAY);

        if (xQueueReceive(telemetry_queue, &msg, portMAX_DELAY) == pdTRUE) {
            char post_data[256];
            snprintf(post_data,
                     sizeof(post_data),
                     "{\"type\":\"%s\",\"value\":\"%s\",\"timestamp\":%" PRIu32
                     "}",
                     type_strings[msg.type],
                     msg.value,
                     msg.timestamp);

            esp_http_client_config_t config = {
                .url = "http://" TELEMETRY_SERVER_IP ":" TELEMETRY_SERVER_PORT
                       "/api/telemetry",
                .event_handler = telemetry_http_event_handler,
                .crt_bundle_attach = esp_crt_bundle_attach,
                .method = HTTP_METHOD_POST,
            };

            esp_http_client_handle_t client = esp_http_client_init(&config);
            esp_http_client_set_post_field(
                client, post_data, strlen(post_data));
            esp_http_client_set_header(
                client, "Content-Type", "application/json");

            esp_err_t err = esp_http_client_perform(client);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "Telemetry sent successfully");
            } else {
                ESP_LOGE(
                    TAG, "Failed to send telemetry: %s", esp_err_to_name(err));
            }

            esp_http_client_cleanup(client);
        }
    }
}