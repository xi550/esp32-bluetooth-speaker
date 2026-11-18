#include "audio_output.h"
#include "board_pins.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
static i2s_chan_handle_t tx;
static uint32_t rate=44100;
esp_err_t audio_output_init(void) {
 if(tx) return ESP_ERR_INVALID_STATE;
 i2s_chan_config_t chan=I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0,I2S_ROLE_MASTER);
 chan.auto_clear=true;
 ESP_RETURN_ON_ERROR(i2s_new_channel(&chan,&tx,NULL),"i2s","channel");
 i2s_std_config_t cfg={
  .clk_cfg=I2S_STD_CLK_DEFAULT_CONFIG(44100),
  .slot_cfg=I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,I2S_SLOT_MODE_STEREO),
  .gpio_cfg={.mclk=I2S_GPIO_UNUSED,.bclk=BOARD_I2S_BCLK,.ws=BOARD_I2S_LRCLK,
    .dout=BOARD_I2S_DOUT,.din=I2S_GPIO_UNUSED}};
 ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(tx,&cfg),"i2s","std");
 return i2s_channel_enable(tx);
}
esp_err_t audio_output_set_rate(uint32_t hz) {
 if(!tx) return ESP_ERR_INVALID_STATE;
 if(hz!=16000 && hz!=32000 && hz!=44100 && hz!=48000) return ESP_ERR_INVALID_ARG;
 if(hz==rate) return ESP_OK;
 ESP_RETURN_ON_ERROR(i2s_channel_disable(tx),"i2s","disable");
 i2s_std_clk_config_t cfg=I2S_STD_CLK_DEFAULT_CONFIG(hz);
 ESP_RETURN_ON_ERROR(i2s_channel_reconfig_std_clock(tx,&cfg),"i2s","clock");
 ESP_RETURN_ON_ERROR(i2s_channel_enable(tx),"i2s","enable");
 rate=hz; return ESP_OK;
}
esp_err_t audio_output_write(const void *pcm,size_t bytes) {
 if(!tx) return ESP_ERR_INVALID_STATE;
 if(!pcm || bytes%4) return ESP_ERR_INVALID_ARG;
 size_t written=0;
 esp_err_t err=i2s_channel_write(tx,pcm,bytes,&written,100);
 return err==ESP_OK && written!=bytes ? ESP_FAIL : err;
}
