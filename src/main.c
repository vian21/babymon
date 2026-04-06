#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stddef.h>

#include "main.h"

void app_main(void) {
    xTaskCreate(wifi_task, "Wifi Man", 4096, NULL, 5, NULL);
    xTaskCreate(mobility_task, "Mobility Mon", 4096, NULL, 5, NULL);
}
