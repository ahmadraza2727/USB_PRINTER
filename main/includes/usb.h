#ifndef USB_H
  #define USB_H
#include <stdint.h>

void send_data_to_printer(const uint8_t *image_data_buffer, uint32_t size);
void send_text_to_printer(const uint8_t *image_data_buffer, uint32_t size);






#endif // !USB_H
