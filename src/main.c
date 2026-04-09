#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stddef.h>

#include "main.h"
#include "telemetry.h"
#include "test.h"

QueueHandle_t sms_queue;

void app_main(void) {
    sms_queue = xQueueCreate(5, sizeof(sms_msg_t));
    if (sms_queue == NULL) {
        ESP_LOGE("MAIN", "Failed to create SMS queue");
    }

    telemetry_queue = xQueueCreate(20, sizeof(telemetry_msg_t));
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

    xTaskCreate(task_max30205_monitor, "Body Temp Mon", 4096, NULL, 5, NULL);

    xTaskCreate(task_max30102_monitor, "Heart Rate Mon", 4096, NULL, 5, NULL);

    xTaskCreate(task_mhz19_monitor, "CO2 Mon", 4096, NULL, 5, NULL);

    // Tests
    // xTaskCreate(test_sms, "test_sms", 4096, NULL, 5, NULL);
    // xTaskCreate(test_telemetry, "test_telemetry", 4096, NULL, 5, NULL);
}