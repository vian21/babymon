#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "main.h"
#include "telemetry.h"
#include "wifi_events.h"

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
    SemaphoreHandle_t lock;
} motion_state_t;

static motion_state_t motion_state = {0};

static void websocket_event_handler(void* handler_args,
                                    esp_event_base_t base,
                                    int32_t event_id,
                                    void* event_data) {
    esp_websocket_event_data_t* data = (esp_websocket_event_data_t*)event_data;
    switch (event_id) {
    case WEBSOCKET_EVENT_DATA:
        if (data->op_code == WS_TRANSPORT_OPCODES_TEXT) {
            // Check if the message contains occupancy:true
            // Zigbee2MQTT WS API sends messages like:
            // {"topic":"zigbee2mqtt/SENSOR_NAME","payload":"{\"occupancy\":true,...}"}
            // Check if this fragment contains our sensor name AND occupancy
            // state Note: In some versions, the payload is stringified inside a
            // "payload" field
            char* raw_str = (char*)data->data_ptr;

            // We search for the sensor name to ensure we aren't picking up
            // occupancy from a different room/sensor.
            if (strstr(raw_str, MOTION_SENSOR_NAME)) {
                if (strstr(raw_str, "\"occupancy\":true")) {
                    xSemaphoreTake(motion_state.lock, portMAX_DELAY);
                    motion_state.motion_detected = true;
                    motion_state.last_motion_time = xTaskGetTickCount();
                    xSemaphoreGive(motion_state.lock);
                    ESP_LOGI(TAG, "Matched: %s is ACTIVE", MOTION_SENSOR_NAME);
                } else if (strstr(raw_str, "\"occupancy\":false")) {
                    // We don't force false here because we want the 10s
                    // loop to handle the "active" window.
                    ESP_LOGI(TAG, "Matched: %s is idle", MOTION_SENSOR_NAME);
                }
            }
        }
        break;
    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGE(TAG, "Websocket Error");
        break;
    default:
        break;
    }
}

void mobility_task(void* arguments) {
    char msg[MSG_LEN];
    bool warning_sent = false;
    bool alarm_sent = false;

    motion_state.lock = xSemaphoreCreateMutex();

    // Wait for WiFi to be connected
    if (wifi_event_group != NULL) {
        xEventGroupWaitBits(wifi_event_group,
                            WIFI_CONNECTED_BIT,
                            pdFALSE,
                            pdTRUE,
                            portMAX_DELAY);
    }

    char url[128];
    snprintf(
        url, sizeof(url), "ws://" ZIGBEE2MQTT_HOST ":" ZIGBEE2MQTT_PORT "/api");

    esp_websocket_client_config_t ws_cfg = {
        .uri = url,
        .buffer_size = 1024, // Increase buffer size to handle larger
                             // Zigbee2MQTT headers/payloads
    };

    ESP_LOGI(TAG, "Connecting to %s", url);
    esp_websocket_client_handle_t client = esp_websocket_client_init(&ws_cfg);
    esp_websocket_register_events(
        client, WEBSOCKET_EVENT_ANY, websocket_event_handler, (void*)client);

    esp_websocket_client_start(client);

    uint32_t last_motion_tick = xTaskGetTickCount();

    while (1) {
        xSemaphoreTake(motion_state.lock, portMAX_DELAY);
        bool current_motion = motion_state.motion_detected;
        uint32_t last_m = motion_state.last_motion_time;
        // Reset motion detected flag for the next period
        motion_state.motion_detected = false;
        xSemaphoreGive(motion_state.lock);

        uint32_t elapsed_sec = (xTaskGetTickCount() - last_motion_tick) *
                               portTICK_PERIOD_MS / 1000;

        if (current_motion) {
            last_motion_tick = last_m;
            warning_sent = false;
            alarm_sent = false;
            ESP_LOGI(TAG, "Motion detected!");
            send_telemetry(MOVEMENT, "1");
            elapsed_sec = 0; // Motion happened in this window
        } else {
            send_telemetry(MOVEMENT, "0");
        }

        char elapsed_str[16];
        snprintf(elapsed_str,
                 sizeof(elapsed_str),
                 "%lu",
                 (unsigned long)elapsed_sec);
        send_telemetry(MOVEMENT_LAST_TIME, elapsed_str);

        if (!current_motion) {
            if (elapsed_sec > (NO_MOTION_ALARM_TIMEOUT_MIN * 60)) {
                if (!alarm_sent) {
                    ESP_LOGE(TAG, "ALARM: No motion detected for >60 mins!");
                    snprintf(msg,
                             MSG_LEN,
                             "ALARM: Aucun mouvement detecte depuis plus de 60 "
                             "min!");
                    send_sms(ALARM, msg, strlen(msg));
                    send_telemetry(TELEMETRY_ALERT, msg);
                    alarm_sent = true;
                }
            } else if (elapsed_sec > (NO_MOTION_WARN_TIMEOUT_MIN * 60)) {
                if (!warning_sent) {
                    ESP_LOGW(TAG, "WARNING: No motion detected for >40 mins.");
                    snprintf(msg,
                             MSG_LEN,
                             "ATTENTION: Aucun mouvement detecte depuis plus "
                             "de 40 min.");
                    send_sms(WARNING, msg, strlen(msg));
                    send_telemetry(TELEMETRY_WARNING, msg);
                    warning_sent = true;
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10000)); // Check state every 10 seconds
    }

    esp_websocket_client_stop(client);
    esp_websocket_client_destroy(client);
}
