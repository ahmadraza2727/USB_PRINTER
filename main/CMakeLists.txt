idf_component_register(
    SRCS "usb_printer.c" "class_driver.c" "uart_driver.c"
    INCLUDE_DIRS "./includes"
    REQUIRES driver log freertos usb esp_adc led_strip
)
