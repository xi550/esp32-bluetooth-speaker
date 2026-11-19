#pragma once

#include "esp_err.h"
#include <stdint.h>

esp_err_t lcd_hal_init(void);
/* 阻塞发送按传输字节序排列的 RGB565 缓冲区，高字节在前；右下坐标不包含端点。
 * 调用返回前，缓冲区由调用者持有且必须保持有效；仅允许单个界面任务调用。 */
esp_err_t lcd_hal_flush(int x0, int y0, int x1, int y1, const uint16_t *pixels);
