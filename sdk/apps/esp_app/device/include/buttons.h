#pragma once
#include <stdbool.h>
#include "esp_err.h"
esp_err_t buttons_init(void);
/* Raw active-low state, 0..4; no debounce or action mapping. */
esp_err_t buttons_read(unsigned index,bool *pressed);
