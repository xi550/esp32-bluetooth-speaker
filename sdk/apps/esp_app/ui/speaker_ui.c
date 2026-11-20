#include "speaker_ui.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>

LV_FONT_DECLARE(speaker_font_16);

enum
{
    PAGE_HOME,
    PAGE_SETTINGS,
    PAGE_BRIGHTNESS,
    PAGE_INFO,
    PAGE_COUNT,
};

static const uint32_t background = 0x101724;
static const uint32_t surface = 0x1a2638;
static const uint32_t foreground = 0xf0f5ff;
static const uint32_t secondary = 0x98a8c0;
static const uint32_t accent = 0x69baff;
static speaker_ui_config_t settings;
static speaker_ui_state_t state;
static speaker_ui_action_cb_t on_action;
static void *action_context;
static lv_obj_t *root;
static lv_obj_t *pages[PAGE_COUNT];
static lv_obj_t *connection_label;
static lv_obj_t *volume_label;
static lv_obj_t *home_buttons[4];
static lv_obj_t *play_icon;
static lv_obj_t *title_label;
static lv_obj_t *artist_label;
static lv_obj_t *lyric_labels[3];
static lv_obj_t *progress;
static lv_obj_t *elapsed_label;
static lv_obj_t *duration_label;
static lv_obj_t *hint_label;
static lv_obj_t *setting_buttons[2];
static lv_obj_t *brightness_value;
static lv_obj_t *brightness_number;
static lv_obj_t *brightness_bar;
static lv_obj_t *brightness_hint;
static lv_obj_t *device_label;
static lv_obj_t *version_label;
static lv_obj_t *volume_popup;
static lv_obj_t *volume_popup_label;
static lv_obj_t *volume_popup_bar;
static lv_obj_t *notice_label;
static unsigned page;
static unsigned home_focus = 1;
static unsigned setting_focus;
static bool confirm_pending;
static uint32_t confirm_started;
static bool volume_visible;
static uint32_t volume_started;
static bool notice_visible;
static uint32_t notice_started;

static void release_label_text(lv_event_t *event)
{
    lv_obj_t *object = lv_event_get_target_obj(event);
    lv_free(lv_obj_get_user_data(object));
}

