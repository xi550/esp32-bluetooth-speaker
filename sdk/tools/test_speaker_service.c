/* Compile the real service; only transport and scheduling are fake. */
#include "board_esp_proj_cfg.h"
#ifdef TEST_MASTER_MAX
#undef BOARD_MASTER_VOL_MAX
#undef BOARD_MASTER_VOL_DEFAULT
#undef BOARD_MASTER_VOL_STEP
#define BOARD_MASTER_VOL_MAX TEST_MASTER_MAX
#define BOARD_MASTER_VOL_DEFAULT 0
#define BOARD_MASTER_VOL_STEP TEST_MASTER_STEP
#endif
#include "../apps/esp_app/interface/speaker_service.c"
#include <limits.h>

struct fake_queue { unsigned depth, size, count; uint8_t data[12 * sizeof(pcm_block_t)]; };
static struct fake_queue queues[4];
static unsigned queue_count, lock_depth, sent_keys, status_queries, subscriptions, volume_replies;
static TickType_t ticks;
static bool respond = true, disconnect_on_wait, response_pending;
static esp_avrc_rsp_t key_response = ESP_AVRC_RSP_ACCEPT;
static esp_err_t volume_error = ESP_OK;
static esp_avrc_ct_cb_param_t pending_response;
static esp_avrc_rn_rsp_t last_volume_response;
static uint8_t last_volume, last_key, last_key_state;
static unsigned metadata_queries, track_subscriptions;
static esp_err_t metadata_error, track_error;
esp_err_t esp_avrc_ct_send_metadata_cmd(uint8_t tl, uint8_t mask)
{
    assert(!lock_depth && tl == TL_METADATA && mask == MEDIA_ATTRIBUTES);
    ++metadata_queries;
    return metadata_error;
}

QueueHandle_t xQueueCreate(unsigned depth, unsigned size)
{
    assert(queue_count < 4 && depth * size <= sizeof(queues[0].data));
    struct fake_queue *q = &queues[queue_count++];
    q->depth = depth; q->size = size;
    return q;
}
int xQueueSend(QueueHandle_t q, const void *data, TickType_t timeout)
{
    if (q->count == q->depth) return 0;
    memcpy(q->data + q->count++ * q->size, data, q->size);
    return pdTRUE;
}
int xQueueOverwrite(QueueHandle_t q, const void *data)
{
    assert(lock_depth == 1); /* 运行期快照发布必须在状态锁内。 */
    assert(q->depth == 1);
    memcpy(q->data, data, q->size); q->count = 1;
    return pdTRUE;
}
int xQueueReceive(QueueHandle_t q, void *data, TickType_t timeout)
{
    if (q == response_queue) {
        assert(!lock_depth); /* No mutex held while waiting for Bluetooth. */
        if (disconnect_on_wait) {
            disconnect_on_wait = false;
            esp_a2d_cb_param_t p = {0};
            p.conn_stat.state = ESP_A2D_CONNECTION_STATE_DISCONNECTED;
            a2dp_cb(ESP_A2D_CONNECTION_STATE_EVT, &p);
        }
        if (response_pending) {
            response_pending = false;
            avrc_ct_cb(ESP_AVRC_CT_PASSTHROUGH_RSP_EVT, &pending_response);
        }
    }
    if (!q->count) { ticks += timeout; return 0; }
    memcpy(data, q->data, q->size);
    --q->count;
    memmove(q->data, q->data + q->size, q->count * q->size);
    return pdTRUE;
}
int xSemaphoreTake(SemaphoreHandle_t mutex, TickType_t timeout) { assert(!lock_depth++); return pdTRUE; }
int xSemaphoreGive(SemaphoreHandle_t mutex) { assert(lock_depth-- == 1); return pdTRUE; }
TickType_t xTaskGetTickCount(void) { return ticks; }
esp_err_t esp_bt_gap_set_scan_mode(esp_bt_connection_mode_t c, esp_bt_discovery_mode_t d) { assert(!lock_depth); return ESP_OK; }
esp_err_t esp_avrc_ct_send_get_rn_capabilities_cmd(uint8_t tl) { assert(!lock_depth && tl == TL_CAPABILITIES); return ESP_OK; }
esp_err_t esp_avrc_ct_send_get_play_status_cmd(uint8_t tl) { assert(!lock_depth); ++status_queries; return ESP_OK; }
esp_err_t esp_avrc_ct_send_register_notification_cmd(uint8_t tl, uint8_t event, uint32_t param)
{
    assert(!lock_depth);
    if (event == ESP_AVRC_RN_TRACK_CHANGE) {
        assert(tl == TL_TRACK_CHANGE);
        ++track_subscriptions;
        return track_error;
    }
    assert(tl == TL_NOTIFICATION && event == ESP_AVRC_RN_PLAY_STATUS_CHANGE);
    ++subscriptions; return ESP_OK;
}
bool esp_avrc_rn_evt_bit_mask_operation(esp_avrc_bit_mask_op_t op, esp_avrc_rn_evt_cap_mask_t *mask,
                                       esp_avrc_rn_event_ids_t event)
{
    if (op == ESP_AVRC_BIT_MASK_OP_SET) mask->bits |= 1u << event;
    return (mask->bits & (1u << event)) != 0;
}
esp_err_t esp_avrc_tg_send_rn_rsp(esp_avrc_rn_event_ids_t event, esp_avrc_rn_rsp_t rsp, esp_avrc_rn_param_t *p)
{
    assert(!lock_depth && event == ESP_AVRC_RN_VOLUME_CHANGE);
    ++volume_replies; last_volume = p->volume; last_volume_response = rsp;
    return volume_error;
}
esp_err_t esp_avrc_ct_send_passthrough_cmd(uint8_t tl, uint8_t key, uint8_t key_state)
{
    assert(!lock_depth && tl >= 5 && tl <= 15);
    ++sent_keys; last_key = key; last_key_state = key_state;
    pending_response.psth_rsp.tl = tl;
    pending_response.psth_rsp.key_code = key;
    pending_response.psth_rsp.key_state = key_state;
    pending_response.psth_rsp.rsp_code = key_response;
    response_pending = respond;
    return ESP_OK;
}

