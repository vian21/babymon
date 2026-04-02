#ifndef MAIN_H
#define MAIN_H

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"

typedef enum { WARNING, ALARM } EVENT_LEVEL;

int send_sms(EVENT_LEVEL level, char* msg, int len);
void wifi_task(void* arguments);

typedef struct {
    float temperature_c;
    uint32_t timestamp_ms;
} MAX30205_Data_t;

void MAX30205_Init(void);
void task_max30205_monitor(void* pvParameters);

typedef struct {
    int co2_ppm;
    uint8_t status;
    uint32_t timestamp_ms;
} MHZ19_Data_t;

void MHZ19_Init(void);
void task_mhz19_monitor(void* pvParameters);

typedef struct {
    float heart_rate_bpm;
    float spo2_percent;
    bool finger_detected;
    uint32_t timestamp_ms;
} MAX30102_Data_t;

void MAX30102_Init(void);
void task_max30102_monitor(void* pvParameters);

#endif // MAIN_H
