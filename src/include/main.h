#ifndef MAIN_H
#define MAIN_H

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"

#define MINUTE_MS (1000 * 60)
#define MSG_LEN 256

typedef enum { WARNING, ALARM } EVENT_LEVEL;

int send_sms(EVENT_LEVEL level, char* msg, int len);

// Tasks
void wifi_task(void* arguments);
void mobility_task(void* arguments);

void task_max30205_monitor(void* pvParameters);

void task_mhz19_monitor(void* pvParameters);

void task_max30102_monitor(void* pvParameters);

#endif // MAIN_H