static void connection(esp_a2d_connection_state_t value)
{
    esp_a2d_cb_param_t p = {0}; p.conn_stat.state = value;
    a2dp_cb(ESP_A2D_CONNECTION_STATE_EVT, &p);
}
static void ct_connection(bool connected)
{
    esp_avrc_ct_cb_param_t p = {0}; p.conn_stat.connected = connected;
    avrc_ct_cb(ESP_AVRC_CT_CONNECTION_STATE_EVT, &p);
}
static void playback(esp_avrc_playback_stat_t value)
{
    esp_avrc_ct_cb_param_t p = {0};
    p.change_ntf.event_id = ESP_AVRC_RN_PLAY_STATUS_CHANGE;
    p.change_ntf.event_parameter.playback = value;
    avrc_ct_cb(ESP_AVRC_CT_CHANGE_NOTIFY_EVT, &p);
}
static void audio(bool started)
{
    esp_a2d_cb_param_t p = {0}; p.audio_stat.state = started ? ESP_A2D_AUDIO_STATE_STARTED : ESP_A2D_AUDIO_STATE_SUSPEND;
    a2dp_cb(ESP_A2D_AUDIO_STATE_EVT, &p);
}
static void run_control(void)
{
    control_request_t request;
    assert(xQueueReceive(control_queue, &request, 0) == pdTRUE);
    execute_control(&request);
    assert(!state.control_busy);
}

