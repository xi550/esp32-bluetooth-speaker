#pragma once
#include "esp_err.h"
/* Start once after BSP; boot errors are fatal. No AVRCP or application policy. */
esp_err_t speaker_service_start(void);
