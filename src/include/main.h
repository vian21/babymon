#ifndef MAIN_H
#define MAIN_H

typedef enum { WARNING, ALARM } EVENT_LEVEL;

int send_sms(EVENT_LEVEL level, char* msg, int len);
#endif // MAIN_H