/* 档位封装直接运行生产实现，手机协议数值仍保持原精度。 */
static void check_master_volumes(void)
{
    uint8_t vol;
    assert(get_master_vol(NULL) == ESP_ERR_INVALID_ARG);
    for (int value = 0; value <= BOARD_MASTER_VOL_MAX; ++value)
    {
        assert(set_master_vol(value) == ESP_OK);
        assert(get_master_vol(&vol) == ESP_OK && vol == value);
        assert(state.master_vol == value && !lock_depth);
        assert(state.volume == (value * 127 + BOARD_MASTER_VOL_MAX / 2) / BOARD_MASTER_VOL_MAX);
        assert(state.volume_percent == (state.volume * 100 + 63) / 127);
        assert(atomic_load(&pcm_volume) == state.volume);
    }
    unsigned old = state.volume;
    assert(set_master_vol(-1) == ESP_ERR_INVALID_ARG);
    assert(set_master_vol(BOARD_MASTER_VOL_MAX + 1) == ESP_ERR_INVALID_ARG);
    assert(set_master_vol(INT_MIN) == ESP_ERR_INVALID_ARG);
    assert(set_master_vol(INT_MAX) == ESP_ERR_INVALID_ARG && state.volume == old);
    for (int value = 0; value <= BOARD_MASTER_VOL_MAX; ++value)
    {
        assert(set_master_vol(value) == ESP_OK && master_vol_up() == ESP_OK);
        int expected = value + BOARD_MASTER_VOL_STEP;
        if (expected > BOARD_MASTER_VOL_MAX) expected = BOARD_MASTER_VOL_MAX;
        assert(state.master_vol == expected && !lock_depth);
        assert(set_master_vol(value) == ESP_OK && master_vol_down() == ESP_OK);
        expected = value - BOARD_MASTER_VOL_STEP;
        if (expected < 0) expected = 0;
        assert(state.master_vol == expected && !lock_depth);
    }
    assert(set_master_vol(0) == ESP_OK && master_vol_down() == ESP_OK && state.volume == 0);
    assert(set_master_vol(BOARD_MASTER_VOL_MAX) == ESP_OK && master_vol_up() == ESP_OK && state.volume == 127);
    esp_avrc_tg_cb_param_t tg = {0};
    tg.conn_stat.connected = true;
    avrc_tg_cb(ESP_AVRC_TG_CONNECTION_STATE_EVT, &tg);
    for (int raw = 0; raw <= 127; ++raw)
    {
        tg.set_abs_vol.volume = raw;
        avrc_tg_cb(ESP_AVRC_TG_SET_ABSOLUTE_VOLUME_CMD_EVT, &tg);
        int mapped = (raw * BOARD_MASTER_VOL_MAX + 63) / 127;
        assert(state.volume == raw && state.master_vol == mapped);
        assert(state.volume_percent == (raw * 100 + 63) / 127 && !volume_local_pending);
        assert(get_master_vol(&vol) == ESP_OK && vol == mapped);
        assert(master_vol_up() == ESP_OK);
        int expected = mapped + BOARD_MASTER_VOL_STEP;
        if (expected > BOARD_MASTER_VOL_MAX) expected = BOARD_MASTER_VOL_MAX;
        assert(state.master_vol == expected);
        avrc_tg_cb(ESP_AVRC_TG_SET_ABSOLUTE_VOLUME_CMD_EVT, &tg);
        assert(master_vol_down() == ESP_OK);
        expected = mapped - BOARD_MASTER_VOL_STEP;
        if (expected < 0) expected = 0;
        assert(state.master_vol == expected);
    }
    tg.conn_stat.connected = false;
    avrc_tg_cb(ESP_AVRC_TG_CONNECTION_STATE_EVT, &tg);
}

static void metadata_text(uint8_t attr, const char *text, int length)
{
    esp_avrc_ct_cb_param_t p = {0};
    p.meta_rsp.attr_id = attr;
    p.meta_rsp.attr_text = (uint8_t *)text;
    p.meta_rsp.attr_length = length;
    avrc_ct_cb(ESP_AVRC_CT_METADATA_RSP_EVT, &p);
}

static void media_status(uint32_t position, uint32_t duration, esp_avrc_playback_stat_t status)
{
    esp_avrc_ct_cb_param_t p = {0};
    p.play_status_rsp.song_position = position;
    p.play_status_rsp.song_length = duration;
    p.play_status_rsp.play_status = status;
    avrc_ct_cb(ESP_AVRC_CT_PLAY_STATUS_RSP_EVT, &p);
}

