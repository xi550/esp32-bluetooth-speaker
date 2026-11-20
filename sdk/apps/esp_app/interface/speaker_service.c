#include <string.h>
#include <stdatomic.h>
#include "speaker_service.h"
#include "master_volume.h"
#include "speaker_service_state.h"
#include "board_esp_proj_cfg.h"
#include "audio_output.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"
#include "esp_a2dp_legacy_api.h"
#include "esp_avrc_api.h"
#include "nvs_flash.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#if !CONFIG_BT_CLASSIC_ENABLED || !CONFIG_BT_A2DP_ENABLE || !CONFIG_BT_AVRCP_ENABLED ||            \
    defined(CONFIG_BT_A2DP_USE_EXTERNAL_CODEC)
#error "Requires Classic A2DP with internal SBC decoder and AVRCP"
#endif
#define PCM_BLOCK          2048
#define PCM_DEPTH          12
#define CONTROL_TIMEOUT_MS 600
/* 长期占用的状态通知事务标签与其他请求分开使用。 */
#define TL_CAPABILITIES 0
#define TL_NOTIFICATION 1
#define TL_PLAY_STATUS  2
#define TL_METADATA     3
#define TL_TRACK_CHANGE 4
#define MEDIA_ATTRIBUTES                                                                           \
    (ESP_AVRC_MD_ATTR_TITLE | ESP_AVRC_MD_ATTR_ARTIST | ESP_AVRC_MD_ATTR_PLAYING_TIME)

_Static_assert(BOARD_MASTER_VOL_MAX >= 1 && BOARD_MASTER_VOL_MAX <= 127,
    "Master volume maximum must be 1..127");
_Static_assert(BOARD_MASTER_VOL_DEFAULT >= 0 && BOARD_MASTER_VOL_DEFAULT <= BOARD_MASTER_VOL_MAX,
    "Default master volume is out of range");
_Static_assert(BOARD_MASTER_VOL_STEP >= 1 && BOARD_MASTER_VOL_STEP <= BOARD_MASTER_VOL_MAX,
    "Master volume step is out of range");

typedef struct
{
    uint32_t rate;
    uint32_t epoch;
    uint32_t len;
    uint8_t data[PCM_BLOCK];
} pcm_block_t;

typedef struct
{
    uint8_t key;
    unsigned session;
} control_request_t;

typedef struct
{
    uint8_t tl;
    uint8_t key;
    uint8_t state;
    esp_avrc_rsp_t response;
    unsigned session;
} control_response_t;

static QueueHandle_t pcm_queue;
static QueueHandle_t state_queue;
static QueueHandle_t control_queue;
static QueueHandle_t response_queue;
static SemaphoreHandle_t state_lock;
static atomic_uint sample_rate;
static atomic_uint epoch;
static atomic_uint dropped;
static atomic_uint pcm_volume;
static atomic_bool audio_active;
static atomic_bool ready;
static speaker_state_t state;
static speaker_state_t published_state;
static unsigned session;
static bool volume_subscribed;
static bool play_status_supported;
static bool track_change_supported;
static bool track_notification_requested;
static bool metadata_requested;
static uint8_t metadata_pending;
static TickType_t metadata_started;
static char metadata_title[128];
static char metadata_artist[96];
static bool metadata_duration_known;
static uint32_t metadata_duration;
static bool play_query_pending;
static uint8_t volume_reported;
static TickType_t play_query_started;
static bool capabilities_requested;
static bool notification_requested;
static bool query_requested;
static bool scan_requested;
static bool volume_interim_pending;
static bool volume_local_pending;
static unsigned volume_subscription;

/* 持有此互斥锁时禁止调用 SDK，因为其 BTC 队列可能阻塞。 */
static void lock_state(void)
{
    xSemaphoreTake(state_lock, portMAX_DELAY);
}

static void unlock_state(void)
{
    xSemaphoreGive(state_lock);
}

static void publish_state(void)
{
    /* 所有本地及手机状态出口统一换算；应用只读档位和显示值。 */
    state.master_vol = (uint8_t)((state.volume * BOARD_MASTER_VOL_MAX + 63) / 127);
    state.volume_percent = (uint8_t)((state.volume * 100 + 63) / 127);
    if (memcmp(&state, &published_state, sizeof(state)) != 0)
    {
        published_state = state;
        /* 仅保留最新值，状态接收任务处理较慢时也不会阻塞蓝牙。 */
        xQueueOverwrite(state_queue, &state);
    }
}

esp_err_t speaker_service_get_state(speaker_state_t *out)
{
    if (!out)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (!atomic_load(&ready))
    {
        return ESP_ERR_INVALID_STATE;
    }
    lock_state();
    *out = state;
    unlock_state();
    return ESP_OK;
}

