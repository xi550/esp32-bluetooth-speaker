#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    SPEAKER_DISCONNECTED,
    SPEAKER_CONNECTING,
    SPEAKER_CONNECTED,
    SPEAKER_DISCONNECTING,
} speaker_connection_t;

typedef enum {
    SPEAKER_PLAYBACK_UNKNOWN,
    SPEAKER_PLAYBACK_STOPPED,
    SPEAKER_PLAYBACK_PLAYING,
    SPEAKER_PLAYBACK_PAUSED,
    SPEAKER_PLAYBACK_FORWARD_SEEK,
    SPEAKER_PLAYBACK_REVERSE_SEEK,
    SPEAKER_PLAYBACK_ERROR,
} speaker_playback_t;

typedef enum {
    SPEAKER_CONTROL_PLAY,
    SPEAKER_CONTROL_PAUSE,
    SPEAKER_CONTROL_PLAY_PAUSE,
    SPEAKER_CONTROL_PREVIOUS,
    SPEAKER_CONTROL_NEXT,
} speaker_control_t;

/* Value snapshot: no borrowed pointers or SDK types. Volume is linear 0..127. */
typedef struct {
    speaker_connection_t connection;
    speaker_playback_t playback;
    bool avrcp_connected;         /* CT ready for playback commands */
    bool volume_connected;       /* TG connected; phone support still required */
    bool streaming;              /* A2DP transport, distinct from player status */
    uint32_t sample_rate_hz;      /* 0 until a supported format is negotiated */
    uint8_t volume;               /* 0 = mute, 127 = unchanged PCM */
    esp_err_t volume_sync_result; /* Last TG notification submission result */
    bool control_busy;
    esp_err_t control_result;     /* Last completed command: response/timeout/send error */
} speaker_state_t;

/* Task context only (not ISR). Start once after BSP; boot errors are fatal. */
esp_err_t speaker_service_start(void);
esp_err_t speaker_service_get_state(speaker_state_t *state);

/* One app task consumes changes, then distributes snapshots to its UI.
 * First receive returns the current snapshot. A length-one mailbox coalesces
 * changes: slow consumers get the latest state, not a history of every edge.
 * timeout_ms: 0 = poll, UINT32_MAX = wait forever. Copies all data to caller. */
esp_err_t speaker_service_wait_state(speaker_state_t *state, uint32_t timeout_ms);

/* Nonblocking submission; ESP_OK means accepted, not that the phone played it.
 * Only one command in flight. Busy => ESP_ERR_NOT_FINISHED; disconnected =>
 * ESP_ERR_INVALID_STATE. Observe control_busy/control_result for completion.
 * PLAY_PAUSE needs known playback status. Actual playback follows peer events. */
esp_err_t speaker_service_control(speaker_control_t control);

/* Applies locally even without a phone. Not persisted. Local changes send a
 * one-shot AVRCP TG notification only when the phone subscribed. Remote volume
 * updates do not echo. ESP_OK means local gain applied; protocol work is async.
 * Failed notification submissions are retried; see volume_sync_result. */
esp_err_t speaker_service_set_volume(uint8_t volume);
