#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/idf_additions.h"
#include "usb.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define ESP_CAM_BAUD_RATE 1000000
#define CAM_UART          UART_NUM_1
#define CAM_TX_PIN        GPIO_NUM_17
#define CAM_RX_PIN        GPIO_NUM_18
#define MAX_CHUNK         7200
#define CAM_ERR_SENTINEL  0xFFFF   

void cam_uart_task(void *args);
extern bool is_printer_ready(void); // Import the check function

static const char *TAG = "cam_uart";

#define RX_BUF   (1024 * 16)
#define TX_BUF   (1024 * 2)
#define ACK_BYTES "ack"
#define ACK_LEN   3

static volatile bool cam_synced = false;
static volatile bool is_printing = false;

bool is_cam_synced(void) { return cam_synced; }
bool get_is_printing(void) { return is_printing; }

static bool uart_read_exactly(uint8_t *dst, size_t n, uint32_t timeout_ms) {
    size_t received = 0;
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
    while (received < n) {
        int got = uart_read_bytes(CAM_UART, dst + received, n - received, pdMS_TO_TICKS(50));
        if (got > 0) received += got;
        if (xTaskGetTickCount() > deadline) return false;
    }
    return true;
}

static void send_ack(void) {
    uart_write_bytes(CAM_UART, ACK_BYTES, ACK_LEN);
}

static void do_sync(void) {
    ESP_LOGI(TAG, "Starting sync...");
    cam_synced = false;

    uint8_t discard[64];
    while (uart_read_bytes(CAM_UART, discard, sizeof(discard), pdMS_TO_TICKS(10)) > 0) {}

    const char *target = "ACK";
    int idx = 0;
    TickType_t last_send = 0;

    while (true) {
        if (xTaskGetTickCount() - last_send >= pdMS_TO_TICKS(100)) {
            uart_write_bytes(CAM_UART, "SYNC", 4);
            last_send = xTaskGetTickCount();
        }
        uint8_t c;
        if (uart_read_bytes(CAM_UART, &c, 1, pdMS_TO_TICKS(10)) == 1) {
            if (c == (uint8_t)target[idx]) {
                idx++;
                if (idx == 3) {
                    ESP_LOGI(TAG, "Sync complete");
                    uint8_t flush_buf[32];
                    vTaskDelay(pdMS_TO_TICKS(20));
                    while (uart_read_bytes(CAM_UART, flush_buf, sizeof(flush_buf), pdMS_TO_TICKS(10)) > 0) {}
                    
                    cam_synced = true; 
                    return;
                }
            } else {
                idx = (c == (uint8_t)target[0]) ? 1 : 0;
            }
        }
    }
}

static void request_capture(void) {
    uint8_t flush_buf[64];
    while (uart_read_bytes(CAM_UART, flush_buf, sizeof(flush_buf), pdMS_TO_TICKS(5)) > 0) {}

    uart_write_bytes(CAM_UART, "SEN", 3);
    vTaskDelay(pdMS_TO_TICKS(10));
    uart_write_bytes(CAM_UART, "BEGIN", 5);
    ESP_LOGI(TAG, "Capture requested");
}

static bool receive_and_print_image(void) {
    uint16_t hdr_len = 0;
    if (!uart_read_exactly((uint8_t *)&hdr_len, 2, 8000)) return false;
    if (hdr_len == CAM_ERR_SENTINEL || hdr_len != 4) return false;

    uint16_t dims[2] = {0};
    if (!uart_read_exactly((uint8_t *)dims, 4, 2000)) return false;
    send_ack();

    uint16_t w = dims[0], h = dims[1];
    if (w == 0 || h == 0 || w > 4096 || h > 4096) return false;

    uint16_t width_bytes = w / 8;
    uint8_t esc_header[10] = {0x1B, 0x40, 0x1D, 0x76, 0x30, 0x00, width_bytes & 0xFF, (width_bytes >> 8) & 0xFF, h & 0xFF, (h >> 8) & 0xFF};

    uint8_t *chunk_buf = (uint8_t *)malloc(MAX_CHUNK + sizeof(esc_header));
    if (!chunk_buf) return false;

    bool first_chunk = true;
    bool printer_alive = true;

    while (true) {
        uint16_t chunk_len = 0;
        if (!uart_read_exactly((uint8_t *)&chunk_len, 2, 5000)) {
            free(chunk_buf); return false;
        }

        if (chunk_len == 0) {  
            send_ack(); break;
        }

        if (chunk_len > MAX_CHUNK) {
            free(chunk_buf); return false;
        }

        uint32_t read_timeout = 3000 + (chunk_len / 100);

        if (first_chunk) {
            if (!uart_read_exactly(chunk_buf + sizeof(esc_header), chunk_len, read_timeout)) {
                free(chunk_buf); return false;
            }
            memcpy(chunk_buf, esc_header, sizeof(esc_header));
            send_ack();
            
            if (printer_alive) {
                send_data_to_printer(chunk_buf, (uint32_t)(sizeof(esc_header) + chunk_len));
            }
            first_chunk = false;
        } else {
            if (!uart_read_exactly(chunk_buf, chunk_len, read_timeout)) {
                free(chunk_buf); return false;
            }
            send_ack();
            
            // Re-evaluate if the printer is still plugged in
            printer_alive = is_printer_ready();
            
            if (printer_alive) {
                send_data_to_printer(chunk_buf, (uint32_t)chunk_len);
            }
        }
    }

    free(chunk_buf);
    
    if (printer_alive) {
        uint8_t feed[] = {0x1B, 0x64, 4};
        send_data_to_printer(feed, sizeof(feed));
        ESP_LOGI(TAG, "Print complete!");
    } else {
        ESP_LOGW(TAG, "Print aborted due to USB disconnect.");
    }
    
    return true;
}

void cam_uart_task(void *args) {
    uart_config_t cfg = {
        .baud_rate  = ESP_CAM_BAUD_RATE,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(CAM_UART, RX_BUF, TX_BUF, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(CAM_UART, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(CAM_UART, CAM_TX_PIN, CAM_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    do_sync();

    while (true) {
        uint32_t received_value = 0;
        xTaskNotifyWait(0, 0xFFFFFFFF, &received_value, portMAX_DELAY);

        if (received_value & 1) {
            is_printing = true; 
            
            request_capture();
            if (!receive_and_print_image()) {
                ESP_LOGE(TAG, "Image receive/print failed");
                vTaskDelay(pdMS_TO_TICKS(300));
            }
            
            is_printing = false; 
        }
    }
}
