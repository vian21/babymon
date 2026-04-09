/**
 * mhz19.cpp
 * MH-Z19 – Non-Dispersive Infrared CO2 Sensor
 *
 * Protocol: 9-byte command / 9-byte response over UART 9600 8N1
 *
 * Read CO2 command:  FF 01 86 00 00 00 00 00 79
 * Response:          FF 86 HH LL TT SS 00 00 CS
 *   CO2  = (HH << 8) | LL   ppm
 *   CS   = checksum (see calc_checksum)
 *
 * Wiring:
 *   MH-Z19 TX → ESP32 GPIO 16  (UART2 RX)
 *   MH-Z19 RX → ESP32 GPIO 17  (UART2 TX)
 *   VCC → 5 V  (the MH-Z19 requires 5 V)
 *   GND → GND
 */

#include "main.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include "driver/uart.h"
#include "esp_log.h"
typedef struct {
    int co2_ppm;
    uint8_t status;
    uint32_t timestamp_ms;
} MHZ19_Data_t;

static const char* TAG = "MHZ19";
static const int UART_PORT = UART_NUM_2;
static const int TX_PIN = 17;
static const int RX_PIN = 16;
static const int BAUD = 9600;
static const int BUF_SIZE = 256;

// CO2 alert thresholds (ppm)
static const int CO2_WARN = 1000;       // stuffy indoor air
static const int CO2_ALERT = 2000;      // poor air quality
static const int CO2_MAX_VALID = 10000; // sensor range limit

static const TickType_t READ_PERIOD =
    pdMS_TO_TICKS(5000); // 5 s (sensor updates ~every 2 s)

// ── MH-Z19 commands ───────────────────────────────────────────
static const uint8_t CMD_READ_CO2[9] = {0xFF, 0x01, 0x86, 0, 0, 0, 0, 0, 0x79};
static const uint8_t CMD_ABC_OFF[9] = {
    0xFF, 0x01, 0x79, 0x00, 0, 0, 0, 0, 0x86};

// ── Checksum ──────────────────────────────────────────────────
static uint8_t calc_checksum(const uint8_t* packet) {
    uint8_t sum = 0;
    for (int i = 1; i <= 7; i++)
        sum += packet[i];
    return (~sum) + 1;
}

static bool read_co2_data(MHZ19_Data_t* data) {
    uint8_t response[9] = {0};

    uart_flush_input(UART_PORT);
    uart_write_bytes(UART_PORT, (const char*)CMD_READ_CO2, 9);

    int rx_len = uart_read_bytes(UART_PORT, response, 9, pdMS_TO_TICKS(1000));

    if (rx_len != 9) {
        ESP_LOGE(TAG, "Bad: rx_len=%d (expected 9)", rx_len);
        // Log what we did receive
        if (rx_len > 0) {
            ESP_LOGE(TAG, "Received bytes: ");
            for (int i = 0; i < rx_len; i++) {
                ESP_LOGE(TAG, "  [%d] = 0x%02x", i, response[i]);
            }
        }
        return false;
    }

    if (response[0] != 0xFF) {
        ESP_LOGE(TAG, "Bad: resp[0]=%02x (expected 0xFF)", response[0]);
        return false;
    }

    if (response[1] != 0x86) {
        ESP_LOGE(TAG, "Bad: resp[1]=%02x (expected 0x86)", response[1]);
        return false;
    }

    uint8_t cs_calc = calc_checksum(response);
    if (response[8] != cs_calc) {
        ESP_LOGE(
            TAG, "Bad checksum: cs=%02x vs calc=%02x", response[8], cs_calc);
        return false;
    }

    data->co2_ppm = (response[2] << 8) | response[3];
    data->status = response[5];
    data->timestamp_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;

    ESP_LOGI(TAG,
             "CO2 reading: %d ppm, status: 0x%02x",
             data->co2_ppm,
             data->status);
    return true;
}

static bool verify_co2_value(int co2_ppm) {
    return (co2_ppm > 0 && co2_ppm <= CO2_MAX_VALID);
}

// ── Initialisation ────────────────────────────────────────────
void MHZ19_Init(void) {
    uart_config_t cfg = {};
    cfg.baud_rate = BAUD;
    cfg.data_bits = UART_DATA_8_BITS;
    cfg.parity = UART_PARITY_DISABLE;
    cfg.stop_bits = UART_STOP_BITS_1;
    cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    cfg.source_clk = UART_SCLK_APB;

    uart_param_config(UART_PORT, &cfg);
    uart_set_pin(
        UART_PORT, TX_PIN, RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(UART_PORT, BUF_SIZE * 2, 0, 0, NULL, 0);

    // Disable ABC (Automatic Baseline Correction) for indoor use
    ESP_LOGI(TAG, "Sending ABC OFF command...");
    uart_write_bytes(UART_PORT, (const char*)CMD_ABC_OFF, 9);
    vTaskDelay(pdMS_TO_TICKS(100)); // Wait for command to be sent

    // Allow sensor warm-up (3 min recommended; at minimum flush the pipe)
    ESP_LOGI(TAG, "MH-Z19 warming up – wait 3 min for accurate readings.");
    ESP_LOGI(TAG,
             "MH-Z19 initialised (UART%d, TX=%d, RX=%d)",
             UART_PORT,
             TX_PIN,
             RX_PIN);

    // Clear any leftover data in UART buffer
    uint8_t dummy[256];
    int flushed =
        uart_read_bytes(UART_PORT, dummy, sizeof(dummy), pdMS_TO_TICKS(100));
    if (flushed > 0) {
        ESP_LOGI(TAG, "Flushed %d bytes from UART buffer", flushed);
    }
}

// ── Monitor task ──────────────────────────────────────────────
void task_mhz19_monitor(void* pvParameters) {
    (void)pvParameters;

    char buf[80];

    MHZ19_Init();

    while (true) {
        MHZ19_Data_t data;
        if (!read_co2_data(&data)) {
            ESP_LOGE(TAG, "Bad response from sensor.");
            vTaskDelay(READ_PERIOD);
            continue;
        }

        if (!verify_co2_value(data.co2_ppm)) {
            ESP_LOGW(TAG,
                     "[%lu ms] Invalid CO2 value: %d ppm",
                     data.timestamp_ms,
                     data.co2_ppm);
            vTaskDelay(READ_PERIOD);
            continue;
        }

        if (data.co2_ppm >= CO2_ALERT) {
            int len = snprintf(
                buf,
                sizeof(buf),
                "[MHZ19] CRITICAL CO2: %d ppm - ventilate immediately!",
                data.co2_ppm);
            send_sms(ALARM, buf, len);
        } else if (data.co2_ppm >= CO2_WARN) {
            int len =
                snprintf(buf,
                         sizeof(buf),
                         "[MHZ19] HIGH CO2: %d ppm - consider ventilation.",
                         data.co2_ppm);
            send_sms(WARNING, buf, len);
        }

        vTaskDelay(READ_PERIOD);
    }
}
