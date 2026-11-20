#pragma once

#include "esp_err.h"

/* 板级硬件、服务和 UI 启动后调用一次，框架自动启动扫描和按键分发任务。
 * 实习生在 system/board_app.c 的 app_key_event 中使用 KRS/KRH 等事件。
 * 音量键直接调用主音量接口；启用 LCD 后另启动状态回显与界面动作接收任务。 */
esp_err_t app_start(void);
