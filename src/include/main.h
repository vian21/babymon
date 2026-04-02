#ifndef MAIN_H
#define MAIN_H

typedef enum { WARNING, ALARM } EVENT_LEVEL;

int send_sms(EVENT_LEVEL level, char* msg, int len);
void ambient_temp_task(void*arguments);
void sound_mon_task(void*arguments);

#endif // MAIN_H
