
idf_component_register(SRCS "main.c"
                            "co2.c"
                            "heart_rate.c"
                            "hvac.c"
                            "mobility_mon.c"
                            "oxy_sat.c"
                            "sound_mon.c"
                            "sms.c"
                            "wifi.c"
                            "test.c"
                            "telemetry.c"
                            "amb_temp_humidity.c"
                            "bme680.c"
                            "body_temp.c"
                            "shared_i2c.c"
                            "max30205_driver.c"
                            "max30102.c"
                    INCLUDE_DIRS "." "include"
                    REQUIRES esp_http_client
                             esp_event
                             esp_netif
                             esp_wifi
                             mbedtls
                             nvs_flash
                             driver)

target_compile_definitions(${COMPONENT_TARGET} PRIVATE
    TWILIO_ACCOUNT_SID="${TWILIO_ACCOUNT_SID}"
    TWILIO_AUTH_TOKEN="${TWILIO_AUTH_TOKEN}"
    TWILIO_FROM_NUMBER="${TWILIO_FROM_NUMBER}"
    TWILIO_TO_NUMBER="${TWILIO_TO_NUMBER}"
    WIFI_SSID="${WIFI_SSID}"
    WIFI_PASSWORD="${WIFI_PASSWORD}"
    ZIGBEE2MQTT_HOST="${ZIGBEE2MQTT_HOST}"
    ZIGBEE2MQTT_PORT="${ZIGBEE2MQTT_PORT}"
    MOTION_SENSOR_NAME="${MOTION_SENSOR_NAME}"
    MOTION_TIMEOUT_SEC="${MOTION_TIMEOUT_SEC}"
    TELEMETRY_SERVER_IP="${TELEMETRY_SERVER_IP}"
    TELEMETRY_SERVER_PORT="${TELEMETRY_SERVER_PORT}"
)
