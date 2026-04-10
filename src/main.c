#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stddef.h>

#include "main.h"
#include "telemetry.h"
#include "test.h"

QueueHandle_t sms_queue;
static ambient_task_args_t ambient_temp_args = {
    .type = DATA_TYPE_AMBIENT_TEMP,
    .value = 0.0f,
    .wanted_value = 21.0f,
};


void app_main(void) {
    sms_queue = xQueueCreate(SMS_QUEUE_SIZE, sizeof(sms_msg_t));
    if (sms_queue == NULL) {
        ESP_LOGE("MAIN", "Failed to create SMS queue");
    }

    telemetry_queue = xQueueCreate(TELEMETRY_QUEUE_SIZE, sizeof(telemetry_msg_t));
    if (telemetry_queue == NULL) {
        ESP_LOGE("MAIN", "Failed to create telemetry queue");
    }

    xTaskCreate(wifi_task, "Wifi Man", 4096, NULL, 5, NULL);

    xTaskCreate(sms_task, "SMS Worker", 8192, NULL, 5, NULL);

    xTaskCreate(mobility_task, "Mobility Mon", 4096, NULL, 5, NULL);

    xTaskCreate(telemetry_task, "Telemetry Worker", 8192, NULL, 5, NULL);

    xTaskCreate(task_max30205_monitor, "Body Temp Mon", 4096, NULL, 5, NULL);

    xTaskCreate(task_max30102_monitor, "Heart Rate Mon", 4096, NULL, 5, NULL);

    xTaskCreate(task_mhz19_monitor, "CO2 Mon", 4096, NULL, 5, NULL);

    xTaskCreate(amb_temp_hum_task, "Ambient Temp", 4096, &ambient_temp_args, 4, NULL);

    xTaskCreate(sound_mon_task, "Sound Monitor", 8192, NULL, 3, NULL);

    // Tests
    // xTaskCreate(test_sms, "test_sms", 4096, NULL, 5, NULL);
    // xTaskCreate(test_telemetry, "test_telemetry", 4096, NULL, 5, NULL);
}