esp_err_t speaker_service_wait_state(speaker_state_t *out, uint32_t timeout_ms)
{
    if (!out)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (!atomic_load(&ready))
    {
        return ESP_ERR_INVALID_STATE;
    }
    TickType_t ticks = timeout_ms == UINT32_MAX ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xQueueReceive(state_queue, out, ticks) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

/* 调用方持有状态锁；不发送协议命令，避免在锁内阻塞蓝牙队列。 */
static void set_volume_locked(uint8_t volume)
{
    bool changed = state.volume != volume;
    state.volume = volume;
    atomic_store(&pcm_volume, volume);
    if (changed)
    {
        volume_local_pending = true;
    }
    publish_state();
}

esp_err_t speaker_service_set_volume(uint8_t volume)
{
    if (volume > 127)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (!atomic_load(&ready))
    {
        return ESP_ERR_INVALID_STATE;
    }
    lock_state();
    set_volume_locked(volume);
    unlock_state();
    return ESP_OK;
}

static uint8_t master_to_volume(int vol)
{
    return (uint8_t)((vol * 127 + BOARD_MASTER_VOL_MAX / 2) / BOARD_MASTER_VOL_MAX);
}

esp_err_t set_master_vol(int vol)
{
    if (vol < 0 || vol > BOARD_MASTER_VOL_MAX)
    {
        return ESP_ERR_INVALID_ARG;
    }
    return speaker_service_set_volume(master_to_volume(vol));
}

esp_err_t get_master_vol(uint8_t *vol)
{
    if (!vol)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (!atomic_load(&ready))
    {
        return ESP_ERR_INVALID_STATE;
    }
    lock_state();
    *vol = state.master_vol;
    unlock_state();
    return ESP_OK;
}

static esp_err_t adjust_master_vol(int delta)
{
    if (!atomic_load(&ready))
    {
        return ESP_ERR_INVALID_STATE;
    }
    /* 读取、加减及写入在同一把锁内，多个任务同时调节也不会丢步长。 */
    lock_state();
    int vol = state.master_vol + delta;
    if (vol < 0)
    {
        vol = 0;
    }
    else if (vol > BOARD_MASTER_VOL_MAX)
    {
        vol = BOARD_MASTER_VOL_MAX;
    }
    set_volume_locked(master_to_volume(vol));
    unlock_state();
    return ESP_OK;
}

esp_err_t master_vol_up(void)
{
    return adjust_master_vol(BOARD_MASTER_VOL_STEP);
}

esp_err_t master_vol_down(void)
{
    return adjust_master_vol(-BOARD_MASTER_VOL_STEP);
}

esp_err_t speaker_service_control(speaker_control_t control)
{
    if (control < SPEAKER_CONTROL_PLAY || control > SPEAKER_CONTROL_NEXT)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (!atomic_load(&ready))
    {
        return ESP_ERR_INVALID_STATE;
    }
    lock_state();
    esp_err_t err = speaker_control_resolve(&control, state.playback);
    if (err == ESP_OK && (state.connection != SPEAKER_CONNECTED || !state.avrcp_connected))
    {
        err = ESP_ERR_INVALID_STATE;
    }
    if (err == ESP_OK && state.control_busy)
    {
        err = ESP_ERR_NOT_FINISHED;
    }
    if (err == ESP_OK)
    {
        static const uint8_t keys[] = {
            [SPEAKER_CONTROL_PLAY] = ESP_AVRC_PT_CMD_PLAY,
            [SPEAKER_CONTROL_PAUSE] = ESP_AVRC_PT_CMD_PAUSE,
            [SPEAKER_CONTROL_PREVIOUS] = ESP_AVRC_PT_CMD_BACKWARD,
            [SPEAKER_CONTROL_NEXT] = ESP_AVRC_PT_CMD_FORWARD,
        };
        control_request_t request = {
            .key = keys[control],
            .session = session,
        };
        if (xQueueSend(control_queue, &request, 0) == pdTRUE)
        {
            state.control_busy = true;
            state.control_result = ESP_ERR_NOT_FINISHED;
            publish_state();
        }
        else
        {
            err = ESP_ERR_NOT_FINISHED;
        }
    }
    unlock_state();
    return err;
}

static bool control_connected(unsigned generation)
{
    return session == generation && state.avrcp_connected && state.connection == SPEAKER_CONNECTED;
}

static void query_play_status(void)
{
    query_requested = true;
}

/* SDK 文本不保证以零结尾；只复制容量内的合法 UTF-8 完整字符。
 * 遇到非法编码或嵌入零即结束，不能把半个汉字传给 UI。 */
static void speaker_copy_text(char *out, size_t capacity, const uint8_t *text, int length)
{
    size_t used = 0;
    while (text && length > 0 && used < (size_t)length && text[used])
    {
        uint8_t first = text[used];
        unsigned bytes = 0;
        if (first < 0x80)
        {
            bytes = 1;
        }
        else if (first >= 0xc2 && first <= 0xdf)
        {
            bytes = 2;
        }
        else if (first >= 0xe0 && first <= 0xef)
        {
            bytes = 3;
        }
        else if (first >= 0xf0 && first <= 0xf4)
        {
            bytes = 4;
        }
        if (!bytes || used + bytes >= capacity || used + bytes > (size_t)length)
        {
            break;
        }
        uint32_t point = first & (bytes == 1 ? 0x7f : (0x7f >> bytes));
        bool valid = true;
        for (unsigned i = 1; i < bytes; ++i)
        {
            uint8_t next = text[used + i];
            valid = valid && (next & 0xc0) == 0x80;
            point = (point << 6) | (next & 0x3f);
        }
        if (!valid || (bytes == 2 && point < 0x80) || (bytes == 3 && point < 0x800) ||
            (bytes == 4 && point < 0x10000) || (point >= 0xd800 && point <= 0xdfff) ||
            point > 0x10ffff)
        {
            break;
        }
        memcpy(out + used, text + used, bytes);
        used += bytes;
    }
    out[used] = '\0';
}

static bool speaker_parse_duration(const uint8_t *text, int length, uint32_t *value)
{
    if (!text || length <= 0 || length > 10)
    {
        return false;
    }
    uint64_t parsed = 0;
    for (int i = 0; i < length; ++i)
    {
        if (text[i] < '0' || text[i] > '9')
        {
            return false;
        }
        parsed = parsed * 10 + text[i] - '0';
    }
    if (parsed == 0 || parsed >= UINT32_MAX)
    {
        return false;
    }
    *value = (uint32_t)parsed;
    return true;
}

/* 属性分批回调，收齐或超时后一次合并；省略的歌手不会沿用上一首。 */
static void apply_metadata(void)
{
    if (strcmp(state.title, metadata_title) != 0)
    {
        state.position_known = false;
        state.duration_known = false;
        state.position_ms = 0;
        state.duration_ms = 0;
        query_play_status();
    }
    memcpy(state.title, metadata_title, sizeof(state.title));
    memcpy(state.artist, metadata_artist, sizeof(state.artist));
    if (metadata_duration_known)
    {
        state.duration_known = true;
        state.duration_ms = metadata_duration;
    }
}

/* 运行期 SDK 请求只由服务任务提交；回调仅复制状态并设置待处理标记。
 * SDK 队列满时，不会让回调或持有状态锁的界面任务陷入死锁。
 * 相同请求通过标记合并。 */
static void protocol_step(void)
{
    lock_state();
    unsigned generation = session;
    unsigned subscription = volume_subscription;
    bool scan = scan_requested;
    bool caps = state.avrcp_connected && capabilities_requested;
    bool notify = state.avrcp_connected && notification_requested && play_status_supported;
    bool track_notify =
        state.avrcp_connected && track_notification_requested && track_change_supported;
    TickType_t now = xTaskGetTickCount();
    bool expired = now - play_query_started >= pdMS_TO_TICKS(2000);
    bool query = state.avrcp_connected && state.connection == SPEAKER_CONNECTED &&
        (query_requested || now - play_query_started >= pdMS_TO_TICKS(1000)) &&
        (!play_query_pending || expired);
    if (metadata_pending && now - metadata_started >= pdMS_TO_TICKS(2000))
    {
        if (!metadata_requested)
        {
            apply_metadata();
        }
        metadata_pending = 0;
    }
    /* 曲目通知即时触发；10 秒轮询兜底，缺少属性的响应最多等 2 秒。
     * 元数据回调没有事务标签，串行请求期间曲目变更会丢弃旧响应。 */
    bool metadata = state.avrcp_connected && state.connection == SPEAKER_CONNECTED &&
        (metadata_requested || now - metadata_started >= pdMS_TO_TICKS(10000)) &&
        (!metadata_pending || now - metadata_started >= pdMS_TO_TICKS(2000));
    bool interim = state.volume_connected && volume_interim_pending;
    bool changed = state.volume_connected && volume_subscribed && volume_local_pending &&
        state.volume != volume_reported;
    uint8_t volume = state.volume;
    if (scan)
    {
        scan_requested = false;
    }
    if (caps)
    {
        capabilities_requested = false;
    }
    if (notify)
    {
        notification_requested = false;
    }
    if (track_notify)
    {
        track_notification_requested = false;
    }
    if (metadata)
    {
        metadata_requested = false;
        metadata_pending = MEDIA_ATTRIBUTES;
        memset(metadata_title, 0, sizeof(metadata_title));
        memset(metadata_artist, 0, sizeof(metadata_artist));
        metadata_duration_known = false;
        metadata_started = now;
    }
    if (query)
    {
        query_requested = false;
        play_query_pending = true;
        play_query_started = now;
    }
    if (interim)
    {
        volume_interim_pending = false;
    }
    unlock_state();

    esp_err_t scan_err =
        scan ? esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE) : ESP_OK;
    esp_err_t caps_err = caps ? esp_avrc_ct_send_get_rn_capabilities_cmd(TL_CAPABILITIES) : ESP_OK;
    esp_err_t notify_err = notify ? esp_avrc_ct_send_register_notification_cmd(
                                        TL_NOTIFICATION, ESP_AVRC_RN_PLAY_STATUS_CHANGE, 0)
                                  : ESP_OK;
    esp_err_t track_err = track_notify
        ? esp_avrc_ct_send_register_notification_cmd(TL_TRACK_CHANGE, ESP_AVRC_RN_TRACK_CHANGE, 0)
        : ESP_OK;
    esp_err_t metadata_err =
        metadata ? esp_avrc_ct_send_metadata_cmd(TL_METADATA, MEDIA_ATTRIBUTES) : ESP_OK;
    esp_err_t query_err = query ? esp_avrc_ct_send_get_play_status_cmd(TL_PLAY_STATUS) : ESP_OK;
    esp_avrc_rn_param_t param = {.volume = volume};
    esp_err_t volume_err = (interim || changed)
        ? esp_avrc_tg_send_rn_rsp(ESP_AVRC_RN_VOLUME_CHANGE,
              interim ? ESP_AVRC_RN_RSP_INTERIM : ESP_AVRC_RN_RSP_CHANGED,
              &param)
        : ESP_OK;

    lock_state();
    if (scan_err != ESP_OK)
    {
        scan_requested = true;
    }
    if (generation == session)
    {
        if (caps_err != ESP_OK)
        {
            capabilities_requested = true;
        }
        if (notify_err != ESP_OK)
        {
            notification_requested = true;
        }
        if (track_err != ESP_OK)
        {
            track_notification_requested = true;
        }
        if (metadata_err != ESP_OK)
        {
            metadata_pending = 0;
            metadata_requested = true;
        }
        if (query_err != ESP_OK)
        {
            play_query_pending = false;
            query_requested = true;
        }
    }
    if ((interim || changed) && subscription == volume_subscription && state.volume_connected)
    {
        state.volume_sync_result = volume_err;
        if (volume_err == ESP_OK)
        {
            volume_subscribed = interim;
            /* 后续手机更新不回发通知；后续本地更新保持待处理，
             * 最终上报最新音量。 */
            volume_reported = volume_local_pending ? volume : state.volume;
            if (state.volume == volume)
            {
                volume_local_pending = false;
            }
        }
        else if (interim)
        {
            volume_interim_pending = true;
        }
    }
    publish_state();
    unlock_state();
}