static void check_media(void)
{
    connection(ESP_A2D_CONNECTION_STATE_CONNECTED);
    ct_connection(true);
    metadata_error = ESP_FAIL;
    protocol_step();
    assert(metadata_requested && !metadata_pending);
    metadata_error = ESP_OK;
    unsigned requests = metadata_queries;
    protocol_step();
    assert(metadata_queries == requests + 1 && metadata_pending == MEDIA_ATTRIBUTES);
    char title[] = "中文音乐";
    metadata_text(ESP_AVRC_MD_ATTR_TITLE, title, sizeof(title)-1);
    memset(title, 'x', sizeof(title)-1); /* SDK 借用内存必须已经复制。 */
    assert(state.title[0] == 0); /* 分批属性收齐后才发布。 */
    metadata_text(ESP_AVRC_MD_ATTR_ARTIST, "歌手", 6);
    metadata_text(ESP_AVRC_MD_ATTR_PLAYING_TIME, "180000", 6);
    assert(strcmp(state.title,"中文音乐") == 0 && strcmp(state.artist,"歌手") == 0);
    assert(state.duration_known && state.duration_ms == 180000 && !metadata_pending);
    /* 标题变化后重查时间，旧查询响应不能把旧进度带回来。 */
    media_status(90000, 200000, ESP_AVRC_PLAYBACK_PLAYING);
    assert(!state.position_known);
    protocol_step();
    media_status(0, 180000, ESP_AVRC_PLAYBACK_PLAYING);
    assert(state.position_known && state.position_ms == 0 && state.duration_known);
    assert(state.playback == SPEAKER_PLAYBACK_PLAYING);
    ticks += 999; requests = status_queries; protocol_step();
    assert(status_queries == requests);
    ticks += 1; protocol_step();
    assert(status_queries == requests + 1);
    media_status(UINT32_MAX, UINT32_MAX, ESP_AVRC_PLAYBACK_PAUSED);
    assert(!state.position_known && state.duration_known && state.duration_ms == 180000 && state.position_ms == 0);
    assert(state.playback == SPEAKER_PLAYBACK_PAUSED);
    /* 通知能力、注册失败重试及曲目变更后重新注册。 */
    esp_avrc_ct_cb_param_t caps = {0};
    caps.get_rn_caps_rsp.evt_set.bits = (1u << ESP_AVRC_RN_TRACK_CHANGE) | (1u << ESP_AVRC_RN_PLAY_STATUS_CHANGE);
    avrc_ct_cb(ESP_AVRC_CT_GET_RN_CAPABILITIES_RSP_EVT, &caps);
    track_error = ESP_FAIL; protocol_step(); assert(track_notification_requested);
    track_error = ESP_OK; unsigned registered = track_subscriptions;
    protocol_step(); assert(track_subscriptions == registered+1 && !track_notification_requested);
    esp_avrc_ct_cb_param_t track = {0}; track.change_ntf.event_id = ESP_AVRC_RN_TRACK_CHANGE;
    avrc_ct_cb(ESP_AVRC_CT_CHANGE_NOTIFY_EVT, &track);
    assert(state.title[0] == 0 && state.artist[0] == 0 && !state.duration_known);
    protocol_step(); assert(metadata_pending == MEDIA_ATTRIBUTES);
    metadata_text(ESP_AVRC_MD_ATTR_TITLE, "过期曲目", 12);
    avrc_ct_cb(ESP_AVRC_CT_CHANGE_NOTIFY_EVT, &track); /* 前一批未结束，再次切歌。 */
    metadata_text(ESP_AVRC_MD_ATTR_ARTIST, "旧歌手", 9);
    metadata_text(ESP_AVRC_MD_ATTR_PLAYING_TIME, "100", 3);
    assert(state.title[0] == 0 && state.artist[0] == 0);
    protocol_step();
    metadata_text(ESP_AVRC_MD_ATTR_ARTIST, "新歌手", 9); /* 属性顺序不限。 */
    metadata_text(ESP_AVRC_MD_ATTR_TITLE, "新曲目", 9);
    metadata_text(ESP_AVRC_MD_ATTR_PLAYING_TIME, "120000", 6);
    assert(strcmp(state.title,"新曲目") == 0 && strcmp(state.artist,"新歌手") == 0);
    assert(state.duration_ms == 120000);
    /* 无曲目通知也定期兜底；缺少属性在超时后合并，不沿用旧歌手。 */
    ticks += 10000; protocol_step();
    metadata_text(ESP_AVRC_MD_ATTR_TITLE, "只有歌名", 12);
    ticks += 2000; protocol_step();
    assert(strcmp(state.title,"只有歌名") == 0 && state.artist[0] == 0 && !metadata_pending);
    /* UTF-8 容量边界、截断和非法编码，不读取长度以外的字节。 */
    char text[8];
    speaker_copy_text(text,sizeof(text),(const uint8_t *)"你好呀",9);
    assert(strcmp(text,"你好") == 0);
    speaker_copy_text(text,sizeof(text),(const uint8_t *)"你好",5);
    assert(strcmp(text,"你") == 0);
    speaker_copy_text(text,sizeof(text),(const uint8_t *)"A\xc0\xaf",3);
    assert(strcmp(text,"A") == 0);
    speaker_copy_text(text,sizeof(text),(const uint8_t *)"\xed\xa0\x80",3);
    assert(text[0] == 0);
    speaker_copy_text(text,sizeof(text),(const uint8_t *)"\xf4\x90\x80\x80",4);
    assert(text[0] == 0);
    speaker_copy_text(text,sizeof(text),NULL,7); assert(text[0] == 0);
    speaker_copy_text(text,sizeof(text),(const uint8_t *)"abc",-1); assert(text[0] == 0);
    uint32_t duration;
    assert(!speaker_parse_duration((const uint8_t *)"4294967296",10,&duration));
    assert(!speaker_parse_duration((const uint8_t *)"-1",2,&duration));
    assert(!speaker_parse_duration((const uint8_t *)"1x",2,&duration));
    assert(!speaker_parse_duration((const uint8_t *)"0",1,&duration));
    /* 断连清空媒体，晚到的回调不能重新填入旧曲目；重连会重新请求。 */
    ct_connection(false);
    metadata_text(ESP_AVRC_MD_ATTR_TITLE,"旧曲目",9);
    media_status(1,2,ESP_AVRC_PLAYBACK_PLAYING);
    assert(state.title[0] == 0 && state.artist[0] == 0 && !state.position_known);
    ct_connection(true); protocol_step();
    assert(metadata_pending == MEDIA_ATTRIBUTES);
    connection(ESP_A2D_CONNECTION_STATE_DISCONNECTED);
    assert(!metadata_pending && state.title[0] == 0 && !state.duration_known);
}

