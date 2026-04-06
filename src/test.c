#include "test.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "main.h"
#include "wifi_events.h"

static const char* TAG = "TEST";

void test_sms(void* pvParameters) {
    ESP_LOGI(TAG, "Waiting for WiFi connection...");
    xEventGroupWaitBits(
        wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);

    ESP_LOGI(TAG, "WiFi connected! Starting test_sms sequence");

    char warning_msg[] = "Test Warning SMS";
    char alarm_msg[] = "Test Alarm SMS";

    ESP_LOGI(TAG, "Sending Warning level SMS...");
    send_sms(WARNING, warning_msg, sizeof(warning_msg));

    // Small delay between sends
    vTaskDelay(pdMS_TO_TICKS(2000));

    ESP_LOGI(TAG, "Sending Alarm level SMS...");
    send_sms(ALARM, alarm_msg, sizeof(alarm_msg));

    ESP_LOGI(TAG, "test_sms task completed, deleting itself");
    vTaskDelete(NULL);
}
