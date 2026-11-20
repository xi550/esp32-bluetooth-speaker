#pragma once

#include <stdbool.h>
#include <stdint.h>

/* 可移植的页面层：不包含 ESP-IDF、GPIO、蓝牙及任务类型。
 * 以下函数只由同一个 UI 任务调用；应用通过 lvgl_port.h 投递数据。 */
typedef enum
{
    SPEAKER_UI_KEY_UP,
    SPEAKER_UI_KEY_DOWN,
    SPEAKER_UI_KEY_CONFIRM,
    SPEAKER_UI_KEY_VOLUME_UP,
    SPEAKER_UI_KEY_VOLUME_DOWN,
} speaker_ui_key_t;

typedef enum
{
    SPEAKER_UI_PREVIOUS,
    SPEAKER_UI_PLAY_PAUSE,
    SPEAKER_UI_NEXT,
    SPEAKER_UI_BRIGHTNESS,
} speaker_ui_action_type_t;

typedef struct
{
    speaker_ui_action_type_t type;
    int delta; /* 亮度为百分比增量；其他动作固定为 0。 */
} speaker_ui_action_t;

typedef struct
{
    bool connected;
    bool connecting;
    bool playing;
    bool playback_known;
    bool control_available;
    bool control_busy;
    bool volume_known;
    uint8_t volume_percent;
    bool brightness_available;
    uint8_t brightness_percent;
    bool position_known;
    bool duration_known;
    uint32_t position_ms;
    uint32_t duration_ms;
    char title[128];
    char artist[96];
    char lyrics[3][160]; /* 上一行、当前行、下一行；空字符串表示无数据。 */
    char device_name[64];
    char firmware_version[32];
    char message[96]; /* 业务接入方提供的中文结果或错误提示；空字符串清除。 */
} speaker_ui_state_t;

typedef struct
{
    uint32_t double_click_ms;
    uint8_t brightness_step;
} speaker_ui_config_t;

/* 回调只投递动作，不执行耗时业务；队列已满等情况返回 false。 */
typedef bool (*speaker_ui_action_cb_t)(const speaker_ui_action_t *action, void *context);

bool speaker_ui_init(
    const speaker_ui_config_t *config, speaker_ui_action_cb_t callback, void *context);
bool speaker_ui_set_state(const speaker_ui_state_t *state, uint32_t now_ms);
bool speaker_ui_state_valid(const speaker_ui_state_t *state);
/* 音量键输入仅显示浮层，主音量由应用按键分支直接调节。 */
void speaker_ui_send_key(speaker_ui_key_t key, uint32_t now_ms);
void speaker_ui_tick(uint32_t now_ms);
