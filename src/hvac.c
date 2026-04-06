#include <stdlib.h>
#include <string.h>
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "main.h"

static const char* TAG = "HVAC";

static int handle_temperature(float current_val, float desired_val) {
    esp_http_client_config_t config = {
        .url = "https://" HA_HOST ":" HA_PORT
               "/api/services/climate/set_temperature",
        .method = HTTP_METHOD_POST,
        .cert_pem = NULL,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Authorization", "Bearer " HA_TOKEN);
    esp_http_client_set_header(client, "Content-Type", "application/json");

    char post_data[256];
    int ret = 0;

    // Set temperature
    snprintf(post_data,
             sizeof(post_data),
             "{\"entity_id\":\"%s\",\"temperature\":%.1f}",
             CLIMATE_ENTITY_ID,
             desired_val);
    esp_http_client_set_post_field(client, post_data, strlen(post_data));
    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set temperature: %s", esp_err_to_name(err));
        ret = -1;
    } else if (esp_http_client_get_status_code(client) != 200) {
        ESP_LOGE(
            TAG, "HA API error: %d", esp_http_client_get_status_code(client));
        ret = -1;
    }
    esp_http_client_cleanup(client);

    // Set mode
    config.url =
        "https://" HA_HOST ":" HA_PORT "/api/services/climate/set_hvac_mode";
    client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Authorization", "Bearer " HA_TOKEN);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    const char* mode;
    if (current_val < desired_val - 0.5f) {
        mode = "heat";
    } else if (current_val > desired_val + 0.5f) {
        mode = "cool";
    } else {
        mode = "off";
    }
    snprintf(post_data,
             sizeof(post_data),
             "{\"entity_id\":\"%s\",\"hvac_mode\":\"%s\"}",
             CLIMATE_ENTITY_ID,
             mode);
    esp_http_client_set_post_field(client, post_data, strlen(post_data));
    err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set mode: %s", esp_err_to_name(err));
        ret = -1;
    } else if (esp_http_client_get_status_code(client) != 200) {
        ESP_LOGE(
            TAG, "HA API error: %d", esp_http_client_get_status_code(client));
        ret = -1;
    }
    esp_http_client_cleanup(client);

    // Fan control
    if (strcmp(mode, "cool") == 0) {
        config.url = "https://" HA_HOST ":" HA_PORT "/api/services/fan/turn_on";
    } else {
        config.url =
            "https://" HA_HOST ":" HA_PORT "/api/services/fan/turn_off";
    }
    client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Authorization", "Bearer " HA_TOKEN);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    snprintf(
        post_data, sizeof(post_data), "{\"entity_id\":\"%s\"}", FAN_ENTITY_ID);
    esp_http_client_set_post_field(client, post_data, strlen(post_data));
    err = esp_http_client_perform(client);
    if (err != ESP_OK || esp_http_client_get_status_code(client) != 200) {
        ESP_LOGE(TAG, "Failed to control fan");
        ret = -1;
    }
    esp_http_client_cleanup(client);

    return ret;
}

static int handle_humidity(float current_val, float desired_val) {
    esp_http_client_config_t config = {
        .url =
            "https://" HA_HOST ":" HA_PORT "/api/services/climate/set_humidity",
        .method = HTTP_METHOD_POST,
        .cert_pem = NULL,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Authorization", "Bearer " HA_TOKEN);
    esp_http_client_set_header(client, "Content-Type", "application/json");

    char post_data[256];
    int ret = 0;

    snprintf(post_data,
             sizeof(post_data),
             "{\"entity_id\":\"%s\",\"humidity\":%.1f}",
             CLIMATE_ENTITY_ID,
             desired_val);
    esp_http_client_set_post_field(client, post_data, strlen(post_data));
    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set humidity: %s", esp_err_to_name(err));
        ret = -1;
    } else if (esp_http_client_get_status_code(client) != 200) {
        ESP_LOGE(
            TAG, "HA API error: %d", esp_http_client_get_status_code(client));
        ret = -1;
    }
    esp_http_client_cleanup(client);

    return ret;
}

int control_hvac(measurement_type_t type,
                 float current_val,
                 float desired_val) {
    switch (type) {
    case TEMPERATURE:
        return handle_temperature(current_val, desired_val);
    case HUMIDITY:
        return handle_humidity(current_val, desired_val);
    default:
        ESP_LOGE(TAG, "Unknown measurement type");
        return -1;
    }
}
esp_http_client_cleanup(client);

// Set mode
config.url =
    "https://" HA_HOST ":" HA_PORT "/api/services/climate/set_hvac_mode";
client = esp_http_client_init(&config);
esp_http_client_set_header(client, "Authorization", "Bearer " HA_TOKEN);
esp_http_client_set_header(client, "Content-Type", "application/json");
const char* mode;
if (current_val < desired_val - 0.5f) {
    mode = "heat";
} else if (current_val > desired_val + 0.5f) {
    mode = "cool";
} else {
    mode = "off";
}
snprintf(post_data,
         sizeof(post_data),
         "{\"entity_id\":\"%s\",\"hvac_mode\":\"%s\"}",
         CLIMATE_ENTITY_ID,
         mode);
esp_http_client_set_post_field(client, post_data, strlen(post_data));
err = esp_http_client_perform(client);
if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set mode: %s", esp_err_to_name(err));
    ret = -1;
} else if (esp_http_client_get_status_code(client) != 200) {
    ESP_LOGE(TAG, "HA API error: %d", esp_http_client_get_status_code(client));
    ret = -1;
}
esp_http_client_cleanup(client);

// Fan control
if (strcmp(mode, "cool") == 0) {
    config.url = "https://" HA_HOST ":" HA_PORT "/api/services/fan/turn_on";
} else {
    config.url = "https://" HA_HOST ":" HA_PORT "/api/services/fan/turn_off";
}
client = esp_http_client_init(&config);
esp_http_client_set_header(client, "Authorization", "Bearer " HA_TOKEN);
esp_http_client_set_header(client, "Content-Type", "application/json");
snprintf(post_data, sizeof(post_data), "{\"entity_id\":\"%s\"}", FAN_ENTITY_ID);
esp_http_client_set_post_field(client, post_data, strlen(post_data));
err = esp_http_client_perform(client);
if (err != ESP_OK || esp_http_client_get_status_code(client) != 200) {
    ESP_LOGE(TAG, "Failed to control fan");
    ret = -1;
}
esp_http_client_cleanup(client);
}
else if (type == HUMIDITY) {
    config.url =
        "https://" HA_HOST ":" HA_PORT "/api/services/climate/set_humidity";
    client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Authorization", "Bearer " HA_TOKEN);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    snprintf(post_data,
             sizeof(post_data),
             "{\"entity_id\":\"%s\",\"humidity\":%.1f}",
             CLIMATE_ENTITY_ID,
             desired_val);
    esp_http_client_set_post_field(client, post_data, strlen(post_data));
    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set humidity: %s", esp_err_to_name(err));
        ret = -1;
    } else if (esp_http_client_get_status_code(client) != 200) {
        ESP_LOGE(
            TAG, "HA API error: %d", esp_http_client_get_status_code(client));
        ret = -1;
    }
    esp_http_client_cleanup(client);
}

return ret;
}
