#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "main.h"

#define AMBIENT_I2C_PORT I2C_NUM_0
#define AMBIENT_I2C_SDA_GPIO 21
#define AMBIENT_I2C_SCL_GPIO 22
#define AMBIENT_I2C_FREQ_HZ 100000

#define BME680_I2C_ADDR 0x76
#define BME680_CHIP_ID_REG 0xD0
#define BME680_CHIP_ID 0x61
#define BME680_RESET_REG 0xE0
#define BME680_CTRL_MEAS_REG 0x74
#define BME680_CONFIG_REG 0x75
#define BME680_STATUS_REG 0x1D
#define BME680_FIELD0_ADDR 0x1F

#define AMBIENT_SAMPLE_PERIOD_MS 5000
#define AMBIENT_TEMP_MIN_C 20.0f
#define AMBIENT_TEMP_MAX_C 22.2f

typedef struct {
    uint16_t par_t1;
    int16_t par_t2;
    int8_t par_t3;
    float t_fine;
} bme680_calib_t;

static const char* TAG = "ambient_temp";
static bool s_i2c_ready;
static bool s_bme_ready;
static bme680_calib_t s_calib;

static esp_err_t ambient_i2c_write(uint8_t reg, uint8_t value) {
    uint8_t payload[2] = {reg, value};
    return i2c_master_write_to_device(AMBIENT_I2C_PORT,
                                      BME680_I2C_ADDR,
                                      payload,
                                      sizeof(payload),
                                      pdMS_TO_TICKS(100));
}

static esp_err_t ambient_i2c_read(uint8_t reg, uint8_t* data, size_t len) {
    return i2c_master_write_read_device(AMBIENT_I2C_PORT,
                                        BME680_I2C_ADDR,
                                        &reg,
                                        sizeof(reg),
                                        data,
                                        len,
                                        pdMS_TO_TICKS(100));
}

static esp_err_t ambient_i2c_init(void) {
    if (s_i2c_ready) {
        return ESP_OK;
    }

    i2c_config_t config = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = AMBIENT_I2C_SDA_GPIO,
        .scl_io_num = AMBIENT_I2C_SCL_GPIO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = AMBIENT_I2C_FREQ_HZ,
    };

    ESP_RETURN_ON_ERROR(i2c_param_config(AMBIENT_I2C_PORT, &config),
                        TAG,
                        "Failed to configure ambient I2C bus");
    ESP_RETURN_ON_ERROR(i2c_driver_install(AMBIENT_I2C_PORT,
                                           config.mode,
                                           0,
                                           0,
                                           0),
                        TAG,
                        "Failed to install ambient I2C driver");

    s_i2c_ready = true;
    return ESP_OK;
}

static esp_err_t bme680_load_temperature_calibration(void) {
    uint8_t t1[2];
    uint8_t t2[2];

    ESP_RETURN_ON_ERROR(ambient_i2c_read(0xE9, t1, sizeof(t1)),
                        TAG,
                        "Failed to read BME680 par_t1");
    ESP_RETURN_ON_ERROR(ambient_i2c_read(0x8A, t2, sizeof(t2)),
                        TAG,
                        "Failed to read BME680 par_t2");
    ESP_RETURN_ON_ERROR(ambient_i2c_read(0x8C, (uint8_t*)&s_calib.par_t3, 1),
                        TAG,
                        "Failed to read BME680 par_t3");

    s_calib.par_t1 = (uint16_t)((t1[1] << 8) | t1[0]);
    s_calib.par_t2 = (int16_t)((t2[1] << 8) | t2[0]);

    return ESP_OK;
}

