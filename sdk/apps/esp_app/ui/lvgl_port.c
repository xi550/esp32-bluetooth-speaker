#include "lvgl_port.h"
#include "board_esp_proj_cfg.h"
#include "lcd_hal.h"
#include "lvgl.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <stdio.h>

#if BOARD_LCD_ENABLE

typedef struct
{
    speaker_ui_key_t key;
    uint32_t time_ms;
} key_event_t;

static QueueHandle_t state_queue;
static QueueHandle_t key_queue;
static QueueHandle_t action_queue;
static TaskHandle_t ui_task_handle;
static lv_display_t *display;
static void *draw_buffer;
static bool flush_failed;
static const char *tag = "ui";

static uint32_t clock_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void flush(lv_display_t *target, const lv_area_t *area, uint8_t *pixels)
{
    uint32_t count = (uint32_t)lv_area_get_width(area) * lv_area_get_height(area);
    lv_draw_sw_rgb565_swap(pixels, count);
    esp_err_t error =
        lcd_hal_flush(area->x1, area->y1, area->x2 + 1, area->y2 + 1, (const uint16_t *)pixels);
    if (error != ESP_OK)
    {
        if (!flush_failed)
        {
            ESP_LOGE(tag, "LCD flush failed: %s", esp_err_to_name(error));
        }
        flush_failed = true;
    }
    /* 无论传输成功与否，都释放绘图缓冲；失败由任务稍后重新请求整屏刷新。 */
    lv_display_flush_ready(target);
}

static bool post_action(const speaker_ui_action_t *action, void *context)
{
    return xQueueSend(action_queue, action, 0) == pdTRUE;
}

static void ui_task(void *context)
{
    speaker_ui_state_t current;
    key_event_t event;
    uint32_t last_retry = clock_ms();
    TickType_t last_wake = xTaskGetTickCount();
    for (;;)
    {
        if (xQueueReceive(state_queue, &current, 0) == pdTRUE)
        {
            speaker_ui_set_state(&current, clock_ms());
        }
        /* 保留投递时刻，避免一次慢刷新把两次普通单击误判为双击。 */
        for (unsigned i = 0; i < 16 && xQueueReceive(key_queue, &event, 0) == pdTRUE; ++i)
        {
            speaker_ui_send_key(event.key, event.time_ms);
        }
        uint32_t now = clock_ms();
        speaker_ui_tick(now);
        if (flush_failed && (uint32_t)(now - last_retry) >= 1000)
        {
            flush_failed = false;
            last_retry = now;
            lv_obj_invalidate(lv_screen_active());
        }
        lv_timer_handler();
        /* 固定轮询周期，避免每次绘图耗时继续叠加到休眠时间上。 */
        xTaskDelayUntil(&last_wake, pdMS_TO_TICKS(10) ? pdMS_TO_TICKS(10) : 1);
    }
}

static void cleanup(void)
{
    if (state_queue)
    {
        vQueueDelete(state_queue);
        state_queue = NULL;
    }
    if (key_queue)
    {
        vQueueDelete(key_queue);
        key_queue = NULL;
    }
    if (action_queue)
    {
        vQueueDelete(action_queue);
        action_queue = NULL;
    }
    heap_caps_free(draw_buffer);
    draw_buffer = NULL;
}

