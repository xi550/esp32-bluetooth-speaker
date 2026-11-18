#pragma once
#include "sdkconfig.h"
#if !CONFIG_IDF_TARGET_ESP32
#error "Only classic ESP32 is supported"
#endif
#define BOARD_I2S_BCLK 26
#define BOARD_I2S_LRCLK 25
#define BOARD_I2S_DOUT 22
#define BOARD_LCD_SCLK 18
#define BOARD_LCD_MOSI 23
#define BOARD_LCD_CS 21
#define BOARD_LCD_DC 19
#define BOARD_LCD_RST 17
#define BOARD_BUTTON_COUNT 5
#define BOARD_BUTTON_PINS {13,14,27,32,33}
/* Shared stereo bus; two MAX98357A select left/right via hardware SD_MODE. */
