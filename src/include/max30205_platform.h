/**
 * MAX30205 Platform Abstraction Layer for ESP32/FreeRTOS
 *
 * Provides ESP32-compatible implementations of the I2C functions
 * required by the MAX30205 driver.
 */

#ifndef MAX30205_PLATFORM_H
#define MAX30205_PLATFORM_H

#include <stdarg.h>
#include <stdint.h>
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Pin configuration from docs/pins.md
#define MAX30205_I2C_PORT I2C_NUM_0
#define MAX30205_SDA_PIN 21
#define MAX30205_SCL_PIN 22
#define MAX30205_I2C_FREQ_HZ 400000 // 400 kHz fast-mode (from body_temp.c)

// Default I2C address (A0, A1, A2 connected to GND)
#define MAX30205_DEFAULT_ADDRESS 0x48

// Debug tag
static const char* MAX30205_TAG = "MAX30205";

/**
 * @brief Initialize I2C bus for MAX30205
 * @return 0 on success, non-zero on error
 */
static uint8_t max30205_i2c_init(void) {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = MAX30205_SDA_PIN,
        .scl_io_num = MAX30205_SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = MAX30205_I2C_FREQ_HZ,
    };

    esp_err_t err = i2c_param_config(MAX30205_I2C_PORT, &conf);
    if (err != ESP_OK) {
        ESP_LOGE(
            MAX30205_TAG, "I2C param config failed: %s", esp_err_to_name(err));
        return 1;
    }

    err = i2c_driver_install(MAX30205_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);
    if (err == ESP_ERR_INVALID_STATE) {
        ESP_LOGI(MAX30205_TAG, "I2C driver already installed on port %d", MAX30205_I2C_PORT);
        return 0;
    }

    if (err != ESP_OK) {
        ESP_LOGE(MAX30205_TAG,
                 "I2C driver install failed: %s",
                 esp_err_to_name(err));
        return 1;
    }

    ESP_LOGI(MAX30205_TAG, "I2C initialized successfully");
    return 0;
}

/**
 * @brief Deinitialize I2C bus
 * @return 0 on success, non-zero on error
 */
static uint8_t max30205_i2c_deinit(void) {
    // The MAX30205 shares the bus with other sensors in this project.
    // Leave the shared I2C driver installed instead of tearing it down.
    return 0;
}

/**
 * @brief Read data from I2C device
 * @param addr I2C device address (7-bit, left-shifted by 1 in driver)
 * @param reg Register address to read from
 * @param buf Buffer to store read data
 * @param len Number of bytes to read
 * @return 0 on success, non-zero on error
 */
static uint8_t
max30205_i2c_read(uint8_t addr, uint8_t reg, uint8_t* buf, uint16_t len) {
    // The driver passes address already shifted left by 1
    // Convert to ESP32 format (7-bit address)
    uint8_t device_addr = addr >> 1;

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (device_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd); // Repeated start
    i2c_master_write_byte(cmd, (device_addr << 1) | I2C_MASTER_READ, true);

    if (len > 1) {
        i2c_master_read(cmd, buf, len - 1, I2C_MASTER_ACK);
    }
    i2c_master_read(cmd, buf + len - 1, 1, I2C_MASTER_LAST_NACK);

    i2c_master_stop(cmd);
    esp_err_t err =
        i2c_master_cmd_begin(MAX30205_I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);

    if (err != ESP_OK) {
        ESP_LOGE(MAX30205_TAG,
                 "I2C read failed: %s (addr=0x%02X, reg=0x%02X)",
                 esp_err_to_name(err),
                 device_addr,
                 reg);
        return 1;
    }

    return 0;
}

/**
 * @brief Write data to I2C device
 * @param addr I2C device address (7-bit, left-shifted by 1 in driver)
 * @param reg Register address to write to
 * @param buf Buffer containing data to write
 * @param len Number of bytes to write
 * @return 0 on success, non-zero on error
 */
static uint8_t
max30205_i2c_write(uint8_t addr, uint8_t reg, uint8_t* buf, uint16_t len) {
    // The driver passes address already shifted left by 1
    // Convert to ESP32 format (7-bit address)
    uint8_t device_addr = addr >> 1;

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (device_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);

    if (len > 0) {
        i2c_master_write(cmd, buf, len, true);
    }

    i2c_master_stop(cmd);
    esp_err_t err =
        i2c_master_cmd_begin(MAX30205_I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);

    if (err != ESP_OK) {
        ESP_LOGE(MAX30205_TAG,
                 "I2C write failed: %s (addr=0x%02X, reg=0x%02X)",
                 esp_err_to_name(err),
                 device_addr,
                 reg);
        return 1;
    }

    return 0;
}

static bool max30205_i2c_probe(uint8_t addr_7bit) {
    esp_err_t err = i2c_master_write_to_device(
        MAX30205_I2C_PORT, addr_7bit, NULL, 0, pdMS_TO_TICKS(100));
    return err == ESP_OK;
}

/**
 * @brief Delay function for FreeRTOS
 * @param ms Milliseconds to delay
 */
static void max30205_delay_ms(uint32_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}

/**
 * @brief Debug print function using ESP32 logging
 * @param fmt Format string
 * @param ... Variable arguments
 */
static void max30205_debug_print(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    esp_log_writev(ESP_LOG_INFO, MAX30205_TAG, fmt, args);
    va_end(args);
}

#endif // MAX30205_PLATFORM_H
