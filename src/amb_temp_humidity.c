#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "driver/i2c.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bme680.h"
#include "main.h"
#include "telemetry.h"

#define I2C_BUS I2C_NUM_0
#define I2C_SCL_PIN 22
#define I2C_SDA_PIN 21
#define I2C_FREQ 100000 // 100 kHz

// Thresholds from requirements.md
#define AMBIENT_TEMP_MIN_C 20.0f // Turn on heating below this (requirement 30)
#define AMBIENT_TEMP_MAX_C 22.2f // Turn on cooling above this (requirement 31)
#define HUMIDITY_MAX_PERCENT                                                   \
    55.0f // Turn on dehumidifier above this (requirement 35)
#define TEMP_WARNING_THRESHOLD                                                 \
    1.0f // Send warning when temp is this far from ideal range

static bme680_sensor_t* sensor = 0;
static const char* TAG = "BME680";
static bool heating_active = false;
static bool cooling_active = false;
static bool dehumidifier_active = false;

// Function prototypes
static void validate_temperature(float temperature_c, float humidity_percent);
static void validate_humidity(float humidity_percent);

void bme680_init(void) {
    /** -- MANDATORY PART -- */

    // Init I2C bus interface for BME680 sensor
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_SDA_PIN,
        .scl_io_num = I2C_SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_FREQ,
    };

    i2c_param_config(I2C_BUS, &conf);
    i2c_driver_install(I2C_BUS, I2C_MODE_MASTER, 0, 0, 0);

    // init the sensor with slave address BME680_I2C_ADDRESS_2 connected to
    // I2C_BUS.
    sensor = bme680_init_sensor(I2C_BUS, BME680_I2C_ADDRESS_2, 0);

    if (sensor) {
        /** -- SENSOR CONFIGURATION PART (optional) --- */

        // Changes the oversampling rates to 4x oversampling for temperature
        // and 2x oversampling for humidity. Pressure measurement is skipped.
        bme680_set_oversampling_rates(sensor, osr_4x, osr_none, osr_2x);

        // Change the IIR filter size for temperature and pressure to 7.
        bme680_set_filter_size(sensor, iir_size_7);

        // Change the heater profile 0 to 200 degree Celcius for 100 ms.
        bme680_set_heater_profile(sensor, 0, 200, 100);
        bme680_use_heater_profile(sensor, 0);

        // Set ambient temperature to 10 degree Celsius
        bme680_set_ambient_temperature(sensor, 10);
    } else
        ESP_LOGI(TAG, "Could not initialize BME680 sensor\n");
}

static void validate_temperature(float temperature_c, float humidity_percent) {
    char sms_buf[MSG_LEN];
    char telemetry_buf[MSG_LEN];

    // Check temperature thresholds and control HVAC
    if (temperature_c < AMBIENT_TEMP_MIN_C) {
        // Temperature too low - need heating
        if (!heating_active) {
            heating_active = true;
            cooling_active = false;

            // Control HVAC (heating)
            control_hvac(TEMPERATURE_MEASUREMENT,
                         temperature_c,
                         AMBIENT_TEMP_MIN_C + 1.0f);

            // Send SMS alert
            int len = snprintf(
                sms_buf,
                sizeof(sms_buf),
                "[AMBIENT] Temperature too low: %.2f°C. Heating activated.",
                temperature_c);
            send_sms(WARNING, sms_buf, len);

            // Send telemetry alert
            snprintf(telemetry_buf,
                     sizeof(telemetry_buf),
                     "Low temp: %.2f°C, heating ON",
                     temperature_c);
            send_telemetry(TELEMETRY_WARNING, telemetry_buf);
        }
    } else if (temperature_c > AMBIENT_TEMP_MAX_C) {
        // Temperature too high - need cooling
        if (!cooling_active) {
            cooling_active = true;
            heating_active = false;

            // Control HVAC (cooling/ventilation)
            control_hvac(TEMPERATURE_MEASUREMENT,
                         temperature_c,
                         AMBIENT_TEMP_MAX_C - 1.0f);

            // Send SMS alert
            int len = snprintf(
                sms_buf,
                sizeof(sms_buf),
                "[AMBIENT] Temperature too high: %.2f°C. Cooling activated.",
                temperature_c);
            send_sms(WARNING, sms_buf, len);

            // Send telemetry alert
            snprintf(telemetry_buf,
                     sizeof(telemetry_buf),
                     "High temp: %.2f°C, cooling ON",
                     temperature_c);
            send_telemetry(TELEMETRY_WARNING, telemetry_buf);
        }
    } else {
        // Temperature within normal range
        if (heating_active || cooling_active) {
            // Turn off HVAC if it was active
            heating_active = false;
            cooling_active = false;
            control_hvac(TEMPERATURE_MEASUREMENT,
                         temperature_c,
                         (AMBIENT_TEMP_MIN_C + AMBIENT_TEMP_MAX_C) / 2.0f);

            ESP_LOGI(TAG,
                     "Temperature normalized: %.2f°C. HVAC turned off.",
                     temperature_c);
        }
    }

    // Check for temperature warnings (approaching thresholds)
    if (temperature_c < (AMBIENT_TEMP_MIN_C + TEMP_WARNING_THRESHOLD) &&
        temperature_c >= AMBIENT_TEMP_MIN_C) {
        // Approaching low temperature threshold
        int len = snprintf(
            sms_buf,
            sizeof(sms_buf),
            "[AMBIENT] Warning: Temperature approaching low threshold: %.2f°C",
            temperature_c);
        send_sms(WARNING, sms_buf, len);
    } else if (temperature_c > (AMBIENT_TEMP_MAX_C - TEMP_WARNING_THRESHOLD) &&
               temperature_c <= AMBIENT_TEMP_MAX_C) {
        // Approaching high temperature threshold
        int len = snprintf(
            sms_buf,
            sizeof(sms_buf),
            "[AMBIENT] Warning: Temperature approaching high threshold: %.2f°C",
            temperature_c);
        send_sms(WARNING, sms_buf, len);
    }
}

