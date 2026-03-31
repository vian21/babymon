idf_component_register(SRCS "main.c"
                            "ambient_temp.c"
                            "co2.c"
                            "body_temp.c"
                            "hvac.c"
                            "mobility_mon.c"
                            "oxy_sat.c"
                            "sound_mon.c"
                            "sms.c"
                    INCLUDE_DIRS "." "include"
                    REQUIRES esp_http_client mbedtls)

target_compile_definitions(${COMPONENT_TARGET} PRIVATE
    TWILIO_ACCOUNT_SID="${TWILIO_ACCOUNT_SID}"
    TWILIO_AUTH_TOKEN="${TWILIO_AUTH_TOKEN}"
    TWILIO_FROM_NUMBER="${TWILIO_FROM_NUMBER}"
    TWILIO_TO_NUMBER="${TWILIO_TO_NUMBER}"
)
