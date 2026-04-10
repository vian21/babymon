#include "shared_i2c.h"

#include <stdbool.h>

#include "esp_log.h"
#include "freertos/semphr.h"

static const char *TAG = "shared_i2c";
static bool s_i2c_initialized;

static StaticSemaphore_t s_init_mutex_buffer;
static StaticSemaphore_t s_bus_mutex_buffer;
static SemaphoreHandle_t s_init_mutex;
static SemaphoreHandle_t s_bus_mutex;
static portMUX_TYPE s_mutex_guard = portMUX_INITIALIZER_UNLOCKED;

static SemaphoreHandle_t shared_i2c_get_init_mutex(void)
{
    portENTER_CRITICAL(&s_mutex_guard);
    if (s_init_mutex == NULL) {
        s_init_mutex = xSemaphoreCreateMutexStatic(&s_init_mutex_buffer);
    }
    portEXIT_CRITICAL(&s_mutex_guard);

    return s_init_mutex;
}

static SemaphoreHandle_t shared_i2c_get_bus_mutex(void)
{
    portENTER_CRITICAL(&s_mutex_guard);
    if (s_bus_mutex == NULL) {
        s_bus_mutex = xSemaphoreCreateMutexStatic(&s_bus_mutex_buffer);
    }
    portEXIT_CRITICAL(&s_mutex_guard);

    return s_bus_mutex;
}

esp_err_t babymon_sensor_i2c_init(void)
{
    SemaphoreHandle_t init_mutex = shared_i2c_get_init_mutex();
    if (init_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    if (xSemaphoreTake(init_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (shared_i2c_get_bus_mutex() == NULL) {
        xSemaphoreGive(init_mutex);
        return ESP_ERR_NO_MEM;
    }

    if (s_i2c_initialized) {
        xSemaphoreGive(init_mutex);
        return ESP_OK;
    }

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = BABYMON_SENSOR_I2C_SDA_PIN,
        .scl_io_num = BABYMON_SENSOR_I2C_SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = BABYMON_SENSOR_I2C_FREQ_HZ,
    };

    esp_err_t err = i2c_param_config(BABYMON_SENSOR_I2C_PORT, &conf);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "I2C param config failed: %s", esp_err_to_name(err));
        xSemaphoreGive(init_mutex);
        return err;
    }

    err = i2c_driver_install(BABYMON_SENSOR_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "I2C driver install failed: %s", esp_err_to_name(err));
        xSemaphoreGive(init_mutex);
        return err;
    }

    s_i2c_initialized = true;
    ESP_LOGI(TAG,
             "Shared sensor I2C ready on port %d (SDA=%d SCL=%d)",
             BABYMON_SENSOR_I2C_PORT,
             BABYMON_SENSOR_I2C_SDA_PIN,
             BABYMON_SENSOR_I2C_SCL_PIN);

    xSemaphoreGive(init_mutex);
    return ESP_OK;
}

esp_err_t babymon_sensor_i2c_lock(TickType_t timeout_ticks)
{
    esp_err_t err = babymon_sensor_i2c_init();
    if (err != ESP_OK) {
        return err;
    }

    SemaphoreHandle_t bus_mutex = shared_i2c_get_bus_mutex();
    if (bus_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    if (xSemaphoreTake(bus_mutex, timeout_ticks) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

void babymon_sensor_i2c_unlock(void)
{
    SemaphoreHandle_t bus_mutex = shared_i2c_get_bus_mutex();
    if (bus_mutex != NULL) {
        xSemaphoreGive(bus_mutex);
    }
}
