#ifndef WIFI_EVENTS_H
#define WIFI_EVENTS_H

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

extern EventGroupHandle_t wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0

#endif // WIFI_EVENTS_H
