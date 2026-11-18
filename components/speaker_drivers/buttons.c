#include "buttons.h"
#include "board_pins.h"
#include "driver/gpio.h"
static const int pins[BOARD_BUTTON_COUNT]=BOARD_BUTTON_PINS;
static bool ready;
esp_err_t buttons_init(void) {
 uint64_t mask=0;
 for(unsigned i=0;i<BOARD_BUTTON_COUNT;++i) mask|=1ULL<<pins[i];
 gpio_config_t cfg={.pin_bit_mask=mask,.mode=GPIO_MODE_INPUT,
  .pull_up_en=GPIO_PULLUP_ENABLE,.pull_down_en=GPIO_PULLDOWN_DISABLE,.intr_type=GPIO_INTR_DISABLE};
 esp_err_t err=gpio_config(&cfg); ready=err==ESP_OK; return err;
}
esp_err_t buttons_read(unsigned index,bool *pressed) {
 if(index>=BOARD_BUTTON_COUNT || !pressed) return ESP_ERR_INVALID_ARG;
 if(!ready) return ESP_ERR_INVALID_STATE;
 *pressed=gpio_get_level(pins[index])==0; return ESP_OK;
}
