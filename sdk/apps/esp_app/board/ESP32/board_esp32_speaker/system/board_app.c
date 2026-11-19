#include "board_app.h"
#include "board_keyevent.h"
#include "board_esp_proj_cfg.h"
#include "lvgl_port.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* 实习生在这里按事件编写业务；扫描、消抖、长按和连发由框架完成。
 * 默认只投递 UI 输入，功能操作仍从 lvgl_port_wait_action 接收并处理。
 * 不要在这里等待业务完成；耗时操作交给业务任务，避免按键事件积压。 */
static void app_key_event(uint16_t event, uint32_t time_ms)
{
#if BOARD_LCD_ENABLE
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
        key = SPEAKER_UI_KEY_VOLUME_UP;
        break;
    case KRS(RKEY_VOL_DOWN):
    case KRH(RKEY_VOL_DOWN):
        key = SPEAKER_UI_KEY_VOLUME_DOWN;
        break;
    default:
        return;
    }
    esp_err_t error;
    /* UI 暂时忙时保留当前事件，扫描任务继续独立运行。 */
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
#else
    /* 无屏产品同样在此处理 KRS/KRH；未接入业务时不执行功能。 */
    (void)event;
    (void)time_ms;
#endif
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
    /* 实习生待办：创建业务任务，消费 UI 动作、调用服务并回传真实状态。 */
    return ESP_OK;
}
