#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stddef.h>

#include "main.h"

void app_main(void) {
    MAX30205_Init();
    MAX30102_Init();
    MHZ19_Init();

    xTaskCreate(wifi_task, "Wifi Man", 4096, NULL, 5, NULL);
    xTaskCreate(mobility_task, "Mobility Mon", 4096, NULL, 5, NULL);
    xTaskCreate(task_max30205_monitor, "Body Temp Mon", 4096, NULL, 5, NULL);

    xTaskCreate(task_max30102_monitor, "Heart Rate Mon", 4096, NULL, 5, NULL);

    xTaskCreate(task_mhz19_monitor, "CO2 Mon", 4096, NULL, 5, NULL);
}
