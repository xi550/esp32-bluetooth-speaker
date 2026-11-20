#pragma once

#include <stdint.h>
#include "esp_err.h"

/* 主音量使用 cfg.h 中的 BOARD_MASTER_VOL_MAX 档位，0 为静音。
 * 服务启动后可在任务中调用，无需手机连接；内部串行处理换算与状态更新。
 * ESP_OK 表示本地音量已设置，手机同步异步进行；不写入 NVS。 */
esp_err_t master_vol_up(void);
esp_err_t master_vol_down(void);

/* 直接设置 0～最大档，越界返回 ESP_ERR_INVALID_ARG，不修改音量。
 * 加减接口按 BOARD_MASTER_VOL_STEP 调节，到边界自动停止、不回绕。 */
esp_err_t set_master_vol(int vol);
esp_err_t get_master_vol(uint8_t *vol);
