#include "speaker_bsp.h"
#include "buttons.h"
#include "lcd_hal.h"
#include "audio_output.h"
#include "esp_check.h"
#include "sdkconfig.h"
esp_err_t speaker_bsp_init(void) {
 ESP_RETURN_ON_ERROR(buttons_init(),"bsp","buttons");
 ESP_RETURN_ON_ERROR(audio_output_init(),"bsp","audio");
#ifdef CONFIG_SPEAKER_LCD_ENABLE
 ESP_RETURN_ON_ERROR(lcd_hal_init(),"bsp","LCD");
#endif
 return ESP_OK;
}
