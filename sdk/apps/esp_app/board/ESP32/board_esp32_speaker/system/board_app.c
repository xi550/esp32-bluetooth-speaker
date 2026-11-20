#include "board_app.h"
#include "board_keyevent.h"
#include "board_esp_proj_cfg.h"
#include "lvgl_port.h"
#include "speaker_service.h"
#include "master_volume.h"
#include "lcd_hal.h"
#include <stdio.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* 实习生在这里按事件编写业务；扫描、消抖、长按和连发由框架完成。
 * 音量键直接调用主音量接口；UI 输入仅显示浮层，不再生成音量动作。
 * 不要在这里等待业务完成；耗时操作交给业务任务，避免按键事件积压。 */
static void app_key_event(uint16_t event, uint32_t time_ms)
{
    esp_err_t error = ESP_OK;
    speaker_ui_key_t key;
    switch (event)
    {
    case KRS(RKEY_UP):
    case KRH(RKEY_UP):
        key = SPEAKER_UI_KEY_UP;
        break;
    case KRS(RKEY_DOWN):
    case KRH(RKEY_DOWN):
        key = SPEAKER_UI_KEY_DOWN;
        break;
    case KRS(RKEY_CONFIRM):
        /* 长按和松手不作为确认输入，防止误触单双击返回。 */
        key = SPEAKER_UI_KEY_CONFIRM;
        break;
    case KRS(RKEY_VOL_UP):
    case KRH(RKEY_VOL_UP):
        error = master_vol_up();
        key = SPEAKER_UI_KEY_VOLUME_UP;
        break;
    case KRS(RKEY_VOL_DOWN):
    case KRH(RKEY_VOL_DOWN):
        error = master_vol_down();
        key = SPEAKER_UI_KEY_VOLUME_DOWN;
        break;
    default:
        return;
    }
    if (error != ESP_OK)
    {
        ESP_LOGE("app", "Key action failed: %s", esp_err_to_name(error));
        return;
    }
    if (!BOARD_LCD_ENABLE)
    {
        return;
    }
    /* 音量已执行，UI 暂时忙时只重试显示输入，不重复调用主音量接口。 */
    do
    {
        error = lvgl_port_input_at(key, time_ms);
        if (error == ESP_ERR_NOT_FINISHED)
        {
            vTaskDelay(1);
        }
    } while (error == ESP_ERR_NOT_FINISHED);
    if (error != ESP_OK)
    {
        ESP_LOGE("app", "UI key input failed: %s", esp_err_to_name(error));
    }
}

static void key_dispatch_task(void *context)
{
    for (;;)
    {
        board_key_event_t event;
        if (board_keys_wait(&event, UINT32_MAX) == ESP_OK)
        {
            app_key_event(event.code, event.time_ms);
        }
    }
}

#if BOARD_LCD_ENABLE

/* 应用持有完整 UI 快照；从服务读取媒体及播放状态，歌词保留独立数据入口。
 * 后续业务也在这个快照合并数据，不要另建任务覆盖成半份状态。 */
static esp_err_t app_refresh_ui(speaker_ui_state_t *ui, esp_err_t action_result)
{
    speaker_state_t current;
    esp_err_t error = speaker_service_get_state(&current);
    if (error != ESP_OK)
    {
        return error;
    }
    ui->connected = current.connection == SPEAKER_CONNECTED;
    ui->connecting = current.connection == SPEAKER_CONNECTING;
    ui->playing = current.playback == SPEAKER_PLAYBACK_PLAYING;
    ui->playback_known = current.playback != SPEAKER_PLAYBACK_UNKNOWN;
    ui->control_available = current.avrcp_connected;
    ui->control_busy = current.control_busy;
    snprintf(ui->title, sizeof(ui->title), "%s", current.title);
    snprintf(ui->artist, sizeof(ui->artist), "%s", current.artist);
    ui->position_known = current.position_known;
    ui->duration_known = current.duration_known;
    ui->position_ms = current.position_ms;
    ui->duration_ms = current.duration_ms;
    ui->volume_known = true;
    ui->volume_percent = current.volume_percent;
    ui->brightness_available = lcd_hal_get_brightness(&ui->brightness_percent) == ESP_OK;
    if (action_result == ESP_ERR_NOT_SUPPORTED)
    {
        snprintf(ui->message, sizeof(ui->message), "示例尚未实现此操作");
    }
    else if (action_result != ESP_OK)
    {
        snprintf(ui->message, sizeof(ui->message), "操作失败：%d", (int)action_result);
    }
    else if (current.volume_sync_result != ESP_OK)
    {
        /* 本地音量成功和手机同步失败分别表达，不回滚本地增益。 */
        snprintf(ui->message, sizeof(ui->message), "本地音量已生效，手机同步失败");
    }
    else
    {
        ui->message[0] = '\0';
    }
    return lvgl_port_update(ui);
}

static void app_business_task(void *context)
{
    speaker_ui_state_t ui = {0};
    snprintf(ui.device_name, sizeof(ui.device_name), "%s", BOARD_BT_NAME);
    snprintf(ui.firmware_version, sizeof(ui.firmware_version), "%s", BOARD_FIRMWARE_VERSION);
    bool refresh = true;
    for (;;)
    {
        speaker_ui_action_t action;
        esp_err_t result = ESP_OK;
        /* 一个任务消费动作和服务状态；动作等待最多 20ms，再检查服务更新。 */
        if (lvgl_port_wait_action(&action, 20) == ESP_OK)
        {
            /* 音量键已直接处理。实习生在此按 action.type 接入播放、切歌和亮度。 */
            result = ESP_ERR_NOT_SUPPORTED;
            refresh = true;
        }
        speaker_state_t changed;
        if (speaker_service_wait_state(&changed, 0) == ESP_OK)
        {
            refresh = true;
        }
        if (refresh)
        {
            /* 再读取当前服务状态，避免用较早入队的快照覆盖刚设置的音量。 */
            esp_err_t error = app_refresh_ui(&ui, result);
            if (error != ESP_OK)
            {
                ESP_LOGE("app", "UI state update failed: %s", esp_err_to_name(error));
            }
            refresh = error != ESP_OK;
        }
    }
}

#endif

esp_err_t app_start(void)
{
    esp_err_t error = board_keys_start();
    if (error != ESP_OK)
    {
        return error;
    }
    if (xTaskCreate(key_dispatch_task, "key_dispatch", 4096, NULL, 3, NULL) != pdPASS)
    {
        return ESP_ERR_NO_MEM;
    }
#if BOARD_LCD_ENABLE
    if (xTaskCreate(app_business_task, "app_business", 6144, NULL, 3, NULL) != pdPASS)
    {
        return ESP_ERR_NO_MEM;
    }
#endif
    return ESP_OK;
}