static esp_err_t bme680_init(void) {
    uint8_t chip_id = 0;

    ESP_RETURN_ON_ERROR(ambient_i2c_init(), TAG, "Ambient I2C init failed");
    ESP_RETURN_ON_ERROR(ambient_i2c_read(BME680_CHIP_ID_REG, &chip_id, 1),
                        TAG,
                        "Failed to read BME680 chip ID");

    if (chip_id != BME680_CHIP_ID) {
        ESP_LOGE(TAG, "Unexpected BME680 chip ID: 0x%02X", chip_id);
        return ESP_ERR_INVALID_RESPONSE;
    }

    ESP_RETURN_ON_ERROR(ambient_i2c_write(BME680_RESET_REG, 0xB6),
                        TAG,
                        "Failed to reset BME680");
    vTaskDelay(pdMS_TO_TICKS(10));

    ESP_RETURN_ON_ERROR(bme680_load_temperature_calibration(),
                        TAG,
                        "Failed to load BME680 temperature calibration");
    ESP_RETURN_ON_ERROR(ambient_i2c_write(BME680_CONFIG_REG, 0x08),
                        TAG,
                        "Failed to set BME680 filter");

    s_bme_ready = true;
    return ESP_OK;
}

static float bme680_compensate_temperature(uint32_t raw_temp) {
    float var1 = (((float)raw_temp / 16384.0f) - ((float)s_calib.par_t1 / 1024.0f)) *
                 (float)s_calib.par_t2;
    float var2 = (((float)raw_temp / 131072.0f) - ((float)s_calib.par_t1 / 8192.0f));
    var2 = (var2 * var2) * ((float)s_calib.par_t3 * 16.0f);

    s_calib.t_fine = var1 + var2;
    return s_calib.t_fine / 5120.0f;
}

static esp_err_t bme680_read_temperature(float* temperature_c) {
    uint8_t ctrl_meas = 0;
    uint8_t status = 0;
    uint8_t field_data[6];

    if (!temperature_c) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_bme_ready) {
        ESP_RETURN_ON_ERROR(bme680_init(), TAG, "BME680 init failed");
    }

    ctrl_meas = (0x03 << 5) | (0x03 << 2) | 0x01;
    ESP_RETURN_ON_ERROR(ambient_i2c_write(BME680_CTRL_MEAS_REG, ctrl_meas),
                        TAG,
                        "Failed to start forced BME680 conversion");

    for (int attempt = 0; attempt < 10; ++attempt) {
        ESP_RETURN_ON_ERROR(ambient_i2c_read(BME680_STATUS_REG, &status, 1),
                            TAG,
                            "Failed to read BME680 status");
        if (status & 0x80) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if ((status & 0x80) == 0) {
        return ESP_ERR_TIMEOUT;
    }

    ESP_RETURN_ON_ERROR(ambient_i2c_read(BME680_FIELD0_ADDR,
                                         field_data,
                                         sizeof(field_data)),
                        TAG,
                        "Failed to read BME680 temperature sample");

    uint32_t raw_temp = ((uint32_t)field_data[3] << 12) |
                        ((uint32_t)field_data[4] << 4) |
                        ((uint32_t)field_data[5] >> 4);

    *temperature_c = bme680_compensate_temperature(raw_temp);
    return ESP_OK;
}

static bool ambient_temperature_out_of_range(float temperature_c) {
    return temperature_c < AMBIENT_TEMP_MIN_C || temperature_c > AMBIENT_TEMP_MAX_C;
}

void ambient_temp_task(void* arguments) {
    ambient_task_args_t* args = (ambient_task_args_t*)arguments;

    for (;;) {
        float temperature_c = 0.0f;
        esp_err_t err = bme680_read_temperature(&temperature_c);

        if (err == ESP_OK) {
            if (task_args){
                task_args->value = temperature_c;
            }
            ESP_LOGI(TAG, "Ambient temperature: %.2f C", temperature_c);
            if (task_args) {
                ESP_LOGI(TAG,
                         "Ambient metric type=%d current=%.2f wanted=%.2f",
                         task_args->type,
                         task_args->value,
                         task_args->wanted_value);}

            if (ambient_temperature_out_of_range(temperature_c)) {
                control_hvac(temperature_c);
            }
        } else {
            ESP_LOGE(TAG,
                     "Ambient temperature read failed: %s",
                     esp_err_to_name(err));
        }

        vTaskDelay(pdMS_TO_TICKS(AMBIENT_SAMPLE_PERIOD_MS));
    }
}
