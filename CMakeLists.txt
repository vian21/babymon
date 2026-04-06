# The following lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.22)

if(EXISTS ${CMAKE_SOURCE_DIR}/.env)
    file(STRINGS ${CMAKE_SOURCE_DIR}/.env env_contents REGEX "^[^#].*")
    foreach(line ${env_contents})
        if(line MATCHES "^([A-Za-z_][A-Za-z0-9_]*)=(.*)$")
            set(${CMAKE_MATCH_1} "${CMAKE_MATCH_2}" CACHE STRING "Twilio config")
        endif()
    endforeach()
endif()

if(NOT TWILIO_ACCOUNT_SID OR NOT TWILIO_AUTH_TOKEN OR NOT TWILIO_FROM_NUMBER OR NOT TWILIO_TO_NUMBER)
    message(FATAL_ERROR "Twilio credentials not set. Create .env file from .env.template")
endif()

if(NOT WIFI_SSID)
    message(FATAL_ERROR "WIFI_SSID not set. Create .env file from .env.template")
endif()

if(NOT WIFI_PASSWORD)
    message(FATAL_ERROR "WIFI_PASSWORD not set. Create .env file from .env.template")
endif()

message(STATUS "TWILIO_ACCOUNT_SID=${TWILIO_ACCOUNT_SID}")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
set(EXTRA_COMPONENT_DIRS src)
project(BabyMon)

set(REQUIRES esp_http_client mbedtls esp_crt_bundle)
