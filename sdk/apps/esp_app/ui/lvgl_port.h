#pragma once

#include "speaker_ui.h"
#include "esp_err.h"

/* 在 board_init 后启动一次。LCD 关闭时返回 ESP_ERR_NOT_SUPPORTED。
 * 以下接口仅限任务上下文；页面和 LVGL 本身只由内部 UI 任务访问。 */
esp_err_t lvgl_port_start(void);

/* 完整复制状态到长度为 1 的邮箱，合并中间更新；字符串必须是有终止符的 UTF-8。
 * 音量、亮度是 0～100；未知值用对应 known/available 字段表达。
 * UI 不推测播放成功、音量变化或歌曲时间，始终显示业务回传值。 */
esp_err_t lvgl_port_update(const speaker_ui_state_t *state);

/* 框架按键分发任务投递按下和方向/音量连发；确认只投递按下，单双击由 UI 判定。
 * 同一个任务按发生顺序投递，满队列返回 ESP_ERR_NOT_FINISHED。 */
esp_err_t lvgl_port_input(speaker_ui_key_t key);
/* 框架分发使用消抖时刻；重试沿用原值，避免队列等待改变单双击间隔。
 * time_ms 与 esp_timer_get_time()/1000 使用相同的 32 位毫秒时钟。 */
esp_err_t lvgl_port_input_at(speaker_ui_key_t key, uint32_t time_ms);

/* 实习生从这里接收动作并调用业务接口；返回后动作由调用方持有。
 * timeout_ms：0 轮询，UINT32_MAX 一直等待；其他值向上取整至系统 tick。
 * 无消费者或队列已满时界面提示操作失败，不阻塞 UI 或蓝牙线程。 */
esp_err_t lvgl_port_wait_action(speaker_ui_action_t *action, uint32_t timeout_ms);
