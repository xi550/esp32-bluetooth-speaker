#pragma once

#include <stddef.h>
#include "speaker_service.h"

/* 不依赖硬件的状态和音频运算，与主机回归检查共用。 */
void speaker_state_clear_media(speaker_state_t *s);
void speaker_state_connection(speaker_state_t *s, speaker_connection_t connection);
void speaker_state_audio(speaker_state_t *s, bool started);
void speaker_state_playback(speaker_state_t *s, speaker_playback_t playback);
esp_err_t speaker_control_resolve(speaker_control_t *control, speaker_playback_t playback);
void speaker_pcm_scale(uint8_t *pcm, size_t bytes, uint8_t volume);
