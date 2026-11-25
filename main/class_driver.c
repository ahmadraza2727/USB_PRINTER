#include "esp_log.h"
#include "freertos/idf_additions.h"
#include "usb.h"
#include "usb/usb_host.h"
#include <stdint.h>
#include <string.h>

static const char *TAG_USB = "USB host lib";

usb_transfer_t *transfer;
static SemaphoreHandle_t transfer_done_sem = NULL;
static SemaphoreHandle_t device_ready_sem  = NULL; 

bool is_printer_ready(void) {
    if (device_ready_sem != NULL && xSemaphoreTake(device_ready_sem, 0) == pdTRUE) {
        xSemaphoreGive(device_ready_sem); 
        return true;
    }
    return false;
}

void send_data_to_printer(const uint8_t *data, uint32_t size) {
  if (!transfer_done_sem || !device_ready_sem) return;

  if (xSemaphoreTake(device_ready_sem, pdMS_TO_TICKS(5000)) != pdTRUE) {
    ESP_LOGE(TAG_USB, "Device not ready after 5s");
    return;
  }
  xSemaphoreGive(device_ready_sem); 

  uint32_t offset = 0;
  while (offset < size) {
    uint32_t chunk = size - offset;
    
    // ==========================================================
    // THE GOLDILOCKS FIX: 576 Bytes (Exactly 8 Rows of Pixels)
    // 72 bytes per row * 8 = 576 bytes.
    // This perfectly aligns with the printer's physical thermal head,
    // preventing memory fragmentation, stalling, and gibberish.
    // ==========================================================
    if (chunk > 576) chunk = 576; 

    if (xSemaphoreTake(transfer_done_sem, pdMS_TO_TICKS(5000)) != pdTRUE) {
      ESP_LOGE(TAG_USB, "Transfer timeout — printer stalled or out of paper");
      return;
    }

    memcpy(transfer->data_buffer, data + offset, chunk);
    transfer->num_bytes = chunk;
    esp_err_t err = usb_host_transfer_submit(transfer);
    
    if (err != ESP_OK) {
      ESP_LOGE(TAG_USB, "Transfer submit failed: %s", esp_err_to_name(err));
      xSemaphoreGive(transfer_done_sem); 
      return;
    }
    offset += chunk;
    
    // 20ms Delay: Fast enough to avoid the "gibberish" timeout, 
    // slow enough to prevent the 4KB buffer overflow.
    vTaskDelay(pdMS_TO_TICKS(20)); 
  }
}

#define CLASS_DRIVER_ACTION_OPEN_DEV  0x01
#define CLASS_DRIVER_ACTION_TRANSFER  0x02
#define CLASS_DRIVER_ACTION_CLOSE_DEV 0x04

struct class_driver_control {
  uint32_t actions;
  uint8_t dev_addr;
  usb_host_client_handle_t client_hdl;
  usb_device_handle_t dev_hdl;
};

static void client_event_cb(const usb_host_client_event_msg_t *event_msg, void *arg) {
  struct class_driver_control *class_driver_obj = (struct class_driver_control *)arg;
  switch (event_msg->event) {
  case USB_HOST_CLIENT_EVENT_NEW_DEV:
    class_driver_obj->actions |= CLASS_DRIVER_ACTION_OPEN_DEV;
    class_driver_obj->dev_addr = event_msg->new_dev.address;
    break;
  case USB_HOST_CLIENT_EVENT_DEV_GONE:
    class_driver_obj->actions |= CLASS_DRIVER_ACTION_CLOSE_DEV;
    break;
  default:
    break;
  }
}

static void transfer_cb(usb_transfer_t *t) {
  if (t->status != USB_TRANSFER_STATUS_COMPLETED) {
    ESP_LOGE(TAG_USB, "Transfer failed with status %d", t->status);
  }
  BaseType_t higher_task_woken = pdFALSE;
  xSemaphoreGiveFromISR(transfer_done_sem, &higher_task_woken);
  if (higher_task_woken) portYIELD_FROM_ISR();
}

