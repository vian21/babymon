#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_http_client.h"
#include "esp_log.h"
#include "main.h"
#include "mbedtls/base64.h"

#ifndef TWILIO_ACCOUNT_SID
#error "TWILIO_ACCOUNT_SID must be set"
#endif

#ifndef TWILIO_AUTH_TOKEN
#error "TWILIO_AUTH_TOKEN must be set"
#endif

#ifndef TWILIO_FROM_NUMBER
#error "TWILIO_FROM_NUMBER must be set"
#endif

#ifndef TWILIO_TO_NUMBER
#error "TWILIO_TO_NUMBER must be set"
#endif

static const char* TAG = "SMS";

static esp_err_t http_event_handler(esp_http_client_event_t* evt) {
    switch (evt->event_id) {
    case HTTP_EVENT_ERROR:
        ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
        break;
    case HTTP_EVENT_HEADER_SENT:
        ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
        break;
    case HTTP_EVENT_ON_HEADER:
        ESP_LOGD(TAG,
                 "HTTP_EVENT_ON_HEADER: key=%s, value=%s",
                 evt->header_key,
                 evt->header_value);
        break;
    case HTTP_EVENT_ON_DATA:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
        break;
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
        break;
    default:
        break;
    }
    return ESP_OK;
}

static void url_encode_msg(const char* src, char* dest, size_t dest_size) {
    int j = 0;
    for (int i = 0; src[i] != '\0' && j < (int)dest_size - 1; i++) {
        if (src[i] == ' ') {
            dest[j++] = '+';
        } else {
            dest[j++] = src[i];
        }
    }
    dest[j] = '\0';
}

int send_sms(EVENT_LEVEL level, char* msg, int len) {
    char auth_header[128];
    char post_data[512];
    char url[256];
    char encoded_msg[MSG_LEN * 2] = {0};

    url_encode_msg(msg, encoded_msg, sizeof(encoded_msg));

    char credentials[64];
    snprintf(credentials,
             sizeof(credentials),
             "%s:%s",
             TWILIO_ACCOUNT_SID,
             TWILIO_AUTH_TOKEN);

    unsigned char encoded[64];
    mbedtls_base64_encode(encoded,
                          sizeof(encoded),
                          NULL,
                          (unsigned char*)credentials,
                          strlen(credentials));

    snprintf(auth_header, sizeof(auth_header), "Basic %s", encoded);

    snprintf(url,
             sizeof(url),
             "https://api.twilio.com/2010-04-01/Accounts/%s/Messages.json",
             TWILIO_ACCOUNT_SID);

    snprintf(post_data,
             sizeof(post_data),
             "To=%s&From=%s&Body=[%s]+%s",
             TWILIO_TO_NUMBER,
             TWILIO_FROM_NUMBER,
             level == WARNING ? "WARNING" : "ALARM",
             encoded_msg);

    ESP_LOGI(TAG, "Sending SMS: %s", post_data);

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .method = HTTP_METHOD_POST,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        return -1;
    }

    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_http_client_set_header(
        client, "Content-Type", "application/x-www-form-urlencoded");

    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "HTTP POST Status = %d", status);
        if (status == 201 || status == 200) {
            ESP_LOGI(TAG, "SMS sent successfully");
        } else {
            ESP_LOGE(TAG, "SMS failed with status %d", status);
        }
    } else {
        ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return err == ESP_OK ? 0 : -1;
}
