#include "lcd_hal.h"
#include "board_esp_proj_cfg.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_check.h"
static esp_lcd_panel_handle_t panel;
static SemaphoreHandle_t done;
static bool on_done(esp_lcd_panel_io_handle_t io,esp_lcd_panel_io_event_data_t *ev,void *ctx) {
 BaseType_t wake=pdFALSE; xSemaphoreGiveFromISR((SemaphoreHandle_t)ctx,&wake); return wake==pdTRUE;
}
esp_err_t lcd_hal_init(void) {
 if(panel) return ESP_ERR_INVALID_STATE;
 done=xSemaphoreCreateBinary(); if(!done) return ESP_ERR_NO_MEM;
 spi_bus_config_t bus={.sclk_io_num=BOARD_LCD_SCLK,.mosi_io_num=BOARD_LCD_MOSI,
  .miso_io_num=-1,.quadwp_io_num=-1,.quadhd_io_num=-1,
  .max_transfer_sz=CONFIG_SPEAKER_LCD_WIDTH*CONFIG_SPEAKER_LCD_HEIGHT*2};
 ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI2_HOST,&bus,SPI_DMA_CH_AUTO),"lcd","bus");
 esp_lcd_panel_io_spi_config_t io_cfg={.dc_gpio_num=BOARD_LCD_DC,.cs_gpio_num=BOARD_LCD_CS,
  .pclk_hz=10000000,.lcd_cmd_bits=8,.lcd_param_bits=8,.spi_mode=0,.trans_queue_depth=1,
  .on_color_trans_done=on_done,.user_ctx=done};
 esp_lcd_panel_io_handle_t io;
 ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST,&io_cfg,&io),"lcd","io");
 esp_lcd_panel_dev_config_t dev={.reset_gpio_num=BOARD_LCD_RST,
  .rgb_ele_order=LCD_RGB_ELEMENT_ORDER_RGB,.bits_per_pixel=16};
 ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(io,&dev,&panel),"lcd","panel");
 ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel),"lcd","reset");
 ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel),"lcd","init");
 ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(panel,CONFIG_SPEAKER_LCD_X_GAP,CONFIG_SPEAKER_LCD_Y_GAP),"lcd","gap");
#ifdef CONFIG_SPEAKER_LCD_INVERT
 ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(panel,true),"lcd","invert");
#endif
 return esp_lcd_panel_disp_on_off(panel,true);
}
esp_err_t lcd_hal_flush(int x0,int y0,int x1,int y1,const uint16_t *pixels) {
 if(!panel) return ESP_ERR_INVALID_STATE;
 if(!pixels || x0<0 || y0<0 || x1<=x0 || y1<=y0 ||
  x1>CONFIG_SPEAKER_LCD_WIDTH || y1>CONFIG_SPEAKER_LCD_HEIGHT) return ESP_ERR_INVALID_ARG;
 ESP_RETURN_ON_ERROR(esp_lcd_panel_draw_bitmap(panel,x0,y0,x1,y1,pixels),"lcd","draw");
 xSemaphoreTake(done,portMAX_DELAY); return ESP_OK;
}
