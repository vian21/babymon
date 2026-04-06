#ifndef MAIN_H
#define MAIN_H

#define MINUTE_MS (1000 * 60)
#define MSG_LEN 128

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

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

#endif // MAIN_H
