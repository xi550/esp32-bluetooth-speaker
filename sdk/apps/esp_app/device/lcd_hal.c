#include "lcd_hal.h"
#include "board_esp_proj_cfg.h"
#include "driver/spi_master.h"
#include "driver/ledc.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_check.h"

static esp_lcd_panel_handle_t panel;
static SemaphoreHandle_t done;
static bool backlight_ready;
static uint8_t brightness_percent;

_Static_assert(BOARD_LCD_BL_ACTIVE_LEVEL == 0 || BOARD_LCD_BL_ACTIVE_LEVEL == 1,
    "Backlight active level must be 0 or 1");
_Static_assert(BOARD_LCD_BL_PWM_BITS >= 1 && BOARD_LCD_BL_PWM_BITS <= 14,
    "Backlight PWM resolution must be 1..14 bits");
_Static_assert(BOARD_LCD_BL_PWM_HZ > 0, "Backlight PWM frequency must be positive");
_Static_assert(BOARD_INITIAL_BRIGHTNESS >= 0 && BOARD_INITIAL_BRIGHTNESS <= 100,
    "Initial brightness must be 0..100");

static esp_err_t backlight_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = BOARD_LCD_BL_PWM_BITS,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = BOARD_LCD_BL_PWM_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), "lcd", "backlight timer");
    ledc_channel_config_t channel = {
        .gpio_num = BOARD_LCD_BL_PIN,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
        .flags.output_invert = BOARD_LCD_BL_ACTIVE_LEVEL == 0,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&channel), "lcd", "backlight channel");
    brightness_percent = 0;
    backlight_ready = true;
    return ESP_OK;
}

esp_err_t lcd_hal_set_brightness(uint8_t percent)
{
    if (percent > 100)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (!backlight_ready)
    {
        return ESP_ERR_INVALID_STATE;
    }
    uint32_t duty = ((1u << BOARD_LCD_BL_PWM_BITS) * percent + 50u) / 100u;
    ESP_RETURN_ON_ERROR(
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty), "lcd", "backlight duty");
    ESP_RETURN_ON_ERROR(
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0), "lcd", "backlight update");
    brightness_percent = percent;
    return ESP_OK;
}

esp_err_t lcd_hal_get_brightness(uint8_t *percent)
{
    if (!percent)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (!backlight_ready)
    {
        return ESP_ERR_INVALID_STATE;
    }
    *percent = brightness_percent;
    return ESP_OK;
}

static bool on_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *ev, void *ctx)
{
    BaseType_t wake = pdFALSE;
    xSemaphoreGiveFromISR((SemaphoreHandle_t)ctx, &wake);
    return wake == pdTRUE;
}

esp_err_t lcd_hal_init(void)
{
    if (panel || backlight_ready)
    {
        return ESP_ERR_INVALID_STATE;
    }
    ESP_RETURN_ON_ERROR(backlight_init(), "lcd", "backlight");
    done = xSemaphoreCreateBinary();
    if (!done)
    {
        return ESP_ERR_NO_MEM;
    }
    spi_bus_config_t bus = {
        .sclk_io_num = BOARD_LCD_SCLK,
        .mosi_io_num = BOARD_LCD_MOSI,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = BOARD_LCD_WIDTH * BOARD_LCD_HEIGHT * 2,
    };
    ESP_RETURN_ON_ERROR(
        spi_bus_initialize(BOARD_LCD_SPI_HOST, &bus, SPI_DMA_CH_AUTO), "lcd", "bus");
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = BOARD_LCD_DC,
        .cs_gpio_num = BOARD_LCD_CS,
        .pclk_hz = BOARD_LCD_SPI_CLOCK_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = BOARD_LCD_SPI_MODE,
        .trans_queue_depth = 1,
        .on_color_trans_done = on_done,
        .user_ctx = done,
    };
    esp_lcd_panel_io_handle_t io;
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)BOARD_LCD_SPI_HOST, &io_cfg, &io),
        "lcd",
        "io");
    esp_lcd_panel_dev_config_t dev = {
        .reset_gpio_num = BOARD_LCD_RST,
        .rgb_ele_order = BOARD_LCD_COLOR_ORDER,
        .bits_per_pixel = 16,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(io, &dev, &panel), "lcd", "panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel), "lcd", "reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel), "lcd", "init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(panel, BOARD_LCD_SWAP_XY), "lcd", "swap axes");
    ESP_RETURN_ON_ERROR(
        esp_lcd_panel_mirror(panel, BOARD_LCD_MIRROR_X, BOARD_LCD_MIRROR_Y), "lcd", "mirror");
    ESP_RETURN_ON_ERROR(
        esp_lcd_panel_set_gap(panel, BOARD_LCD_X_GAP, BOARD_LCD_Y_GAP), "lcd", "gap");
#if BOARD_LCD_INVERT
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(panel, true), "lcd", "invert");
#endif
    return esp_lcd_panel_disp_on_off(panel, true);
}

esp_err_t lcd_hal_flush(int x0, int y0, int x1, int y1, const uint16_t *pixels)
{
    if (!panel)
    {
        return ESP_ERR_INVALID_STATE;
    }
    if (!pixels || x0 < 0 || y0 < 0 || x1 <= x0 || y1 <= y0 || x1 > BOARD_LCD_WIDTH ||
        y1 > BOARD_LCD_HEIGHT)
    {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(esp_lcd_panel_draw_bitmap(panel, x0, y0, x1, y1, pixels), "lcd", "draw");
    xSemaphoreTake(done, portMAX_DELAY);
    return ESP_OK;
}
