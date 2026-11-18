idf_component_register(SRCS "audio_output.c" "lcd_hal.c" "buttons.c"
 INCLUDE_DIRS "include" REQUIRES board esp_driver_i2s esp_driver_spi esp_lcd)
