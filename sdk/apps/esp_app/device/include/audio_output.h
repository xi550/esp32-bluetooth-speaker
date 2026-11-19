#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

/* 仅由音频工作任务调用；使用小端 PCM16 立体声，不重新映射声道。 */
esp_err_t audio_output_init(void);
esp_err_t audio_output_set_rate(uint32_t hz);
esp_err_t audio_output_write(const void *pcm, size_t bytes);
