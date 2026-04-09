/*
 * Platform abstraction layer for BME680 driver for ESP-IDF
 * Based on esp-open-rtos to ESP-IDF compatibility layer
 */

#ifndef __BME680_PLATFORM_H__
#define __BME680_PLATFORM_H__

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include "driver/i2c.h"
#include "driver/spi_master.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

// I2C bus definitions for compatibility
#define I2C_BUS_0 0
#define I2C_BUS_1 1

// Map to ESP-IDF I2C bus numbers
static inline i2c_port_t bus_to_port(uint8_t bus) {
    return (bus == 0) ? I2C_NUM_0 : I2C_NUM_1;
}

// I2C slave read/write functions for ESP-IDF compatibility
static inline int i2c_slave_read(
    uint8_t bus, uint8_t addr, uint8_t* reg, uint8_t* data, uint16_t len) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, *reg, true);
    i2c_master_start(cmd); // repeated start
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_READ, true);
    if (len > 1) {
        i2c_master_read(cmd, data, len - 1, I2C_MASTER_ACK);
    }
    i2c_master_read_byte(cmd, data + len - 1, I2C_MASTER_NACK);
    i2c_master_stop(cmd);

    esp_err_t ret =
        i2c_master_cmd_begin(bus_to_port(bus), cmd, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd);

    return (ret == ESP_OK) ? 0 : -1;
}

static inline int i2c_slave_write(
    uint8_t bus, uint8_t addr, uint8_t* reg, uint8_t* data, uint16_t len) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, *reg, true);
    i2c_master_write(cmd, data, len, true);
    i2c_master_stop(cmd);

    esp_err_t ret =
        i2c_master_cmd_begin(bus_to_port(bus), cmd, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd);

    return (ret == ESP_OK) ? 0 : -1;
}

// SPI functions (not used in this project, but defined for compatibility)
static inline bool spi_device_init(uint8_t bus, uint8_t cs) {
    // SPI not supported in this implementation
    return false;
}

static inline bool spi_transfer_pf(
    uint8_t bus, uint8_t cs, uint8_t* mosi, uint8_t* miso, uint16_t len) {
    // SPI not supported in this implementation
    return false;
}

// Time function replacement for sdk_system_get_time
static inline uint32_t sdk_system_get_time(void) {
    return (uint32_t)(esp_timer_get_time() /
                      1000); // Convert microseconds to milliseconds
}

// FreeRTOS compatibility - already defined in FreeRTOS

#ifdef __cplusplus
}
#endif

#endif /* __BME680_PLATFORM_H__ */