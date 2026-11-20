#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum
{
    SPEAKER_DISCONNECTED,
    SPEAKER_CONNECTING,
    SPEAKER_CONNECTED,
    SPEAKER_DISCONNECTING,
} speaker_connection_t;

typedef enum
{
    SPEAKER_PLAYBACK_UNKNOWN,
    SPEAKER_PLAYBACK_STOPPED,
    SPEAKER_PLAYBACK_PLAYING,
    SPEAKER_PLAYBACK_PAUSED,
    SPEAKER_PLAYBACK_FORWARD_SEEK,
    SPEAKER_PLAYBACK_REVERSE_SEEK,
    SPEAKER_PLAYBACK_ERROR,
} speaker_playback_t;

typedef enum
{
    SPEAKER_CONTROL_PLAY,
    SPEAKER_CONTROL_PAUSE,
    SPEAKER_CONTROL_PLAY_PAUSE,
    SPEAKER_CONTROL_PREVIOUS,
    SPEAKER_CONTROL_NEXT,
} speaker_control_t;

/* 按值复制的状态快照，不包含借用指针或 SDK 类型；应用使用主音量档位和百分比。 */
typedef struct
{
    speaker_connection_t connection;
    speaker_playback_t playback;
    bool avrcp_connected;         /* AVRCP CT 已连接，可发送播放控制命令 */
    bool volume_connected;        /* AVRCP TG 已连接，绝对音量同步仍需手机支持 */
    bool streaming;               /* A2DP 音频流状态，与播放器状态分别维护 */
    uint32_t sample_rate_hz;      /* 协商出支持的音频格式前为 0 */
    uint8_t volume;               /* 底层协议值 0～127，业务无需换算。 */
    uint8_t master_vol;           /* 当前主音量档位，0～BOARD_MASTER_VOL_MAX。 */
    uint8_t volume_percent;       /* 已换算的显示百分比，应用直接赋给 UI。 */
    esp_err_t volume_sync_result; /* 最近一次 TG 音量通知的提交结果 */
    bool control_busy;
    esp_err_t control_result; /* 最近完成的控制结果：应答、超时或发送错误 */
} speaker_state_t;

/* 仅限任务上下文调用，不可在中断中调用；在板级初始化后启动一次，启动错误终止运行。 */
esp_err_t speaker_service_start(void);
esp_err_t speaker_service_get_state(speaker_state_t *state);

/* 由一个应用任务接收状态变化，再向界面分发快照。
 * 第一次接收可取得当前快照；长度为 1 的邮箱会合并中间变化，
 * 处理较慢时只取得最新状态，不保留每次变化的历史。
 * timeout_ms：0 表示轮询，UINT32_MAX 表示一直等待；数据完整复制到调用者。 */
esp_err_t speaker_service_wait_state(speaker_state_t *state, uint32_t timeout_ms);

/* 非阻塞提交；ESP_OK 只表示请求已接收，不表示手机已执行播放。
 * 同时只处理一个控制请求；繁忙返回 ESP_ERR_NOT_FINISHED，未连接返回
 * ESP_ERR_INVALID_STATE。通过 control_busy/control_result 查看完成结果。
 * PLAY_PAUSE 要求播放状态已知；实际播放状态以手机上报事件为准。 */
esp_err_t speaker_service_control(speaker_control_t control);

/* 底层协议音量接口，范围 0～127；普通应用使用 master_volume.h 的档位接口。
 * 无手机连接时也可设置本地音量，不写入持久化存储。
 * 手机已订阅时，本地变化发送一次 AVRCP TG 通知；手机更新音量时不回发通知。
 * ESP_OK 表示本地增益已设置，协议同步异步进行。
 * 通知提交失败会重试，结果见 volume_sync_result。 */
esp_err_t speaker_service_set_volume(uint8_t volume);