static void validate_humidity(float humidity_percent) {
    char sms_buf[MSG_LEN];
    char telemetry_buf[MSG_LEN];

    // Check humidity for dehumidifier control (requirement 35)
    if (humidity_percent > HUMIDITY_MAX_PERCENT) {
        if (!dehumidifier_active) {
            dehumidifier_active = true;

            // Send SMS alert for high humidity
            int len = snprintf(sms_buf,
                               sizeof(sms_buf),
                               "[AMBIENT] Humidity too high: %.2f%%. Consider "
                               "using dehumidifier.",
                               humidity_percent);
            send_sms(WARNING, sms_buf, len);

            // Send telemetry alert
            snprintf(telemetry_buf,
                     sizeof(telemetry_buf),
                     "High humidity: %.2f%%",
                     humidity_percent);
            send_telemetry(TELEMETRY_WARNING, telemetry_buf);
        }
    } else {
        if (dehumidifier_active) {
            dehumidifier_active = false;
            ESP_LOGI(TAG, "Humidity normalized: %.2f%%.", humidity_percent);
        }
    }
}

void amb_temp_hum_task(void* arguments) {
    bme680_values_float_t values;
    char telemetry_buf[MSG_LEN];

    bme680_init();

    // as long as sensor configuration isn't changed, duration is constant
    uint32_t duration = bme680_get_measurement_duration(sensor);

    while (1) {
        // trigger the sensor to start one TPHG measurement cycle
        if (bme680_force_measurement(sensor)) {
            // passive waiting until measurement results are available
            vTaskDelay(duration);

            // get the results and do something with them
            if (bme680_get_results_float(sensor, &values)) {
                ESP_LOGI(TAG,
                         "%.3f BME680 Sensor: %.2f °C, %.2f %%, %.2f hPa, %.2f "
                         "Ohm\n",
                         (double)esp_timer_get_time() * 1e-6,
                         values.temperature,
                         values.humidity,
                         values.pressure,
                         values.gas_resistance);

                // Send telemetry for ambient temperature
                snprintf(telemetry_buf,
                         sizeof(telemetry_buf),
                         "%.2f",
                         values.temperature);
                send_telemetry(AMBIENT_TEMPERATURE, telemetry_buf);

                // Send telemetry for humidity
                snprintf(telemetry_buf,
                         sizeof(telemetry_buf),
                         "%.2f",
                         values.humidity);
                send_telemetry(HUMIDITY, telemetry_buf);

                // Validate temperature and control HVAC
                validate_temperature(values.temperature, values.humidity);

                // Validate humidity
                validate_humidity(values.humidity);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(5000)); // Check every 5 seconds
    }
}
