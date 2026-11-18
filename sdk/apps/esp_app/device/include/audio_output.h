#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
/* Single owner: audio worker. Stereo PCM16 LE, no channel remapping. */
esp_err_t audio_output_init(void);
esp_err_t audio_output_set_rate(uint32_t hz);
esp_err_t audio_output_write(const void *pcm, size_t bytes);