void class_driver_task(void *arg) {
  struct class_driver_control class_driver_obj = {0};

  transfer_done_sem = xSemaphoreCreateBinary();
  xSemaphoreGive(transfer_done_sem);
  device_ready_sem = xSemaphoreCreateBinary();

  usb_host_client_config_t client_config = {
      .is_synchronous    = false,
      .max_num_event_msg = 5,
      .async = {
          .client_event_callback = client_event_cb,
          .callback_arg          = &class_driver_obj,
      }
  };
  usb_host_client_register(&client_config, &class_driver_obj.client_hdl);
  
  usb_host_transfer_alloc(8192, 0, &transfer);

  while (1) {
    usb_host_client_handle_events(class_driver_obj.client_hdl, portMAX_DELAY);

    if (class_driver_obj.actions & CLASS_DRIVER_ACTION_OPEN_DEV) {
      class_driver_obj.actions &= ~CLASS_DRIVER_ACTION_OPEN_DEV;

      esp_err_t err = usb_host_device_open(class_driver_obj.client_hdl, class_driver_obj.dev_addr, &class_driver_obj.dev_hdl);
      if (err != ESP_OK) continue;

      err = usb_host_interface_claim(class_driver_obj.client_hdl, class_driver_obj.dev_hdl, 0, 0);
      if (err != ESP_OK) {
          usb_host_device_close(class_driver_obj.client_hdl, class_driver_obj.dev_hdl);
          class_driver_obj.dev_hdl = NULL;
          continue;
      }

      vTaskDelay(pdMS_TO_TICKS(500));

      uint8_t ep_addr = 0x03; 
      const usb_config_desc_t *config_desc;
      if (usb_host_get_active_config_descriptor(class_driver_obj.dev_hdl, &config_desc) == ESP_OK) {
          const uint8_t *p = (const uint8_t *)config_desc;
          uint32_t offset = 0;
          while (offset < config_desc->wTotalLength) {
              uint8_t len = p[offset];
              uint8_t type = p[offset + 1];
              if (type == USB_W_VALUE_DT_ENDPOINT) {
                  const usb_ep_desc_t *ep = (const usb_ep_desc_t *)&p[offset];
                  if ((ep->bmAttributes & USB_BM_ATTRIBUTES_XFERTYPE_MASK) == USB_BM_ATTRIBUTES_XFER_BULK) {
                      if ((ep->bEndpointAddress & USB_B_ENDPOINT_ADDRESS_EP_DIR_MASK) == 0) { 
                          ep_addr = ep->bEndpointAddress;
                          break;
                      }
                  }
              }
              offset += len;
              if (len == 0) break; 
          }
      }
      
      transfer->device_handle    = class_driver_obj.dev_hdl;
      transfer->bEndpointAddress = ep_addr;
      transfer->callback         = transfer_cb;
      transfer->context          = (void *)&class_driver_obj;

      uint8_t init[] = {0x1B, 0x40}; 
      memcpy(transfer->data_buffer, init, sizeof(init));
      transfer->num_bytes = sizeof(init);

      xSemaphoreGive(transfer_done_sem);
      xSemaphoreTake(transfer_done_sem, 0);

      err = usb_host_transfer_submit(transfer);
      if (err != ESP_OK) {
        ESP_LOGE(TAG_USB, "Init transfer failed: %s", esp_err_to_name(err));
        xSemaphoreGive(transfer_done_sem);
        usb_host_interface_release(class_driver_obj.client_hdl, class_driver_obj.dev_hdl, 0);
        usb_host_device_close(class_driver_obj.client_hdl, class_driver_obj.dev_hdl);
        class_driver_obj.dev_hdl = NULL;
        continue;
      }

      xSemaphoreGive(device_ready_sem);
      ESP_LOGI(TAG_USB, "Printer ready");
    }

    if (class_driver_obj.actions & CLASS_DRIVER_ACTION_CLOSE_DEV) {
      class_driver_obj.actions &= ~CLASS_DRIVER_ACTION_CLOSE_DEV;
      
      xSemaphoreTake(device_ready_sem, 0); 
      ESP_LOGW(TAG_USB, "Printer Unplugged. Terminating handles safely.");

      if (class_driver_obj.dev_hdl != NULL) {
          usb_host_interface_release(class_driver_obj.client_hdl, class_driver_obj.dev_hdl, 0);
          usb_host_device_close(class_driver_obj.client_hdl, class_driver_obj.dev_hdl);
          class_driver_obj.dev_hdl = NULL; 
      }
    }
  }
}

void usb_host_lib_task(void *arg) {
  ESP_LOGI(TAG_USB, "Installing USB Host Library");
  usb_host_config_t host_config = {
      .skip_phy_setup = false,
      .intr_flags     = ESP_INTR_FLAG_LEVEL1,
      .peripheral_map = BIT0,
  };
  ESP_ERROR_CHECK(usb_host_install(&host_config));
  
  xTaskNotifyGive(arg);

  bool has_clients = true;
  bool has_devices = false;
  while (has_clients) {
    uint32_t event_flags;
    ESP_ERROR_CHECK(usb_host_lib_handle_events(portMAX_DELAY, &event_flags));
    if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
      if (ESP_OK == usb_host_device_free_all()) {
        has_clients = false;
      } else {
        has_devices = true;
      }
    }
    if (has_devices && event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
      has_clients = false;
    }
  }
  ESP_ERROR_CHECK(usb_host_uninstall());
  vTaskSuspend(NULL);
}
