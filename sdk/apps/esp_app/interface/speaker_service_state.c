#include "speaker_service_state.h"
#include <string.h>

void speaker_state_clear_media(speaker_state_t *s)
{
    memset(s->title, 0, sizeof(s->title));
    memset(s->artist, 0, sizeof(s->artist));
    s->position_known = false;
    s->duration_known = false;
    s->position_ms = 0;
    s->duration_ms = 0;
}

void speaker_state_connection(speaker_state_t *s, speaker_connection_t connection)
{
    s->connection = connection;
    if (connection != SPEAKER_CONNECTED)
    {
        speaker_state_clear_media(s);
        s->streaming = false;
        s->sample_rate_hz = 0;
        s->playback = SPEAKER_PLAYBACK_UNKNOWN;
        if (connection == SPEAKER_DISCONNECTED)
        {
            s->avrcp_connected = false;
            s->volume_connected = false;
        }
    }
}

void speaker_state_audio(speaker_state_t *s, bool started)
{
    if (s->connection != SPEAKER_CONNECTED)
    {
        return;
    }
    s->streaming = started;
    if (started)
    {
        s->playback = SPEAKER_PLAYBACK_PLAYING;
    }
    else if (s->playback == SPEAKER_PLAYBACK_PLAYING || s->playback == SPEAKER_PLAYBACK_UNKNOWN)
    {
        s->playback = SPEAKER_PLAYBACK_PAUSED;
    }
}

void speaker_state_playback(speaker_state_t *s, speaker_playback_t playback)
{
    if (s->connection == SPEAKER_CONNECTED && s->avrcp_connected)
    {
        s->playback = playback;
    }
}

esp_err_t speaker_control_resolve(speaker_control_t *control, speaker_playback_t playback)
{
    if (*control < SPEAKER_CONTROL_PLAY || *control > SPEAKER_CONTROL_NEXT)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (*control == SPEAKER_CONTROL_PLAY_PAUSE)
    {
        if (playback == SPEAKER_PLAYBACK_UNKNOWN || playback == SPEAKER_PLAYBACK_ERROR)
        {
            return ESP_ERR_INVALID_STATE;
        }
        *control =
            (playback == SPEAKER_PLAYBACK_PLAYING || playback == SPEAKER_PLAYBACK_FORWARD_SEEK ||
                playback == SPEAKER_PLAYBACK_REVERSE_SEEK)
            ? SPEAKER_CONTROL_PAUSE
            : SPEAKER_CONTROL_PLAY;
    }
    return ESP_OK;
}

void speaker_pcm_scale(uint8_t *pcm, size_t bytes, uint8_t volume)
{
    if (volume == 127)
    {
        return;
    }
    for (size_t i = 0; i + sizeof(int16_t) <= bytes; i += sizeof(int16_t))
    {
        int16_t sample;
        memcpy(&sample, pcm + i, sizeof(sample));
        sample = (int32_t)sample * volume / 127;
        memcpy(pcm + i, &sample, sizeof(sample));
    }
}
