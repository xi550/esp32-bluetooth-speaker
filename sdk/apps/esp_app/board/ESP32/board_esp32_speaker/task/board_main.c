#include "board_esp_proj.h"
#include "speaker_service.h"
#include "esp_check.h"
#include "board_app.h"
#include "board_esp_proj_cfg.h"
#include "lvgl_port.h"

void app_main(void)
{
    ESP_ERROR_CHECK(board_init());
    ESP_ERROR_CHECK(speaker_service_start());
#if BOARD_LCD_ENABLE
    ESP_ERROR_CHECK(lvgl_port_start());
#endif
    ESP_ERROR_CHECK(app_start());
}
