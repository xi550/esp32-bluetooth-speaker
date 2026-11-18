#pragma once
#include "esp_err.h"
#include <stdint.h>
esp_err_t lcd_hal_init(void);
/* Blocking RGB565 wire-order buffer, high byte first. End-exclusive bounds.
 * Caller owns buffer until return; single UI task only. */
esp_err_t lcd_hal_flush(int x0,int y0,int x1,int y1,const uint16_t *pixels);
