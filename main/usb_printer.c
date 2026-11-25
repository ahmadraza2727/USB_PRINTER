#include "driver/gpio.h"
#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "hal/gpio_types.h"
#include "portmacro.h"
#include "usb/usb_host.h"
#include "led_strip.h"      
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// ================= HARDWARE PINS =================
#define CONFIG_GPIO_BUTTON_PIN 14
#define GPIO_BUTTON_PIN_SEL (1ULL << CONFIG_GPIO_BUTTON_PIN)

// STANDARD ESP32-S3 NEOPIXEL PIN
// If your board is generic, the RGB LED is almost always on Pin 48.
#define NEOPIXEL_PIN 48        

#define HOST_LIB_TASK_PRIORITY 2
#define CLASS_TASK_PRIORITY 3
#define ESP_INTR_FLAG_DEFAULT 0

static QueueHandle_t gpio_evt_queue = NULL;
static const char * TAG = "main task";

extern void class_driver_task(void *arg);
extern void usb_host_lib_task(void *arg);
void cam_uart_task(void *args);

extern bool is_printer_ready(void);
extern bool is_cam_synced(void);
extern bool get_is_printing(void);

// ================= NEOPIXEL SETUP =================
static led_strip_handle_t led_strip;

void init_neopixel(void) {
    // Configure the LED on the standard GPIO 48
    led_strip_config_t strip_config = {
        .strip_gpio_num = NEOPIXEL_PIN,
        .max_leds = 1, 
    };
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000, 
    };
    
    esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NeoPixel: %s", esp_err_to_name(err));
    } else {
        led_strip_clear(led_strip);
    }
}

void set_led_color(uint8_t r, uint8_t g, uint8_t b) {
    if (led_strip) {
        led_strip_set_pixel(led_strip, 0, r, g, b);
        led_strip_refresh(led_strip);
    }
}

static void IRAM_ATTR gpio_isr_handler(void *arg) {
  uint32_t gpio_num = (uint32_t)arg;
  xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
}

void configure_gpio(gpio_config_t* io_conf){
  io_conf->intr_type = GPIO_INTR_POSEDGE;
  io_conf->pin_bit_mask = GPIO_BUTTON_PIN_SEL;
  io_conf->mode = GPIO_MODE_INPUT;
  io_conf->pull_up_en = GPIO_PULLDOWN_ENABLE;
  gpio_config(io_conf);
  
  gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
  gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
  gpio_isr_handler_add(CONFIG_GPIO_BUTTON_PIN, gpio_isr_handler, (void *)CONFIG_GPIO_BUTTON_PIN);
}

void app_main(void) {
  ESP_LOGI(TAG, "System Booting...");

  init_neopixel();
  set_led_color(50, 0, 0); // Default RED (Configuring)

  gpio_config_t io_conf = {};
  configure_gpio(&io_conf);

  TaskHandle_t host_lib_task_hdl, class_driver_task_hdl, cam_uart_task_hdl;

  xTaskCreatePinnedToCore(usb_host_lib_task, "usb_host", 4096, xTaskGetCurrentTaskHandle(), HOST_LIB_TASK_PRIORITY, &host_lib_task_hdl, 0);
  ulTaskNotifyTake(false, 1000);

  xTaskCreatePinnedToCore(class_driver_task, "class", 5 * 1024, NULL, CLASS_TASK_PRIORITY, &class_driver_task_hdl, 0);
  vTaskDelay(10);

  xTaskCreatePinnedToCore(cam_uart_task, "uart_task", 8 * 1024, NULL, CLASS_TASK_PRIORITY, &cam_uart_task_hdl, 1);

  uint32_t io_num;
  TickType_t last_tick = 0;
  int current_led_state = -1; 

  while (1) {
    if (xQueueReceive(gpio_evt_queue, &io_num, pdMS_TO_TICKS(100))) {
      TickType_t current_tick = xTaskGetTickCount();
      uint32_t diff = (current_tick - last_tick) * portTICK_PERIOD_MS;
      last_tick = current_tick;
      
      if (diff > 800) {
        if (is_printer_ready() && is_cam_synced()) {
            xTaskNotify(cam_uart_task_hdl, 1, eSetValueWithOverwrite);
            ESP_LOGI(TAG, "Capture Started!");
        } else {
            ESP_LOGW(TAG, "Button pressed, but Printer or Camera NOT READY.");
        }
      }
    }

    // ================= LED STATE MACHINE =================
    int target_state = 0; // 0 = RED (Configuring / Not Ready)
    
    if (get_is_printing()) {
        target_state = 2; // 2 = BLUE (Printing)
    } else if (is_printer_ready() && is_cam_synced()) {
        target_state = 1; // 1 = GREEN (Ready for Button)
    }

    if (target_state != current_led_state) {
        current_led_state = target_state;
        if (current_led_state == 0) set_led_color(50, 0, 0);       // RED
        if (current_led_state == 1) set_led_color(0, 50, 0);       // GREEN
        if (current_led_state == 2) set_led_color(0, 0, 50);       // BLUE
    }
  }
}
