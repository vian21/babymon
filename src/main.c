#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "include/main.h"
static ambient_task_args_t ambient_temp_args = {
    .type = DATA_TYPE_AMBIENT_TEMP,
    .value = 0.0f,
    .wanted_value = 21.0f,
};

#define BLINK_GPIO 2

static void blink_task(void* arg) {
    gpio_reset_pin(BLINK_GPIO);
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);

    while (1) {
        gpio_set_level(BLINK_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(1000));
        gpio_set_level(BLINK_GPIO, 0);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void) {
    xTaskCreate(blink_task, "blink", 2048, NULL, 5, NULL);
    xTaskCreate(ambient_temp_task, "Ambient Temp", 4096, &ambient_temp_args, 4, NULL);
    xTaskCreate(sound_mon_task, "Sound Monitor", 8192, NULL, 3, NULL);
}
