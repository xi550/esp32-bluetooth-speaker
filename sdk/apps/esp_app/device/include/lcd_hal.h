#pragma once

#include "esp_err.h"
#include <stdint.h>

esp_err_t lcd_hal_init(void);
/* 背光百分比 0～100；0 关闭、100 常亮，PWM 为线性占空比，不写入 NVS。
 * 初始化后先保持关闭，由 UI 首帧完成后设置 BOARD_INITIAL_BRIGHTNESS。
 * get 返回最近一次成功提交的设定值，不代表测得的光学亮度。
 * 由同一个应用任务串行调用，不在中断中使用，不直接操作已占用的 LEDC 通道。 */
esp_err_t lcd_hal_set_brightness(uint8_t percent);
esp_err_t lcd_hal_get_brightness(uint8_t *percent);
/* 阻塞发送按传输字节序排列的 RGB565 缓冲区，高字节在前；右下坐标不包含端点。
 * 调用返回前，缓冲区由调用者持有且必须保持有效；仅允许单个界面任务调用。 */
esp_err_t lcd_hal_flush(int x0, int y0, int x1, int y1, const uint16_t *pixels);
