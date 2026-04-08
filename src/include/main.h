#ifndef MAIN_H
#define MAIN_H

typedef enum { WARNING, ALARM } EVENT_LEVEL;
typedef enum {
    DATA_TYPE_AMBIENT_TEMP = 0,
} DATA_TYPE;

typedef struct {
    DATA_TYPE type;
    float value;
    float wanted_value;
} ambient_task_args_t;

int send_sms(EVENT_LEVEL level, char* msg, int len);
void ambient_temp_task(void* arguments);
void sound_mon_task(void* arguments);
#endif // MAIN_H
