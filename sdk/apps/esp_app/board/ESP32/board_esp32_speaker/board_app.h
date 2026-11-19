#pragma once

#include "esp_err.h"

/* 板级硬件、服务和 UI 启动后调用一次，框架自动启动扫描和按键分发任务。
 * 实习生在 system/board_app.c 的 app_key_event 中使用 KRS/KRH 等事件。
 * 产品业务任务通过 speaker_service.h、lvgl_port.h 接收状态和操作并回传 UI。 */
esp_err_t app_start(void);
