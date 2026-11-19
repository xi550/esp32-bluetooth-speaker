#pragma once

#include <stdbool.h>
#include "esp_err.h"

esp_err_t buttons_init(void);
/* 读取按配置有效电平判定的原始按键状态，序号为 0～4；不包含消抖或功能映射。 */
esp_err_t buttons_read(unsigned index, bool *pressed);
