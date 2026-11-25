#ifndef ENTITIES_H
#define ENTITIES_H
#include <stdint.h>

#define PRINTER_MAX_WIDTH 576
#define PRINTER_HEIGHT 40
uint16_t m_image_width_bits = PRINTER_MAX_WIDTH;
uint16_t m_image_height_bits = 40;

uint8_t m_image_width_bytes = PRINTER_MAX_WIDTH / 8;
uint8_t m_image_height_bytes = PRINTER_HEIGHT / 8;

enum Status {
  IDLE,
  RECEIVING,
  PRINTING,
  SUCCESS,
  ERROR,
};

#endif // !ENTITIES_H
