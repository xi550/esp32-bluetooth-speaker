#pragma once

#include <stdint.h>
#include "esp_err.h"

/* 键值低 8 位、状态高 8 位，沿用参考项目的 KRS/KRH 写法。 */
typedef enum
{
    RKEY_UP = 1,
    RKEY_DOWN,
    RKEY_CONFIRM,
    RKEY_VOL_UP,
    RKEY_VOL_DOWN,
} board_key_t;

#define KRS(key)  ((key) | 0x000u) /* 消抖后的按下，不等待松手。 */
#define KRU(key)  ((key) | 0x100u) /* 短按松手。 */
#define KRL(key)  ((key) | 0x200u) /* 达到长按门限，仅一次。 */
#define KRLU(key) ((key) | 0x300u) /* 长按松手。 */
#define KRH(key)  ((key) | 0x400u) /* 长按后的周期连发。 */

typedef struct
{
    uint16_t code;
    uint32_t time_ms; /* 消抖事件的单调时钟，32 位毫秒，允许回绕。 */
} board_key_event_t;

/* 在 buttons_init 后调用一次；创建扫描任务和事件队列，无业务操作。
 * 多键独立消抖；开机已按住的键也会在消抖后产生按下事件。 */
esp_err_t board_keys_start(void);

/* 单消费者、仅限任务上下文。timeout_ms 为 0 时轮询，UINT32_MAX 一直等待。
 * 满队列时优先省略连发事件；边沿仍无法入队时输出错误日志。
 * app_start 已创建默认消费者，不要另建任务抢读同一个队列。 */
esp_err_t board_keys_wait(board_key_event_t *event, uint32_t timeout_ms);
