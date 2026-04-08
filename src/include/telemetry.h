#ifndef TELEMETRY_H
#define TELEMETRY_H

#include <stdint.h>
#include "main.h"

typedef enum {
    BODY_TEMPERATURE,
    AMBIENT_TEMPERATURE,
    HEART_RATE,
    OXYGEN_SATURATION,
    CO2_LEVEL,
    HUMIDITY,
    MOVEMENT,
    SOUND_LEVEL,
    SMOKE_DETECTED,
    TELEMETRY_WARNING,
    TELEMETRY_ALERT
} telemetry_type_t;

typedef struct {
    telemetry_type_t type;
    char value[MSG_LEN];
    uint32_t timestamp;
} telemetry_msg_t;

extern QueueHandle_t telemetry_queue;

void send_telemetry(telemetry_type_t type, char* value);
void telemetry_task(void* arg);

#endif