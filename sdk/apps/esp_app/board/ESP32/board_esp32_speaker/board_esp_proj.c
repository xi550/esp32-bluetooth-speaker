#include "board_esp_proj.h"
#include "board_esp_proj_cfg.h"
#include "buttons.h"
#include "lcd_hal.h"
#include "audio_output.h"
#include "esp_check.h"
#include "sdkconfig.h"

/* 在编译阶段检查产品配置。 */
_Static_assert(
    BOARD_KEY_ACTIVE_LEVEL == 0 || BOARD_KEY_ACTIVE_LEVEL == 1, "Key active level must be 0 or 1");

_Static_assert(BOARD_LCD_ENABLE == 0 || BOARD_LCD_ENABLE == 1, "LCD enable must be 0 or 1");
_Static_assert(BOARD_LCD_INVERT == 0 || BOARD_LCD_INVERT == 1, "LCD inversion must be 0 or 1");
_Static_assert(BOARD_LCD_SPI_MODE >= 0 && BOARD_LCD_SPI_MODE <= 3, "Invalid LCD SPI mode");
_Static_assert(BOARD_LCD_SPI_CLOCK_HZ > 0, "LCD SPI clock must be positive");
_Static_assert(BOARD_LCD_WIDTH >= 1 && BOARD_LCD_WIDTH <= 320, "LCD width must be 1..320");
_Static_assert(BOARD_LCD_HEIGHT >= 1 && BOARD_LCD_HEIGHT <= 320, "LCD height must be 1..320");
_Static_assert(BOARD_LCD_X_GAP >= 0 && BOARD_LCD_X_GAP <= 320, "LCD X offset must be 0..320");
_Static_assert(BOARD_LCD_Y_GAP >= 0 && BOARD_LCD_Y_GAP <= 320, "LCD Y offset must be 0..320");

_Static_assert(BOARD_INITIAL_VOLUME >= 0 && BOARD_INITIAL_VOLUME <= 127, "Volume must be 0..127");
_Static_assert(
    sizeof(BOARD_BT_NAME) > 1 && sizeof(BOARD_BT_NAME) <= 249, "BT name must be 1..248 bytes");

/* 在任何驱动操作 GPIO 前检查配置；只有已启用的外设才占用引脚。 */
static esp_err_t validate_pins(
    const gpio_num_t *pins, unsigned outputs, unsigned count, bool key_pulls)
{
    uint64_t used = 0;

    for (unsigned i = 0; i < count; ++i)
    {
        int pin = pins[i];

        if (pin < 0 || pin >= GPIO_NUM_MAX || !GPIO_IS_VALID_GPIO(pin) ||
            (i < outputs && !GPIO_IS_VALID_OUTPUT_GPIO(pin)) ||
            (i >= outputs && key_pulls && !GPIO_IS_VALID_OUTPUT_GPIO(pin)) ||
            (pin >= 6 && pin <= 11))
        {
            ESP_LOGE("bsp", "GPIO%d invalid/reserved or incompatible with direction/pulls", pin);
            return ESP_ERR_INVALID_ARG;
        }

        uint64_t bit = 1ULL << pin;

        if (used & bit)
        {
            ESP_LOGE("bsp", "GPIO%d assigned more than once", pin);
            return ESP_ERR_INVALID_ARG;
        }
        used |= bit;

        if (pin == 0 || pin == 2 || pin == 5 || pin == 12 || pin == 15)
        {
            ESP_LOGW("bsp", "GPIO%d is a boot strapping pin; verify external levels at reset", pin);
        }

        if (pin == 1 || pin == 3)
        {
            ESP_LOGW("bsp", "GPIO%d overlaps the default UART console", pin);
        }
    }

    return ESP_OK;
}

esp_err_t board_init(void)
{
    static const gpio_num_t outputs[] = {
        BOARD_I2S_BCLK,
        BOARD_I2S_LRCLK,
        BOARD_I2S_DOUT,
#if BOARD_LCD_ENABLE
        BOARD_LCD_SCLK,
        BOARD_LCD_MOSI,
        BOARD_LCD_CS,
        BOARD_LCD_DC,
        BOARD_LCD_RST,
#endif
    };
    static const gpio_num_t keys[] = BOARD_BUTTON_PINS;

    _Static_assert(sizeof(keys) / sizeof(keys[0]) == BOARD_BUTTON_COUNT, "Button count mismatch");

    gpio_num_t pins[sizeof(outputs) / sizeof(outputs[0]) + BOARD_BUTTON_COUNT];
    unsigned count = 0;

    for (unsigned i = 0; i < sizeof(outputs) / sizeof(outputs[0]); ++i)
    {
        pins[count++] = outputs[i];
    }

    for (unsigned i = 0; i < BOARD_BUTTON_COUNT; ++i)
    {
        pins[count++] = keys[i];
    }

    bool pulls =
        BOARD_KEY_PULL_UP == GPIO_PULLUP_ENABLE || BOARD_KEY_PULL_DOWN == GPIO_PULLDOWN_ENABLE;

    ESP_RETURN_ON_ERROR(validate_pins(pins, sizeof(outputs) / sizeof(outputs[0]), count, pulls),
        "bsp",
        "pin configuration");

    ESP_RETURN_ON_ERROR(buttons_init(), "bsp", "buttons");
    ESP_RETURN_ON_ERROR(audio_output_init(), "bsp", "audio");
#if BOARD_LCD_ENABLE
    ESP_RETURN_ON_ERROR(lcd_hal_init(), "bsp", "LCD");
#endif

    return ESP_OK;
}
