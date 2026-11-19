#include "buttons.h"
#include "board_esp_proj_cfg.h"
#include "driver/gpio.h"

static const gpio_num_t pins[] = BOARD_BUTTON_PINS;
_Static_assert(
    sizeof(pins) / sizeof(pins[0]) == BOARD_BUTTON_COUNT, "Button count does not match pin list");
static bool ready;

esp_err_t buttons_init(void)
{
    uint64_t mask = 0;
    for (unsigned i = 0; i < BOARD_BUTTON_COUNT; ++i)
    {
        mask |= 1ULL << pins[i];
    }
    gpio_config_t cfg = {
        .pin_bit_mask = mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = BOARD_KEY_PULL_UP,
        .pull_down_en = BOARD_KEY_PULL_DOWN,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&cfg);
    ready = err == ESP_OK;
    return err;
}

esp_err_t buttons_read(unsigned index, bool *pressed)
{
    if (index >= BOARD_BUTTON_COUNT || !pressed)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (!ready)
    {
        return ESP_ERR_INVALID_STATE;
    }
    *pressed = gpio_get_level(pins[index]) == BOARD_KEY_ACTIVE_LEVEL;
    return ESP_OK;
}
