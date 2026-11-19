#include <string.h>
#include <stdatomic.h>
#include "speaker_service.h"
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
    bool changed = state.volume != volume;
    state.volume = volume;
    atomic_store(&pcm_volume, volume);
    if (changed)
    {
        volume_local_pending = true;
    }
    if (changed)
    {
        publish_state();
    }
    unlock_state();
    return ESP_OK;
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
    TickType_t now = xTaskGetTickCount();
    bool expired = now - play_query_started >= pdMS_TO_TICKS(2000);
    bool query = state.avrcp_connected && state.connection == SPEAKER_CONNECTED &&
        (query_requested || expired) && (!play_query_pending || expired);
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
 * 事务标签 3～15 循环使用，0～2 保留给能力查询和状态操作。 */
static esp_err_t send_key(const control_request_t *request, uint8_t key_state)
{
    static uint8_t next_tl = 3;
    lock_state();
    if (!control_connected(request->session))
    {
        unlock_state();
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t tl = next_tl++;
    if (next_tl > 15)
    {
        next_tl = 3;
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
        play_status_supported = esp_avrc_rn_evt_bit_mask_operation(
            ESP_AVRC_BIT_MASK_OP_TEST, &p->get_rn_caps_rsp.evt_set, ESP_AVRC_RN_PLAY_STATUS_CHANGE);
        register_play_notification();
        break;
    case ESP_AVRC_CT_PLAY_STATUS_RSP_EVT:
        play_query_pending = false;
        speaker_state_playback(&state, playback_status(p->play_status_rsp.play_status));
        break;
    case ESP_AVRC_CT_CHANGE_NOTIFY_EVT:
        if (p->change_ntf.event_id == ESP_AVRC_RN_PLAY_STATUS_CHANGE)
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
    state.volume = BOARD_INITIAL_VOLUME;
    atomic_store(&pcm_volume, state.volume);
    xQueueOverwrite(state_queue, &state);
    published_state = state;
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
