#ifndef MAIN_H
#define MAIN_H

#define MINUTE_MS (1000 * 60)
#define MSG_LEN 256

typedef enum { WARNING, ALARM } EVENT_LEVEL;

int send_sms(EVENT_LEVEL level, char* msg, int len);

// Tasks
void wifi_task(void* arguments);
void mobility_task(void* arguments);

#endif // MAIN_H