/* 只有此任务发送 AVRCP 透传命令，按下与释放成对处理。
 * 事务标签 5～15 循环使用，0～4 保留给能力、状态和媒体操作。 */
static esp_err_t send_key(const control_request_t *request, uint8_t key_state)
{
    static uint8_t next_tl = 5;
    lock_state();
    if (!control_connected(request->session))
    {
        unlock_state();
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t tl = next_tl++;
    if (next_tl > 15)
    {
        next_tl = 5;
    }
    unlock_state();
    esp_err_t err = esp_avrc_ct_send_passthrough_cmd(tl, request->key, key_state);
    if (err != ESP_OK)
    {
        return err;
    }
    TickType_t start = xTaskGetTickCount();
    while (xTaskGetTickCount() - start < pdMS_TO_TICKS(CONTROL_TIMEOUT_MS))
    {
        protocol_step();
        control_response_t response;
        if (xQueueReceive(response_queue, &response, pdMS_TO_TICKS(20)) == pdTRUE &&
            response.session == request->session && response.tl == tl &&
            response.key == request->key && response.state == key_state)
        {
            return response.response == ESP_AVRC_RSP_ACCEPT ? ESP_OK : ESP_ERR_NOT_SUPPORTED;
        }
        lock_state();
        bool connected = control_connected(request->session);
        unlock_state();
        if (!connected)
        {
            return ESP_ERR_INVALID_STATE;
        }
    }
    return ESP_ERR_TIMEOUT;
}

static void execute_control(const control_request_t *request)
{
    esp_err_t press = send_key(request, ESP_AVRC_PT_CMD_STATE_PRESSED);
    /* 按下被拒绝或超时后仍尝试释放；连接已断开时取消发送。 */
    esp_err_t release = send_key(request, ESP_AVRC_PT_CMD_STATE_RELEASED);
    lock_state();
    state.control_busy = false;
    state.control_result = press != ESP_OK ? press : release;
    if (control_connected(request->session))
    {
        query_play_status();
    }
    publish_state();
    unlock_state();
}

static void control_worker(void *arg)
{
    control_request_t request;
    for (;;)
    {
        protocol_step();
        if (xQueueReceive(control_queue, &request, pdMS_TO_TICKS(20)) == pdTRUE)
        {
            execute_control(&request);
        }
    }
}

static void pcm_cb(const uint8_t *data, uint32_t len)
{
    uint32_t generation = atomic_load(&epoch);
    uint32_t hz = atomic_load(&sample_rate);
    if (generation != atomic_load(&epoch) || !atomic_load(&audio_active))
    {
        return;
    }
    if (!hz || !data || len % 4)
    {
        return;
    }
    while (len)
    {
        /* IDF 串行调用此回调；使用静态缓冲区，避免占用约 2KB 的回调栈空间。 */
        static pcm_block_t block;
        block.rate = hz;
        block.epoch = generation;
        block.len = len > PCM_BLOCK ? PCM_BLOCK : len;
        memcpy(block.data, data, block.len);
        if (xQueueSend(pcm_queue, &block, 0) != pdTRUE)
        {
            atomic_fetch_add(&dropped, 1);
            return;
        }
        data += block.len;
        len -= block.len;
    }
}

static void audio_worker(void *arg)
{
    pcm_block_t block;
    for (;;)
    {
        if (xQueueReceive(pcm_queue, &block, pdMS_TO_TICKS(1000)) == pdTRUE &&
            block.epoch == atomic_load(&epoch) && atomic_load(&audio_active))
        {
            speaker_pcm_scale(block.data, block.len, atomic_load(&pcm_volume));
            ESP_ERROR_CHECK(audio_output_set_rate(block.rate));
            esp_err_t err = audio_output_write(block.data, block.len);
            if (err != ESP_OK)
            {
                ESP_LOGW("audio", "I2S write: %s", esp_err_to_name(err));
            }
        }
        unsigned lost = atomic_exchange(&dropped, 0);
        if (lost)
        {
            ESP_LOGW("audio", "PCM overflow: %u blocks", lost);
        }
    }
}

static speaker_playback_t playback_status(esp_avrc_playback_stat_t status)
{
    switch (status)
    {
    case ESP_AVRC_PLAYBACK_STOPPED:
        return SPEAKER_PLAYBACK_STOPPED;
    case ESP_AVRC_PLAYBACK_PLAYING:
        return SPEAKER_PLAYBACK_PLAYING;
    case ESP_AVRC_PLAYBACK_PAUSED:
        return SPEAKER_PLAYBACK_PAUSED;
    case ESP_AVRC_PLAYBACK_FWD_SEEK:
        return SPEAKER_PLAYBACK_FORWARD_SEEK;
    case ESP_AVRC_PLAYBACK_REV_SEEK:
        return SPEAKER_PLAYBACK_REVERSE_SEEK;
    case ESP_AVRC_PLAYBACK_ERROR:
        return SPEAKER_PLAYBACK_ERROR;
    default:
        return SPEAKER_PLAYBACK_UNKNOWN;
    }
}

static void register_play_notification(void)
{
    notification_requested = state.avrcp_connected && play_status_supported;
}

static void avrc_ct_cb(esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t *p)
{
    lock_state();
    switch (event)
    {
    case ESP_AVRC_CT_CONNECTION_STATE_EVT:
        if (state.avrcp_connected == p->conn_stat.connected)
        {
            break;
        }
        state.avrcp_connected = p->conn_stat.connected;
        ++session;
        play_status_supported = false;
        track_change_supported = false;
        track_notification_requested = false;
        metadata_pending = 0;
        metadata_requested = state.avrcp_connected;
        speaker_state_clear_media(&state);
        play_query_pending = false;
        notification_requested = false;
        capabilities_requested = state.avrcp_connected;
        if (state.avrcp_connected)
        {
            query_play_status();
        }
        else
        {
            state.playback = state.connection == SPEAKER_CONNECTED && state.streaming
                ? SPEAKER_PLAYBACK_PLAYING
                : SPEAKER_PLAYBACK_UNKNOWN;
        }
        break;
    case ESP_AVRC_CT_GET_RN_CAPABILITIES_RSP_EVT:
        if (!state.avrcp_connected)
        {
            break;
        }
        track_change_supported = esp_avrc_rn_evt_bit_mask_operation(
            ESP_AVRC_BIT_MASK_OP_TEST, &p->get_rn_caps_rsp.evt_set, ESP_AVRC_RN_TRACK_CHANGE);
        track_notification_requested = track_change_supported;
        play_status_supported = esp_avrc_rn_evt_bit_mask_operation(
            ESP_AVRC_BIT_MASK_OP_TEST, &p->get_rn_caps_rsp.evt_set, ESP_AVRC_RN_PLAY_STATUS_CHANGE);
        register_play_notification();
        break;
    case ESP_AVRC_CT_PLAY_STATUS_RSP_EVT:
        if (!state.avrcp_connected || state.connection != SPEAKER_CONNECTED || !play_query_pending)
        {
            break;
        }
        play_query_pending = false;
        if (query_requested)
        {
            /* 曲目刚切换，丢弃切换前尚未完成的查询，稍后重新请求。 */
            break;
        }
        speaker_state_playback(&state, playback_status(p->play_status_rsp.play_status));
        state.position_known = p->play_status_rsp.song_position != UINT32_MAX;
        state.position_ms = state.position_known ? p->play_status_rsp.song_position : 0;
        if (p->play_status_rsp.song_length != 0 && p->play_status_rsp.song_length != UINT32_MAX)
        {
            state.duration_known = true;
            state.duration_ms = p->play_status_rsp.song_length;
        }
        /* 状态响应未给时长时，保留本曲目已取得的元数据时长；切歌会统一清空。 */
        break;
    case ESP_AVRC_CT_METADATA_RSP_EVT:
        if (!state.avrcp_connected || state.connection != SPEAKER_CONNECTED ||
            !(metadata_pending & p->meta_rsp.attr_id))
        {
            break;
        }
        metadata_pending &= ~p->meta_rsp.attr_id;
        if (metadata_requested)
        {
            /* 上一首的剩余属性仍会回调，但不能写进新曲目的快照。 */
            break;
        }
        if (p->meta_rsp.attr_id == ESP_AVRC_MD_ATTR_TITLE)
        {
            speaker_copy_text(metadata_title,
                sizeof(metadata_title),
                p->meta_rsp.attr_text,
                p->meta_rsp.attr_length);
        }
        else if (p->meta_rsp.attr_id == ESP_AVRC_MD_ATTR_ARTIST)
        {
            speaker_copy_text(metadata_artist,
                sizeof(metadata_artist),
                p->meta_rsp.attr_text,
                p->meta_rsp.attr_length);
        }
        else if (p->meta_rsp.attr_id == ESP_AVRC_MD_ATTR_PLAYING_TIME)
        {
            metadata_duration_known = speaker_parse_duration(
                p->meta_rsp.attr_text, p->meta_rsp.attr_length, &metadata_duration);
        }
        if (!metadata_pending)
        {
            apply_metadata();
        }
        break;
    case ESP_AVRC_CT_CHANGE_NOTIFY_EVT:
        if (!state.avrcp_connected || state.connection != SPEAKER_CONNECTED)
        {
            break;
        }
        if (p->change_ntf.event_id == ESP_AVRC_RN_TRACK_CHANGE)
        {
            speaker_state_clear_media(&state);
            metadata_requested = true;
            query_play_status();
            track_notification_requested = track_change_supported;
        }
        else if (p->change_ntf.event_id == ESP_AVRC_RN_PLAY_STATUS_CHANGE)
        {
            speaker_state_playback(&state, playback_status(p->change_ntf.event_parameter.playback));
            register_play_notification();
        }
        break;
    case ESP_AVRC_CT_PASSTHROUGH_RSP_EVT:
    {
        control_response_t response = {
            .tl = p->psth_rsp.tl,
            .key = p->psth_rsp.key_code,
            .state = p->psth_rsp.key_state,
            .response = p->psth_rsp.rsp_code,
            .session = session,
        };
        if (xQueueSend(response_queue, &response, 0) != pdTRUE)
        {
            ESP_LOGW("avrcp", "Response mailbox full; control will time out");
        }
        break;
    }
    case ESP_AVRC_CT_PROF_STATE_EVT:
        if (p->avrc_ct_init_stat.state != ESP_AVRC_INIT_SUCCESS)
        {
            ESP_LOGE("avrcp", "CT profile state: %d", p->avrc_ct_init_stat.state);
        }
        break;
    default:
        break;
    }
    publish_state();
    unlock_state();
}

static void avrc_tg_cb(esp_avrc_tg_cb_event_t event, esp_avrc_tg_cb_param_t *p)
{
    lock_state();
    switch (event)
    {
    case ESP_AVRC_TG_CONNECTION_STATE_EVT:
        state.volume_connected = p->conn_stat.connected;
        state.volume_sync_result = ESP_OK;
        volume_subscribed = false;
        volume_interim_pending = false;
        ++volume_subscription;
        break;
    case ESP_AVRC_TG_SET_ABSOLUTE_VOLUME_CMD_EVT:
        if (state.volume_connected && p->set_abs_vol.volume <= 127)
        {
            state.volume = p->set_abs_vol.volume;
            volume_reported = state.volume;
            volume_local_pending = false;
            atomic_store(&pcm_volume, state.volume);
            /* SDK 负责回复绝对音量设置命令；不要回发手机发起的音量更新。 */
        }
        break;
    case ESP_AVRC_TG_REGISTER_NOTIFICATION_EVT:
        if (state.volume_connected && p->reg_ntf.event_id == ESP_AVRC_RN_VOLUME_CHANGE)
        {
            ++volume_subscription;
            volume_subscribed = false;
            volume_interim_pending = true;
        }
        break;
    case ESP_AVRC_TG_PROF_STATE_EVT:
        if (p->avrc_tg_init_stat.state != ESP_AVRC_INIT_SUCCESS)
        {
            ESP_LOGE("avrcp", "TG profile state: %d", p->avrc_tg_init_stat.state);
        }
        break;
    default:
        break;
    }
    publish_state();
    unlock_state();
}

static void a2dp_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *p)
{
    lock_state();
    if (event == ESP_A2D_AUDIO_CFG_EVT)
    {
        atomic_store(&sample_rate, 0);
        atomic_fetch_add(&epoch, 1);
        uint32_t hz = 0;
        if (p->audio_cfg.mcc.type == ESP_A2D_MCT_SBC)
        {
            esp_a2d_cie_sbc_t cfg = p->audio_cfg.mcc.cie.sbc_info;
            if (!(cfg.ch_mode & ESP_A2D_SBC_CIE_CH_MODE_MONO))
            {
                if (cfg.samp_freq & ESP_A2D_SBC_CIE_SF_48K)
                {
                    hz = 48000;
                }
                else if (cfg.samp_freq & ESP_A2D_SBC_CIE_SF_44K)
                {
                    hz = 44100;
                }
                else if (cfg.samp_freq & ESP_A2D_SBC_CIE_SF_32K)
                {
                    hz = 32000;
                }
                else if (cfg.samp_freq & ESP_A2D_SBC_CIE_SF_16K)
                {
                    hz = 16000;
                }
            }
        }
        atomic_store(&sample_rate, hz);
        state.sample_rate_hz = hz;
        ESP_LOGI("a2dp", "Stereo PCM16: %lu Hz (0 = unsupported)", (unsigned long)hz);
    }
    else if (event == ESP_A2D_CONNECTION_STATE_EVT)
    {
        static const speaker_connection_t connections[] = {
            [ESP_A2D_CONNECTION_STATE_DISCONNECTED] = SPEAKER_DISCONNECTED,
            [ESP_A2D_CONNECTION_STATE_CONNECTING] = SPEAKER_CONNECTING,
            [ESP_A2D_CONNECTION_STATE_CONNECTED] = SPEAKER_CONNECTED,
            [ESP_A2D_CONNECTION_STATE_DISCONNECTING] = SPEAKER_DISCONNECTING,
        };
        if ((unsigned)p->conn_stat.state < sizeof(connections) / sizeof(connections[0]))
        {
            speaker_state_connection(&state, connections[p->conn_stat.state]);
        }
        if (state.connection != SPEAKER_CONNECTED)
        {
            atomic_store(&audio_active, false);
            atomic_store(&sample_rate, 0);
            atomic_fetch_add(&epoch, 1);
            ++session;
            metadata_pending = 0;
            metadata_requested = false;
            track_notification_requested = false;
            if (state.connection == SPEAKER_DISCONNECTED)
            {
                volume_subscribed = false;
                volume_interim_pending = false;
                state.volume_sync_result = ESP_OK;
                ++volume_subscription;
                play_status_supported = false;
                play_query_pending = false;
            }
        }
        else
        {
            query_play_status();
            metadata_requested = state.avrcp_connected;
            track_notification_requested = state.avrcp_connected && track_change_supported;
        }
        ESP_LOGI("a2dp", "Connection: %d", p->conn_stat.state);
    }
    else if (event == ESP_A2D_PROF_STATE_EVT)
    {
        if (p->a2d_prof_stat.init_state == ESP_A2D_INIT_SUCCESS)
        {
            scan_requested = true;
        }
        else
        {
            ESP_LOGE("a2dp", "Profile state: %d", p->a2d_prof_stat.init_state);
        }
    }
    else if (event == ESP_A2D_AUDIO_STATE_EVT)
    {
        bool started = p->audio_stat.state == ESP_A2D_AUDIO_STATE_STARTED;
        atomic_store(&audio_active, false);
        atomic_fetch_add(&epoch, 1);
        speaker_state_audio(&state, started);
        atomic_store(&audio_active, state.streaming);
        ESP_LOGI("a2dp", "Audio state: %d", p->audio_stat.state);
    }
    publish_state();
    unlock_state();
}

