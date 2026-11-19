#include "board_keyevent.h"
#include "board_esp_proj_cfg.h"
#include "buttons.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

_Static_assert(BOARD_KEY_SCAN_MS > 0, "Scan period must be positive");
_Static_assert(BOARD_KEY_DEBOUNCE_MS >= BOARD_KEY_SCAN_MS, "Debounce must cover a scan");
_Static_assert(BOARD_KEY_LONG_MS > BOARD_KEY_DEBOUNCE_MS, "Long press must exceed debounce");
_Static_assert(BOARD_KEY_HOLD_MS >= BOARD_KEY_SCAN_MS, "Hold period must cover a scan");
_Static_assert(BOARD_KEY_LONG_MS < INT32_MAX && BOARD_KEY_HOLD_MS < INT32_MAX,
    "Key intervals must fit clock arithmetic");

typedef struct
{
    bool raw;
    bool pressed;
    bool long_sent;
    uint32_t changed_ms;
    uint32_t pressed_ms;
    uint32_t repeat_ms;
} key_state_t;

static const uint8_t key_map[BOARD_BUTTON_COUNT] = {
    [BOARD_KEY_UP_INDEX] = RKEY_UP,
    [BOARD_KEY_DOWN_INDEX] = RKEY_DOWN,
    [BOARD_KEY_CONFIRM_INDEX] = RKEY_CONFIRM,
    [BOARD_KEY_VOLUME_UP_INDEX] = RKEY_VOL_UP,
    [BOARD_KEY_VOLUME_DOWN_INDEX] = RKEY_VOL_DOWN,
};
static key_state_t keys[BOARD_BUTTON_COUNT];
static QueueHandle_t events;

static void post_event(uint16_t code, uint32_t now)
{
    board_key_event_t event = {.code = code, .time_ms = now};
    /* 为五键的按下和松手保留空间，积压时不再追加无意义的连发。 */
    if ((code & 0xff00u) == 0x400u && uxQueueSpacesAvailable(events) <= 2 * BOARD_BUTTON_COUNT)
    {
        return;
    }
    if (xQueueSend(events, &event, 0) != pdTRUE)
    {
        ESP_LOGE("keys", "Key queue full, lost event 0x%04x", (unsigned)code);
    }
}

static void scan_key(unsigned index, bool raw, uint32_t now)
{
    key_state_t *state = &keys[index];
    uint16_t key = key_map[index];
    if (raw != state->raw)
    {
        state->raw = raw;
        state->changed_ms = now;
    }
    if (state->raw != state->pressed &&
        (uint32_t)(now - state->changed_ms) >= BOARD_KEY_DEBOUNCE_MS)
    {
        state->pressed = state->raw;
        if (state->pressed)
        {
            state->pressed_ms = now;
            state->long_sent = false;
            post_event(KRS(key), now);
        }
        else
        {
            post_event(state->long_sent ? KRLU(key) : KRU(key), now);
        }
    }
    /* 松手消抖期间不额外触发长按或连发，抖动恢复后继续原按住计时。 */
    if (!state->pressed || !state->raw)
    {
        return;
    }
    if (!state->long_sent && (uint32_t)(now - state->pressed_ms) >= BOARD_KEY_LONG_MS)
    {
        state->long_sent = true;
        state->repeat_ms = now;
        post_event(KRL(key), now);
    }
    else if (state->long_sent && (uint32_t)(now - state->repeat_ms) >= BOARD_KEY_HOLD_MS)
    {
        /* 调度变慢时不补发历史连击，避免恢复后音量突然跳变。 */
        state->repeat_ms = now;
        post_event(KRH(key), now);
    }
}

static void scan_task(void *context)
{
    TickType_t wake = xTaskGetTickCount();
    const TickType_t period =
        pdMS_TO_TICKS(BOARD_KEY_SCAN_MS) ? pdMS_TO_TICKS(BOARD_KEY_SCAN_MS) : 1;
    for (;;)
    {
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
        for (unsigned i = 0; i < BOARD_BUTTON_COUNT; ++i)
        {
            bool pressed;
            if (buttons_read(i, &pressed) == ESP_OK)
            {
                scan_key(i, pressed, now);
            }
        }
        xTaskDelayUntil(&wake, period);
    }
}

esp_err_t board_keys_start(void)
{
    if (events)
    {
        return ESP_ERR_INVALID_STATE;
    }
    bool pressed;
    esp_err_t error = buttons_read(0, &pressed);
    if (error != ESP_OK)
    {
        return error;
    }
    events = xQueueCreate(32, sizeof(board_key_event_t));
    if (!events)
    {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(scan_task, "key_scan", 3072, NULL, 4, NULL) != pdPASS)
    {
        vQueueDelete(events);
        events = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t board_keys_wait(board_key_event_t *event, uint32_t timeout_ms)
{
    if (!event)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (!events)
    {
        return ESP_ERR_INVALID_STATE;
    }
    TickType_t ticks = timeout_ms == UINT32_MAX
        ? portMAX_DELAY
        : (TickType_t)(((uint64_t)timeout_ms * configTICK_RATE_HZ + 999) / 1000);
    return xQueueReceive(events, event, ticks) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}
