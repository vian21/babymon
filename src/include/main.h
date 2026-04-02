#ifndef MAIN_H
#define MAIN_H

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#define MINUTE_MS (1000 * 60)
#define MSG_LEN 128

typedef enum { WARNING, ALARM } EVENT_LEVEL;

typedef struct {
    EVENT_LEVEL level;
    char msg[MSG_LEN];
} sms_msg_t;

extern QueueHandle_t sms_queue;

typedef enum { TEMPERATURE, HUMIDITY } measurement_type_t;

int control_hvac(measurement_type_t type, float current_val, float desired_val);
void wifi_task(void* arguments);
void mobility_task(void* arguments);
void sms_task(void* arguments);
void task_max30205_monitor(void* pvParameters);
void task_mhz19_monitor(void* pvParameters);
void task_max30102_monitor(void* pvParameters);

#endif // MAIN_H