static lv_obj_t *box(lv_obj_t *parent, int x, int y, int width, int height, uint32_t color)
{
    lv_obj_t *object = lv_obj_create(parent);
    lv_obj_remove_style_all(object);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_size(object, width, height);
    lv_obj_set_style_bg_color(object, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    return object;
}

static lv_obj_t *label(lv_obj_t *parent, int x, int y, int width, uint32_t color)
{
    lv_obj_t *object = lv_label_create(parent);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_width(object, width);
    lv_obj_set_style_text_font(object, &speaker_font_16, 0);
    lv_obj_set_style_text_color(object, lv_color_hex(color), 0);
    lv_label_set_long_mode(object, LV_LABEL_LONG_DOT);
    lv_obj_set_height(object, 24);
    lv_label_set_text(object, "");
    lv_obj_add_event_cb(object, release_label_text, LV_EVENT_DELETE, NULL);
    return object;
}

static void text(lv_obj_t *object, const char *value)
{
    /* 省略模式会改写显示缓冲，比较原始文字才能避免无变化时重复刷新。 */
    const char *previous = lv_obj_get_user_data(object);
    if (!previous || strcmp(previous, value) != 0)
    {
        char *copy = lv_malloc(strlen(value) + 1);
        LV_ASSERT_MALLOC(copy);
        if (!copy)
        {
            return;
        }
        strcpy(copy, value);
        lv_free(lv_obj_get_user_data(object));
        lv_obj_set_user_data(object, copy);
        lv_label_set_text(object, value);
    }
}

static lv_obj_t *bar(lv_obj_t *parent, int x, int y, int width, int height)
{
    lv_obj_t *object = lv_bar_create(parent);
    lv_obj_remove_style_all(object);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_size(object, width, height);
    lv_obj_set_style_bg_color(object, lv_color_hex(surface), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(object, lv_color_hex(accent), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_bar_set_range(object, 0, 100);
    return object;
}

static lv_obj_t *button(lv_obj_t *parent, int x, int y, int width, int height)
{
    lv_obj_t *object = box(parent, x, y, width, height, background);
    lv_obj_set_style_radius(object, 9, 0);
    lv_obj_set_style_border_width(object, 1, 0);
    lv_obj_set_style_border_color(object, lv_color_hex(surface), 0);
    lv_obj_set_style_bg_color(object, lv_color_hex(surface), LV_STATE_FOCUSED);
    lv_obj_set_style_border_color(object, lv_color_hex(accent), LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(object, 2, LV_STATE_FOCUSED);
    return object;
}

static void focus(lv_obj_t *object, bool selected)
{
    if (selected)
    {
        lv_obj_add_state(object, LV_STATE_FOCUSED);
    }
    else
    {
        lv_obj_remove_state(object, LV_STATE_FOCUSED);
    }
}

static void show_page(unsigned next)
{
    page = next;
    confirm_pending = false;
    for (unsigned i = 0; i < PAGE_COUNT; ++i)
    {
        if (i == page)
        {
            lv_obj_remove_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (page == PAGE_HOME)
    {
        lv_obj_remove_flag(home_buttons[3], LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(volume_label, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(home_buttons[3], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(volume_label, LV_OBJ_FLAG_HIDDEN);
    }
}

static void notice(const char *value, uint32_t now_ms)
{
    text(notice_label, value);
    notice_visible = value[0] != '\0';
    notice_started = now_ms;
    if (notice_visible)
    {
        lv_obj_remove_flag(notice_label, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(notice_label, LV_OBJ_FLAG_HIDDEN);
    }
}

static void format_time(char *buffer, size_t capacity, bool known, uint32_t time_ms)
{
    if (known)
    {
        unsigned seconds = time_ms / 1000;
        snprintf(buffer, capacity, "%02u:%02u", seconds / 60, seconds % 60);
    }
    else
    {
        snprintf(buffer, capacity, "--:--");
    }
}

static void refresh(void)
{
    char buffer[64];
    const char *connection_text = "未连接";
    if (state.connected)
    {
        connection_text = "已连接";
    }
    else if (state.connecting)
    {
        connection_text = "连接中";
    }
    text(connection_label, connection_text);
    if (state.volume_known)
    {
        if (state.volume_percent)
        {
            snprintf(buffer, sizeof(buffer), "音量 %u%%", state.volume_percent);
        }
        else
        {
            snprintf(buffer, sizeof(buffer), "已静音");
        }
    }
    else
    {
        snprintf(buffer, sizeof(buffer), "音量 --");
    }
    text(volume_label, buffer);
    text(volume_popup_label, state.volume_known ? buffer : "等待音量状态");
    lv_bar_set_value(volume_popup_bar, state.volume_known ? state.volume_percent : 0, LV_ANIM_OFF);
    text(title_label, state.title[0] ? state.title : "蓝牙音乐");
    text(artist_label, state.artist[0] ? state.artist : "未知歌手");
    for (unsigned i = 0; i < 3; ++i)
    {
        text(lyric_labels[i], i == 1 && !state.lyrics[1][0] ? "暂无歌词" : state.lyrics[i]);
    }
    uint32_t position = state.position_ms;
    if (state.duration_known && state.duration_ms && position > state.duration_ms)
    {
        position = state.duration_ms;
    }
    format_time(buffer, sizeof(buffer), state.position_known, position);
    text(elapsed_label, buffer);
    format_time(buffer, sizeof(buffer), state.duration_known, state.duration_ms);
    text(duration_label, buffer);
    int percent = state.position_known && state.duration_known && state.duration_ms
        ? (int)((uint64_t)position * 100 / state.duration_ms)
        : 0;
    lv_bar_set_value(progress, percent, LV_ANIM_OFF);
    text(play_icon, state.playback_known && state.playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    const char *names[] = {"上一首", state.playing ? "暂停" : "播放", "下一首", "设置"};
    snprintf(buffer, sizeof(buffer), "%s · 单击确认", names[home_focus]);
    text(hint_label, state.control_busy && home_focus != 3 ? "操作处理中" : buffer);
    for (unsigned i = 0; i < 4; ++i)
    {
        focus(home_buttons[i], i == home_focus);
        lv_obj_t *symbol = lv_obj_get_child(home_buttons[i], 0);
        lv_color_t color = lv_color_hex(i == home_focus ? background : foreground);
        if (!lv_color_eq(lv_obj_get_style_text_color(symbol, 0), color))
        {
            lv_obj_set_style_text_color(symbol, color, 0);
        }
    }
    for (unsigned i = 0; i < 2; ++i)
    {
        focus(setting_buttons[i], i == setting_focus);
    }
    if (state.brightness_available)
    {
        snprintf(buffer, sizeof(buffer), "%u%%", state.brightness_percent);
        text(brightness_hint, "上增大 · 下减小 · 单击完成");
    }
    else
    {
        snprintf(buffer, sizeof(buffer), "未接入");
        text(brightness_hint, "背光控制尚未接入");
    }
    text(brightness_value, buffer);
    text(brightness_number, state.brightness_available ? buffer : "--");
    lv_bar_set_value(
        brightness_bar, state.brightness_available ? state.brightness_percent : 0, LV_ANIM_OFF);
    text(device_label, state.device_name[0] ? state.device_name : "--");
    text(version_label, state.firmware_version[0] ? state.firmware_version : "--");
}

static void emit(speaker_ui_action_type_t type, int delta, uint32_t now_ms)
{
    speaker_ui_action_t action = {.type = type, .delta = delta};
    if (!on_action(&action, action_context))
    {
        notice("操作队列已满，请稍后重试", now_ms);
    }
}

static void confirm(uint32_t now_ms)
{
    if (page == PAGE_HOME)
    {
        if (home_focus == 3)
        {
            show_page(PAGE_SETTINGS);
        }
        else if (!state.control_available)
        {
            notice("蓝牙控制未就绪", now_ms);
        }
        else if (state.control_busy)
        {
            notice("请等待当前操作完成", now_ms);
        }
        else
        {
            static const speaker_ui_action_type_t types[] = {
                SPEAKER_UI_PREVIOUS, SPEAKER_UI_PLAY_PAUSE, SPEAKER_UI_NEXT};
            emit(types[home_focus], 0, now_ms);
        }
    }
    else if (page == PAGE_SETTINGS)
    {
        show_page(setting_focus == 0 ? PAGE_BRIGHTNESS : PAGE_INFO);
    }
    else
    {
        show_page(PAGE_SETTINGS);
    }
    refresh();
}

bool speaker_ui_init(
    const speaker_ui_config_t *config, speaker_ui_action_cb_t callback, void *context)
{
    if (root || !config || !callback || config->double_click_ms == 0 ||
        config->double_click_ms > 2000 || config->brightness_step == 0 ||
        config->brightness_step > 100 || !lv_display_get_default() ||
        lv_display_get_horizontal_resolution(NULL) < 240 ||
        lv_display_get_vertical_resolution(NULL) < 280)
    {
        return false;
    }
    settings = *config;
    on_action = callback;
    action_context = context;
    root = lv_obj_create(NULL);
    lv_obj_remove_style_all(root);
    lv_obj_set_style_bg_color(root, lv_color_hex(background), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    for (unsigned i = 0; i < PAGE_COUNT; ++i)
    {
        pages[i] = box(root, 0, 32, 240, 248, background);
    }
    connection_label = label(root, 12, 4, 92, accent);
    volume_label = label(root, 106, 4, 94, secondary);
    home_buttons[3] = button(root, 208, 4, 24, 24);
    lv_obj_set_style_bg_color(home_buttons[3], lv_color_hex(accent), LV_STATE_FOCUSED);
    lv_obj_t *gear = label(home_buttons[3], 0, 0, 24, secondary);
    lv_obj_set_style_text_font(gear, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(gear, LV_TEXT_ALIGN_CENTER, 0);
    text(gear, LV_SYMBOL_SETTINGS);
    lv_obj_center(gear);

    title_label = label(pages[PAGE_HOME], 16, 0, 208, foreground);
    artist_label = label(pages[PAGE_HOME], 16, 26, 208, secondary);
    lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_align(artist_label, LV_TEXT_ALIGN_CENTER, 0);
    for (unsigned i = 0; i < 3; ++i)
    {
        lyric_labels[i] =
            label(pages[PAGE_HOME], 10, 62 + (int)i * 26, 220, i == 1 ? accent : secondary);
        lv_obj_set_style_text_align(lyric_labels[i], LV_TEXT_ALIGN_CENTER, 0);
    }
    progress = bar(pages[PAGE_HOME], 16, 155, 208, 3);
    elapsed_label = label(pages[PAGE_HOME], 16, 162, 100, secondary);
    duration_label = label(pages[PAGE_HOME], 124, 162, 100, secondary);
    lv_obj_set_style_text_font(elapsed_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_font(duration_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(duration_label, LV_TEXT_ALIGN_RIGHT, 0);
    for (unsigned i = 0; i < 3; ++i)
    {
        home_buttons[i] = button(pages[PAGE_HOME], 34 + (int)i * 64, 184, i == 1 ? 44 : 40, 42);
        lv_obj_set_style_radius(home_buttons[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(home_buttons[i], lv_color_hex(accent), LV_STATE_FOCUSED);
        lv_obj_t *symbol = label(home_buttons[i], 0, 0, 36, foreground);
        lv_obj_set_style_text_font(
            symbol, i == 1 ? &lv_font_montserrat_20 : &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_align(symbol, LV_TEXT_ALIGN_CENTER, 0);
        static const char *symbols[] = {LV_SYMBOL_PREV, LV_SYMBOL_PLAY, LV_SYMBOL_NEXT};
        text(symbol, symbols[i]);
        lv_obj_center(symbol);
        if (i == 1)
        {
            play_icon = symbol;
        }
    }
    hint_label = label(pages[PAGE_HOME], 8, 226, 224, secondary);
    lv_obj_set_height(hint_label, 22);
    lv_obj_set_style_text_align(hint_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t *heading = label(pages[PAGE_SETTINGS], 16, 10, 208, foreground);
    text(heading, "设置");
    for (unsigned i = 0; i < 2; ++i)
    {
        setting_buttons[i] = button(pages[PAGE_SETTINGS], 16, 52 + (int)i * 62, 208, 50);
        lv_obj_t *name = label(setting_buttons[i], 12, 12, 112, foreground);
        text(name, i == 0 ? "屏幕亮度" : "设备信息");
    }
    brightness_value = label(setting_buttons[0], 124, 12, 76, secondary);
    heading = label(pages[PAGE_BRIGHTNESS], 16, 10, 208, foreground);
    text(heading, "屏幕亮度");
    brightness_number = label(pages[PAGE_BRIGHTNESS], 16, 70, 208, foreground);
    lv_obj_set_style_text_font(brightness_number, &lv_font_montserrat_28, 0);
    lv_obj_set_height(brightness_number, 40);
    lv_obj_set_style_text_align(brightness_number, LV_TEXT_ALIGN_CENTER, 0);
    brightness_bar = bar(pages[PAGE_BRIGHTNESS], 24, 130, 192, 7);
    brightness_hint = label(pages[PAGE_BRIGHTNESS], 8, 162, 224, secondary);
    lv_obj_set_style_text_align(brightness_hint, LV_TEXT_ALIGN_CENTER, 0);
    heading = label(pages[PAGE_INFO], 16, 10, 208, foreground);
    text(heading, "设备信息");
    heading = label(pages[PAGE_INFO], 16, 48, 208, secondary);
    text(heading, "设备名称");
    device_label = label(pages[PAGE_INFO], 16, 74, 208, foreground);
    heading = label(pages[PAGE_INFO], 16, 108, 208, secondary);
    text(heading, "固件版本");
    version_label = label(pages[PAGE_INFO], 16, 134, 208, foreground);
    heading = label(pages[PAGE_INFO], 16, 174, 208, secondary);
    text(heading, "屏幕");
    heading = label(pages[PAGE_INFO], 16, 200, 208, foreground);
    text(heading, "1.69 英寸 / 240×280");
    volume_popup = box(root, 20, 110, 200, 72, surface);
    lv_obj_set_style_radius(volume_popup, 10, 0);
    lv_obj_set_style_border_width(volume_popup, 1, 0);
    lv_obj_set_style_border_color(volume_popup, lv_color_hex(accent), 0);
    volume_popup_label = label(volume_popup, 8, 10, 184, foreground);
    lv_obj_set_style_text_align(volume_popup_label, LV_TEXT_ALIGN_CENTER, 0);
    volume_popup_bar = bar(volume_popup, 16, 48, 168, 4);
    lv_obj_add_flag(volume_popup, LV_OBJ_FLAG_HIDDEN);
    notice_label = label(root, 8, 246, 224, foreground);
    lv_obj_set_style_text_align(notice_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_color(notice_label, lv_color_hex(surface), 0);
    lv_obj_set_style_bg_opa(notice_label, LV_OPA_COVER, 0);
    lv_obj_add_flag(notice_label, LV_OBJ_FLAG_HIDDEN);
    show_page(PAGE_HOME);
    lv_screen_load(root);
    lv_obj_update_layout(root);
    refresh();
    return true;
}

bool speaker_ui_state_valid(const speaker_ui_state_t *next)
{
    if (!next || next->volume_percent > 100 || next->brightness_percent > 100 ||
        !memchr(next->title, 0, sizeof(next->title)) ||
        !memchr(next->artist, 0, sizeof(next->artist)) ||
        !memchr(next->device_name, 0, sizeof(next->device_name)) ||
        !memchr(next->firmware_version, 0, sizeof(next->firmware_version)) ||
        !memchr(next->message, 0, sizeof(next->message)))
    {
        return false;
    }
    for (unsigned i = 0; i < 3; ++i)
    {
        if (!memchr(next->lyrics[i], 0, sizeof(next->lyrics[i])))
        {
            return false;
        }
    }
    return true;
}

bool speaker_ui_set_state(const speaker_ui_state_t *next, uint32_t now_ms)
{
    if (!root || !speaker_ui_state_valid(next))
    {
        return false;
    }
    if (strcmp(next->message, state.message) != 0)
    {
        notice(next->message, now_ms);
    }
    state = *next;
    refresh();
    return true;
}

void speaker_ui_send_key(speaker_ui_key_t key, uint32_t now_ms)
{
    if (!root || key < SPEAKER_UI_KEY_UP || key > SPEAKER_UI_KEY_VOLUME_DOWN)
    {
        return;
    }
    speaker_ui_tick(now_ms);
    if (key == SPEAKER_UI_KEY_CONFIRM)
    {
        if (confirm_pending)
        {
            confirm_pending = false;
            if (page != PAGE_HOME)
            {
                show_page(page == PAGE_SETTINGS ? PAGE_HOME : PAGE_SETTINGS);
            }
        }
        else
        {
            confirm_pending = true;
            confirm_started = now_ms;
        }
    }
    else if (key == SPEAKER_UI_KEY_VOLUME_UP || key == SPEAKER_UI_KEY_VOLUME_DOWN)
    {
        /* 实体音量键已经执行功能，这里只显示真实音量浮层，不二次发出动作。 */
        volume_visible = true;
        volume_started = now_ms;
        lv_obj_remove_flag(volume_popup, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        /* 移动焦点会取消未完成的单击，避免对新焦点执行旧确认。 */
        confirm_pending = false;
        int delta = key == SPEAKER_UI_KEY_UP ? -1 : 1;
        if (page == PAGE_HOME)
        {
            home_focus = (home_focus + delta + 4) % 4;
        }
        else if (page == PAGE_SETTINGS)
        {
            setting_focus = (setting_focus + delta + 2) % 2;
        }
        else if (page == PAGE_BRIGHTNESS)
        {
            if (state.brightness_available)
            {
                emit(SPEAKER_UI_BRIGHTNESS, -delta * settings.brightness_step, now_ms);
            }
            else
            {
                notice("背光控制尚未接入", now_ms);
            }
        }
    }
    refresh();
}

void speaker_ui_tick(uint32_t now_ms)
{
    if (!root)
    {
        return;
    }
    if (confirm_pending && (int32_t)(now_ms - confirm_started) >= (int32_t)settings.double_click_ms)
    {
        confirm_pending = false;
        confirm(now_ms);
    }
    if (volume_visible && (int32_t)(now_ms - volume_started) >= 1200)
    {
        volume_visible = false;
        lv_obj_add_flag(volume_popup, LV_OBJ_FLAG_HIDDEN);
    }
    if (notice_visible && (int32_t)(now_ms - notice_started) >= 2500)
    {
        notice_visible = false;
        lv_obj_add_flag(notice_label, LV_OBJ_FLAG_HIDDEN);
    }
}
