#ifndef SHARED_I2C_H
#define SHARED_I2C_H

#include "driver/i2c.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#define BABYMON_SENSOR_I2C_PORT I2C_NUM_0
#define BABYMON_SENSOR_I2C_SDA_PIN 21
#define BABYMON_SENSOR_I2C_SCL_PIN 22
#define BABYMON_SENSOR_I2C_FREQ_HZ 400000

esp_err_t babymon_sensor_i2c_init(void);
esp_err_t babymon_sensor_i2c_lock(TickType_t timeout_ticks);
void babymon_sensor_i2c_unlock(void);

#endif
