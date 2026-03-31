#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stddef.h>

#include "main.h"

void app_main(void) {
    xTaskCreate(wifi_task, "Wifi Man", 4096, NULL, 5, NULL);
}
