#include <string.h>
#include <time.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#ifndef WIFI_SSID
#error "WIFI_SSID must be defined"
#endif

#ifndef WIFI_PASSWORD
#error "WIFI_PASSWORD must be defined"
#endif

#include "wifi_events.h"

static const char* TAG = "wifi_task";

EventGroupHandle_t wifi_event_group;

static const char* wifi_disconnect_reason_to_str(uint8_t reason) {
    switch (reason) {
        case WIFI_REASON_AUTH_EXPIRE:
            return "auth expired";
        case WIFI_REASON_AUTH_LEAVE:
            return "auth leave";
        case WIFI_REASON_ASSOC_EXPIRE:
            return "assoc expired";
        case WIFI_REASON_ASSOC_TOOMANY:
            return "too many stations associated";
        case WIFI_REASON_NOT_AUTHED:
            return "not authenticated";
        case WIFI_REASON_NOT_ASSOCED:
            return "not associated";
        case WIFI_REASON_ASSOC_LEAVE:
            return "association leave";
        case WIFI_REASON_ASSOC_NOT_AUTHED:
            return "association without auth";
        case WIFI_REASON_DISASSOC_PWRCAP_BAD:
            return "disassoc power capability bad";
        case WIFI_REASON_DISASSOC_SUPCHAN_BAD:
            return "disassoc supported channel bad";
        case WIFI_REASON_IE_INVALID:
            return "invalid information element";
        case WIFI_REASON_MIC_FAILURE:
            return "MIC failure";
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
            return "4-way handshake timeout";
        case WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT:
            return "group key update timeout";
        case WIFI_REASON_IE_IN_4WAY_DIFFERS:
            return "IE differs in 4-way handshake";
        case WIFI_REASON_GROUP_CIPHER_INVALID:
            return "group cipher invalid";
        case WIFI_REASON_PAIRWISE_CIPHER_INVALID:
            return "pairwise cipher invalid";
        case WIFI_REASON_AKMP_INVALID:
            return "AKMP invalid";
        case WIFI_REASON_UNSUPP_RSN_IE_VERSION:
            return "unsupported RSN IE version";
        case WIFI_REASON_INVALID_RSN_IE_CAP:
            return "invalid RSN IE capabilities";
        case WIFI_REASON_802_1X_AUTH_FAILED:
            return "802.1X auth failed";
        case WIFI_REASON_CIPHER_SUITE_REJECTED:
            return "cipher suite rejected";
        case WIFI_REASON_BEACON_TIMEOUT:
            return "beacon timeout";
        case WIFI_REASON_NO_AP_FOUND:
            return "no AP found";
        case WIFI_REASON_AUTH_FAIL:
            return "auth failed";
        case WIFI_REASON_ASSOC_FAIL:
            return "association failed";
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
            return "handshake timeout";
        case WIFI_REASON_CONNECTION_FAIL:
            return "connection failed";
        case WIFI_REASON_AP_TSF_RESET:
            return "AP TSF reset";
        case WIFI_REASON_ROAMING:
            return "roaming";
        default:
            return "unknown";
    }
}

static void wifi_event_handler(void* arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Starting Wi-Fi station for SSID '%s'", WIFI_SSID);
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT &&
               event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t* event =
            (wifi_event_sta_disconnected_t*)event_data;
        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
        ESP_LOGW(TAG,
                 "Disconnected from '%s' (reason=%u: %s), retrying",
                 WIFI_SSID,
                 event->reason,
                 wifi_disconnect_reason_to_str(event->reason));
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
        ESP_LOGI(TAG, "Connected, IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        // Initialize SNTP
        sntp_setoperatingmode(SNTP_OPMODE_POLL);
        sntp_setservername(0, "pool.ntp.org");
        sntp_init();
        ESP_LOGI(TAG, "SNTP initialized");
    }
}

void wifi_task(void* arg) {
    wifi_event_group = xEventGroupCreate();
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_config_t wifi_config = {
        .sta =
            {
                .pmf_cfg = {
                    .capable = true,
                    .required = false,
                },
            },
    };
    strlcpy(
        (char*)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strlcpy((char*)wifi_config.sta.password,
            WIFI_PASSWORD,
            sizeof(wifi_config.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_task started, waiting for connection...");

    vTaskDelete(NULL);
}