int main(void)
{
    speaker_state_t snapshot;
    assert(speaker_service_get_state(&snapshot) == ESP_ERR_INVALID_STATE);
    assert(speaker_service_get_state(NULL) == ESP_ERR_INVALID_ARG);
    assert(speaker_service_set_volume(128) == ESP_ERR_INVALID_ARG);
    uint8_t master;
    assert(set_master_vol(0) == ESP_ERR_INVALID_STATE);
    assert(master_vol_up() == ESP_ERR_INVALID_STATE);
    assert(master_vol_down() == ESP_ERR_INVALID_STATE);
    assert(get_master_vol(&master) == ESP_ERR_INVALID_STATE);
    state_lock = (void *)1;
    pcm_queue = xQueueCreate(PCM_DEPTH, sizeof(pcm_block_t));
    state_queue = xQueueCreate(1, sizeof(state));
    control_queue = xQueueCreate(1, sizeof(control_request_t));
    response_queue = xQueueCreate(4, sizeof(control_response_t));
    atomic_store(&ready, true);
    check_master_volumes();
    assert(speaker_service_control(SPEAKER_CONTROL_PLAY) == ESP_ERR_INVALID_STATE);
    assert(speaker_service_control((speaker_control_t)99) == ESP_ERR_INVALID_ARG);
    connection(ESP_A2D_CONNECTION_STATE_CONNECTED);
    ct_connection(true);
    unsigned generation = session;
    ct_connection(true);
    assert(session == generation); /* Duplicate events do not abort commands. */
    assert(speaker_service_control(SPEAKER_CONTROL_PLAY_PAUSE) == ESP_ERR_INVALID_STATE);
    audio(true);
    assert(state.streaming && state.playback == SPEAKER_PLAYBACK_PLAYING);
    esp_a2d_cb_param_t format = {0};
    format.audio_cfg.mcc.type = ESP_A2D_MCT_SBC;
    format.audio_cfg.mcc.cie.sbc_info.ch_mode = ESP_A2D_SBC_CIE_CH_MODE_STEREO;
    format.audio_cfg.mcc.cie.sbc_info.samp_freq = ESP_A2D_SBC_CIE_SF_44K;
    a2dp_cb(ESP_A2D_AUDIO_CFG_EVT, &format);
    assert(state.sample_rate_hz == 44100 && atomic_load(&sample_rate) == 44100);
    uint8_t pcm[] = {1, 2, 3, 4};
    pcm_cb(pcm, 3); assert(!pcm_queue->count);
    pcm_cb(pcm, sizeof(pcm)); pcm[0] = 9;
    pcm_block_t block;
    assert(xQueueReceive(pcm_queue, &block, 0) == pdTRUE && block.data[0] == 1);
    unsigned old_epoch = block.epoch;
    audio(false);
    assert(atomic_load(&epoch) != old_epoch);
    pcm_cb(pcm, sizeof(pcm)); assert(!pcm_queue->count);
    audio(true);
    format.audio_cfg.mcc.cie.sbc_info.samp_freq = ESP_A2D_SBC_CIE_SF_48K;
    a2dp_cb(ESP_A2D_AUDIO_CFG_EVT, &format);
    assert(state.sample_rate_hz == 48000);
    format.audio_cfg.mcc.cie.sbc_info.ch_mode = ESP_A2D_SBC_CIE_CH_MODE_MONO;
    a2dp_cb(ESP_A2D_AUDIO_CFG_EVT, &format);
    pcm_cb(pcm, sizeof(pcm));
    assert(state.sample_rate_hz == 0 && !pcm_queue->count);
    format.audio_cfg.mcc.cie.sbc_info.ch_mode = ESP_A2D_SBC_CIE_CH_MODE_STEREO;
    a2dp_cb(ESP_A2D_AUDIO_CFG_EVT, &format);
    for (unsigned i = 0; i <= PCM_DEPTH; ++i) pcm_cb(pcm, sizeof(pcm));
    assert(pcm_queue->count == PCM_DEPTH && atomic_load(&dropped) == 1);
    pcm_queue->count = 0;
    esp_avrc_ct_cb_param_t caps = {0};
    caps.get_rn_caps_rsp.evt_set.bits = 1u << ESP_AVRC_RN_PLAY_STATUS_CHANGE;
    avrc_ct_cb(ESP_AVRC_CT_GET_RN_CAPABILITIES_RSP_EVT, &caps);
    protocol_step();
    unsigned subscribed = subscriptions;
    playback(ESP_AVRC_PLAYBACK_PAUSED);
    protocol_step();
    assert(state.playback == SPEAKER_PLAYBACK_PAUSED && subscriptions == subscribed + 1);
    audio(false);
    assert(!state.streaming && state.playback == SPEAKER_PLAYBACK_PAUSED);
    playback(ESP_AVRC_PLAYBACK_STOPPED);
    audio(false);
    assert(state.playback == SPEAKER_PLAYBACK_STOPPED);
    assert(speaker_service_control(SPEAKER_CONTROL_PLAY_PAUSE) == ESP_OK);
    assert(speaker_service_control(SPEAKER_CONTROL_NEXT) == ESP_ERR_NOT_FINISHED);
    run_control();
    assert(sent_keys == 2 && last_key == ESP_AVRC_PT_CMD_PLAY && last_key_state == ESP_AVRC_PT_CMD_STATE_RELEASED);
    assert(state.control_result == ESP_OK && state.playback == SPEAKER_PLAYBACK_STOPPED);
    key_response = ESP_AVRC_RSP_REJECT;
    assert(speaker_service_control(SPEAKER_CONTROL_NEXT) == ESP_OK); run_control();
    assert(sent_keys == 4 && state.control_result == ESP_ERR_NOT_SUPPORTED);
    key_response = ESP_AVRC_RSP_ACCEPT; respond = false;
    assert(speaker_service_control(SPEAKER_CONTROL_PAUSE) == ESP_OK); run_control();
    assert(sent_keys == 6 && state.control_result == ESP_ERR_TIMEOUT); /* Release still sent. */
    respond = true; disconnect_on_wait = true;
    assert(speaker_service_control(SPEAKER_CONTROL_PREVIOUS) == ESP_OK); run_control();
    assert(sent_keys == 7 && state.control_result == ESP_ERR_INVALID_STATE);
    assert(state.connection == SPEAKER_DISCONNECTED && state.playback == SPEAKER_PLAYBACK_UNKNOWN);
    playback(ESP_AVRC_PLAYBACK_PLAYING); audio(true);
    assert(state.playback == SPEAKER_PLAYBACK_UNKNOWN && !state.streaming);
    connection(ESP_A2D_CONNECTION_STATE_CONNECTED); ct_connection(true);
    assert(!state.streaming && state.playback == SPEAKER_PLAYBACK_UNKNOWN);
    assert(speaker_service_control(SPEAKER_CONTROL_PLAY) == ESP_OK);
    unsigned commands = sent_keys;
    connection(ESP_A2D_CONNECTION_STATE_DISCONNECTED);
    connection(ESP_A2D_CONNECTION_STATE_CONNECTED); ct_connection(true);
    run_control();
    assert(sent_keys == commands && state.control_result == ESP_ERR_INVALID_STATE);
    protocol_step();
    unsigned queries = status_queries;
    ticks += 2000; protocol_step();
    assert(status_queries == queries + 1); /* An unanswered status query can retry. */
    assert(speaker_service_set_volume(40) == ESP_OK);
    esp_avrc_tg_cb_param_t tg = {0}; tg.conn_stat.connected = true;
    avrc_tg_cb(ESP_AVRC_TG_CONNECTION_STATE_EVT, &tg);
    tg.reg_ntf.event_id = ESP_AVRC_RN_VOLUME_CHANGE;
    avrc_tg_cb(ESP_AVRC_TG_REGISTER_NOTIFICATION_EVT, &tg);
    protocol_step();
    assert(last_volume_response == ESP_AVRC_RN_RSP_INTERIM && last_volume == 40);
    unsigned replies = volume_replies;
    assert(speaker_service_set_volume(40) == ESP_OK); protocol_step();
    assert(volume_replies == replies);
    tg.set_abs_vol.volume = 70;
    avrc_tg_cb(ESP_AVRC_TG_SET_ABSOLUTE_VOLUME_CMD_EVT, &tg);
    protocol_step();
    assert(state.volume == 70 && atomic_load(&pcm_volume) == 70 && volume_replies == replies);
    assert(speaker_service_set_volume(70) == ESP_OK); protocol_step();
    assert(volume_replies == replies);
    assert(speaker_service_set_volume(71) == ESP_OK); protocol_step();
    assert(volume_replies == replies + 1);
    assert(last_volume_response == ESP_AVRC_RN_RSP_CHANGED && last_volume == 71 && !volume_subscribed);
    assert(speaker_service_set_volume(72) == ESP_OK); protocol_step();
    assert(volume_replies == replies + 1);
    tg.reg_ntf.event_id = ESP_AVRC_RN_VOLUME_CHANGE;
    avrc_tg_cb(ESP_AVRC_TG_REGISTER_NOTIFICATION_EVT, &tg);
    protocol_step();
    volume_error = ESP_FAIL;
    assert(speaker_service_set_volume(73) == ESP_OK); protocol_step();
    assert(state.volume_sync_result == ESP_FAIL && volume_subscribed && state.volume == 73);
    volume_error = ESP_OK;
    assert(speaker_service_set_volume(73) == ESP_OK); protocol_step();
    assert(!volume_subscribed && state.volume_sync_result == ESP_OK);
    connection(ESP_A2D_CONNECTION_STATE_DISCONNECTED);
    assert(!volume_subscribed && !state.volume_connected);
    assert(speaker_service_set_volume(127) == ESP_OK);
    assert(speaker_service_wait_state(&snapshot, 0) == ESP_OK && snapshot.volume == 127);
    assert(speaker_service_wait_state(&snapshot, 0) == ESP_ERR_TIMEOUT);
    assert(speaker_service_get_state(&snapshot) == ESP_OK && snapshot.volume == 127);
    check_media();
    /* Exhaust all PCM16 values at mute, partial gain and unity, both channels. */
    for (unsigned gain = 0; gain <= 127; ++gain) {
        for (int sample = INT16_MIN; sample <= INT16_MAX; ++sample) {
            int16_t stereo[] = {sample, sample};
            speaker_pcm_scale((uint8_t *)stereo, sizeof(stereo), gain);
            assert(stereo[0] == sample * (int)gain / 127 && stereo[1] == stereo[0]);
        }
    }
    puts("PASS: state/reconnect, PCM copy/pause/format/overflow, AVRCP control/reject/timeout/cancel, status retry, media metadata/UTF-8/track/position/retry/disconnect, master volume levels/steps/phone mapping, volume sync/retry, exhaustive PCM16 gain");
    return 0;
}