esp_err_t speaker_service_start(void)
{
    if (state_lock)
    {
        return ESP_ERR_INVALID_STATE;
    }
    ESP_RETURN_ON_ERROR(nvs_flash_init(), "bt", "NVS init; inspect before erase");
    state_lock = xSemaphoreCreateMutex();
    pcm_queue = xQueueCreate(PCM_DEPTH, sizeof(pcm_block_t));
    state_queue = xQueueCreate(1, sizeof(speaker_state_t));
    control_queue = xQueueCreate(1, sizeof(control_request_t));
    response_queue = xQueueCreate(4, sizeof(control_response_t));
    if (!state_lock || !pcm_queue || !state_queue || !control_queue || !response_queue)
    {
        return ESP_ERR_NO_MEM;
    }
    state.volume = master_to_volume(BOARD_MASTER_VOL_DEFAULT);
    atomic_store(&pcm_volume, state.volume);
    publish_state();
    /* 默认档为零时快照可能与静态初值相同，仍保证首个消费者有初始状态。 */
    xQueueOverwrite(state_queue, &state);
    if (xTaskCreate(audio_worker, "audio_tx", 4096, NULL, 5, NULL) != pdPASS ||
        xTaskCreate(control_worker, "speaker_ctl", 3072, NULL, 4, NULL) != pdPASS)
    {
        return ESP_ERR_NO_MEM;
    }
    ESP_RETURN_ON_ERROR(esp_bt_controller_mem_release(ESP_BT_MODE_BLE), "bt", "release BLE");
    esp_bt_controller_config_t cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_bt_controller_init(&cfg), "bt", "controller");
    ESP_RETURN_ON_ERROR(esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT), "bt", "enable");
    ESP_RETURN_ON_ERROR(esp_bluedroid_init(), "bt", "bluedroid");
    ESP_RETURN_ON_ERROR(esp_bluedroid_enable(), "bt", "host");
    esp_bt_io_cap_t cap = ESP_BT_IO_CAP_NONE;
    ESP_RETURN_ON_ERROR(
        esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE, &cap, sizeof(cap)), "bt", "pairing");
    ESP_RETURN_ON_ERROR(esp_bt_gap_set_device_name(BOARD_BT_NAME), "bt", "name");
    ESP_RETURN_ON_ERROR(esp_avrc_ct_init(), "bt", "AVRCP CT");
    ESP_RETURN_ON_ERROR(esp_avrc_ct_register_callback(avrc_ct_cb), "bt", "AVRCP CT events");
    ESP_RETURN_ON_ERROR(esp_avrc_tg_init(), "bt", "AVRCP TG");
    ESP_RETURN_ON_ERROR(esp_avrc_tg_register_callback(avrc_tg_cb), "bt", "AVRCP TG events");
    esp_avrc_rn_evt_cap_mask_t events = {0};
    esp_avrc_rn_evt_bit_mask_operation(
        ESP_AVRC_BIT_MASK_OP_SET, &events, ESP_AVRC_RN_VOLUME_CHANGE);
    ESP_RETURN_ON_ERROR(esp_avrc_tg_set_rn_evt_cap(&events), "bt", "AVRCP volume capability");
    ESP_RETURN_ON_ERROR(esp_a2d_register_callback(a2dp_cb), "bt", "events");
    ESP_RETURN_ON_ERROR(esp_a2d_sink_init(), "bt", "sink");
    ESP_RETURN_ON_ERROR(esp_a2d_sink_register_data_callback(pcm_cb), "bt", "PCM callback");
    atomic_store(&ready, true);
    return ESP_OK;
}