esp_err_t lvgl_port_start(void)
{
    if (ui_task_handle)
    {
        return ESP_ERR_INVALID_STATE;
    }
    _Static_assert(BOARD_UI_BUFFER_LINES >= 1 && BOARD_UI_BUFFER_LINES <= BOARD_LCD_HEIGHT,
        "Invalid UI buffer lines");
    _Static_assert(
        BOARD_UI_REFRESH_MS >= 1 && BOARD_UI_REFRESH_MS <= 1000, "Invalid UI refresh period");
    size_t buffer_size = BOARD_LCD_WIDTH * BOARD_UI_BUFFER_LINES * sizeof(uint16_t);
    state_queue = xQueueCreate(1, sizeof(speaker_ui_state_t));
    key_queue = xQueueCreate(16, sizeof(key_event_t));
    action_queue = xQueueCreate(16, sizeof(speaker_ui_action_t));
    draw_buffer = heap_caps_malloc(buffer_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!state_queue || !key_queue || !action_queue || !draw_buffer)
    {
        cleanup();
        return ESP_ERR_NO_MEM;
    }
    lv_init();
    lv_tick_set_cb(clock_ms);
    display = lv_display_create(BOARD_LCD_WIDTH, BOARD_LCD_HEIGHT);
    if (!display)
    {
        lv_deinit();
        cleanup();
        return ESP_ERR_NO_MEM;
    }
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, draw_buffer, NULL, buffer_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush);
    lv_timer_set_period(lv_display_get_refr_timer(display), BOARD_UI_REFRESH_MS);
    speaker_ui_config_t config = {
        .double_click_ms = BOARD_UI_DOUBLE_CLICK_MS,
        .volume_step = BOARD_UI_VOLUME_STEP,
        .brightness_step = BOARD_UI_BRIGHTNESS_STEP,
    };
    if (!speaker_ui_init(&config, post_action, NULL))
    {
        lv_deinit();
        cleanup();
        return ESP_ERR_INVALID_ARG;
    }
    speaker_ui_state_t initial = {0};
    snprintf(initial.device_name, sizeof(initial.device_name), "%s", BOARD_BT_NAME);
    snprintf(initial.firmware_version,
        sizeof(initial.firmware_version),
        "%s",
        esp_app_get_description()->version);
    speaker_ui_set_state(&initial, clock_ms());
    /* 首帧完成后再点亮，避免初始化期间显示未定义的屏幕 RAM。 */
    lv_refr_now(display);
    esp_err_t backlight_error =
        flush_failed ? ESP_FAIL : lcd_hal_set_brightness(BOARD_INITIAL_BRIGHTNESS);
    if (backlight_error != ESP_OK)
    {
        lv_deinit();
        cleanup();
        return backlight_error;
    }
    initial.brightness_available = lcd_hal_get_brightness(&initial.brightness_percent) == ESP_OK;
    speaker_ui_set_state(&initial, clock_ms());
    if (xTaskCreate(ui_task, "speaker_ui", 8192, NULL, 3, &ui_task_handle) != pdPASS)
    {
        /* 启动失败由入口终止运行，不支持重新初始化已经创建的页面对象。 */
        lv_deinit();
        cleanup();
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(tag,
        "LVGL 9.3.0, %dx%d, buffer=%u bytes, refresh=%u ms, free heap=%u",
        BOARD_LCD_WIDTH,
        BOARD_LCD_HEIGHT,
        (unsigned)buffer_size,
        BOARD_UI_REFRESH_MS,
        (unsigned)esp_get_free_heap_size());
    return ESP_OK;
}

esp_err_t lvgl_port_update(const speaker_ui_state_t *state)
{
    if (!speaker_ui_state_valid(state))
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (!ui_task_handle)
    {
        return ESP_ERR_INVALID_STATE;
    }
    return xQueueOverwrite(state_queue, state) == pdTRUE ? ESP_OK : ESP_FAIL;
}

esp_err_t lvgl_port_input_at(speaker_ui_key_t key, uint32_t time_ms)
{
    if (key < SPEAKER_UI_KEY_UP || key > SPEAKER_UI_KEY_VOLUME_DOWN)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (!ui_task_handle)
    {
        return ESP_ERR_INVALID_STATE;
    }
    key_event_t event = {.key = key, .time_ms = time_ms};
    return xQueueSend(key_queue, &event, 0) == pdTRUE ? ESP_OK : ESP_ERR_NOT_FINISHED;
}

esp_err_t lvgl_port_wait_action(speaker_ui_action_t *action, uint32_t timeout_ms)
{
    if (!action)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (!ui_task_handle)
    {
        return ESP_ERR_INVALID_STATE;
    }
    TickType_t ticks = timeout_ms == UINT32_MAX
        ? portMAX_DELAY
        : (TickType_t)(((uint64_t)timeout_ms * configTICK_RATE_HZ + 999) / 1000);
    return xQueueReceive(action_queue, action, ticks) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

#else

esp_err_t lvgl_port_start(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t lvgl_port_update(const speaker_ui_state_t *state)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t lvgl_port_input_at(speaker_ui_key_t key, uint32_t time_ms)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t lvgl_port_wait_action(speaker_ui_action_t *action, uint32_t timeout_ms)
{
    return ESP_ERR_NOT_SUPPORTED;
}

#endif

/* 普通业务调用使用当前时刻；框架重试使用原事件时刻。 */
esp_err_t lvgl_port_input(speaker_ui_key_t key)
{
    return lvgl_port_input_at(key, (uint32_t)(esp_timer_get_time() / 1000));
}
