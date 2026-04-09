#ifndef MAIN_H
#define MAIN_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#define MINUTE_MS (1000 * 60)
#define MSG_LEN 128

typedef enum { WARNING, ALARM } EVENT_LEVEL;
typedef enum {
    DATA_TYPE_AMBIENT_TEMP = 0,
} DATA_TYPE;

typedef struct {
    DATA_TYPE type;
    float value;
    float wanted_value;
} ambient_task_args_t;

typedef struct {
    EVENT_LEVEL level;
    char msg[MSG_LEN];
} sms_msg_t;

extern QueueHandle_t sms_queue;

typedef enum {
    TEMPERATURE_MEASUREMENT,
    HUMIDITY_MEASUREMENT
} measurement_type_t;

int control_hvac(measurement_type_t type, float current_val, float desired_val);
void wifi_task(void* arguments);
void mobility_task(void* arguments);
void sms_task(void* arguments);
void task_max30205_monitor(void* pvParameters);
void task_mhz19_monitor(void* pvParameters);
void task_max30102_monitor(void* pvParameters);
int send_sms(EVENT_LEVEL level, char* msg, int len);
void amb_temp_hum_task(void* arguments);
void sound_mon_task(void* arguments);
#endif // MAIN_H
