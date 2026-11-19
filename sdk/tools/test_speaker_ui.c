/* 使用真正的 LVGL 无窗口显示器检查页面，不替换控件实现。 */
#include "../apps/esp_app/ui/speaker_ui.c"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t time_ms;
static speaker_ui_action_t last_action;
static unsigned action_count;
static bool accept_action = true;
static uint16_t frame[240 * 280];
static unsigned flush_count;

static uint32_t test_clock(void)
{
    return time_ms;
}

static bool collect_action(const speaker_ui_action_t *action, void *context)
{
    if (accept_action)
    {
        last_action = *action;
        ++action_count;
    }
    return accept_action;
}

static void flush_frame(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    assert(area->x1 >= 0 && area->y1 >= 0 && area->x2 < 240 && area->y2 < 280);
    const uint16_t *source = (const uint16_t *)pixels;
    for (int y = area->y1; y <= area->y2; ++y)
    {
        for (int x = area->x1; x <= area->x2; ++x)
        {
            frame[y * 240 + x] = *source++;
        }
    }
    ++flush_count;
    lv_display_flush_ready(display);
}

static void advance(uint32_t delta)
{
    time_ms += delta;
    speaker_ui_tick(time_ms);
}

static void key(speaker_ui_key_t value)
{
    speaker_ui_send_key(value, time_ms);
}

static void click(void)
{
    key(SPEAKER_UI_KEY_CONFIRM);
    advance(300);
}

static void back(void)
{
    key(SPEAKER_UI_KEY_CONFIRM);
    advance(100);
    key(SPEAKER_UI_KEY_CONFIRM);
    advance(300);
}

static void save_frame(const char *name)
{
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(NULL);
    FILE *output = fopen(name, "wb");
    assert(output);
    fprintf(output, "P6\n240 280\n255\n");
    for (unsigned i = 0; i < sizeof(frame) / sizeof(frame[0]); ++i)
    {
        unsigned value = frame[i];
        unsigned char pixel[] = {
            (unsigned char)(((value >> 11) & 31) * 255 / 31),
            (unsigned char)(((value >> 5) & 63) * 255 / 63),
            (unsigned char)((value & 31) * 255 / 31),
        };
        assert(fwrite(pixel, 1, 3, output) == 3);
    }
    assert(fclose(output) == 0);
}

static void check_bounds(lv_obj_t *object)
{
    if (lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN))
    {
        return;
    }
    lv_area_t area;
    lv_obj_get_coords(object, &area);
    assert(area.x1 >= 0 && area.y1 >= 0 && area.x2 < 240 && area.y2 < 280);
    for (unsigned i = 0; i < lv_obj_get_child_count(object); ++i)
    {
        check_bounds(lv_obj_get_child(object, i));
    }
}

int main(void)
{
    lv_init();
    lv_tick_set_cb(test_clock);
    lv_display_t *display = lv_display_create(240, 280);
    assert(display);
    static uint16_t buffer[240 * 24];
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, buffer, NULL, sizeof(buffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush_frame);
    speaker_ui_config_t config = {.double_click_ms = 300, .volume_step = 5, .brightness_step = 10};
    assert(!speaker_ui_init(NULL, collect_action, NULL));
    assert(speaker_ui_init(&config, collect_action, NULL));
    speaker_ui_state_t sample = {
        .connected = true,
        .playing = true,
        .playback_known = true,
        .control_available = true,
        .volume_known = true,
        .volume_percent = 40,
        .brightness_available = true,
        .brightness_percent = 60,
        .position_known = true,
        .duration_known = true,
        .position_ms = 83000,
        .duration_ms = 256000,
        .title = "潮汐之间",
        .artist = "深蓝乐队",
        .lyrics = {"晚风轻轻掠过海面", "把心事唱给深蓝听", "等下一阵潮汐回应"},
        .device_name = "ESP32-Speaker-Lab",
        .firmware_version = "0.1.0",
    };
    assert(speaker_ui_set_state(&sample, time_ms));
    assert(lv_bar_get_value(progress) == 32);
    key(SPEAKER_UI_KEY_CONFIRM);
    advance(299);
    assert(action_count == 0);
    advance(1);
    assert(action_count == 1 && last_action.type == SPEAKER_UI_PLAY_PAUSE);
    assert(state.playing);
    back();
    assert(page == PAGE_HOME && action_count == 1);
    key(SPEAKER_UI_KEY_DOWN);
    key(SPEAKER_UI_KEY_DOWN);
    assert(home_focus == 3);
    click();
    assert(page == PAGE_SETTINGS);
    /* 双击返回不会先进入当前选中的亮度页。 */
    back();
    assert(page == PAGE_HOME && home_focus == 3 && action_count == 1);
    click();
    click();
    assert(page == PAGE_BRIGHTNESS);
    key(SPEAKER_UI_KEY_UP);
    assert(last_action.type == SPEAKER_UI_BRIGHTNESS && last_action.delta == 10);
    assert(state.brightness_percent == 60);
    for (unsigned target = 0; target < PAGE_COUNT; ++target)
    {
        show_page(target);
        unsigned old_focus = home_focus;
        key(SPEAKER_UI_KEY_VOLUME_UP);
        assert(page == target && home_focus == old_focus);
        assert(last_action.type == SPEAKER_UI_VOLUME && last_action.delta == 5);
        assert(state.volume_percent == 40);
        assert(!lv_obj_has_flag(volume_popup, LV_OBJ_FLAG_HIDDEN));
        key(SPEAKER_UI_KEY_VOLUME_DOWN);
        assert(last_action.delta == -5);
        advance(1200);
        assert(lv_obj_has_flag(volume_popup, LV_OBJ_FLAG_HIDDEN));
    }
    show_page(PAGE_BRIGHTNESS);
    sample.brightness_available = false;
    assert(speaker_ui_set_state(&sample, time_ms));
    unsigned before = action_count;
    key(SPEAKER_UI_KEY_UP);
    assert(action_count == before && notice_visible);
    assert(strcmp(lv_label_get_text(brightness_value), "未接入") == 0);
    click();
    assert(page == PAGE_SETTINGS && setting_focus == 0);
    key(SPEAKER_UI_KEY_DOWN);
    click();
    assert(page == PAGE_INFO);
    back();
    assert(page == PAGE_SETTINGS && setting_focus == 1);
    back();
    assert(page == PAGE_HOME && home_focus == 3);
    key(SPEAKER_UI_KEY_DOWN);
    assert(home_focus == 0);
    click();
    assert(last_action.type == SPEAKER_UI_PREVIOUS);
    key(SPEAKER_UI_KEY_DOWN);
    key(SPEAKER_UI_KEY_DOWN);
    click();
    assert(last_action.type == SPEAKER_UI_NEXT);
    /* 等待期间移动取消旧确认；正好 300ms 则属于下一次单击。 */
    before = action_count;
    key(SPEAKER_UI_KEY_CONFIRM);
    advance(100);
    key(SPEAKER_UI_KEY_UP);
    advance(300);
    assert(action_count == before && home_focus == 1);
    key(SPEAKER_UI_KEY_CONFIRM);
    time_ms += 300;
    key(SPEAKER_UI_KEY_CONFIRM);
    assert(action_count == before + 1 && confirm_pending);
    advance(300);
    assert(action_count == before + 2);
    /* 无控制通道、繁忙和满队列均不得伪造状态。 */
    sample.control_available = false;
    assert(speaker_ui_set_state(&sample, time_ms));
    before = action_count;
    click();
    assert(action_count == before && state.playing);
    sample.control_available = true;
    sample.control_busy = true;
    assert(speaker_ui_set_state(&sample, time_ms));
    click();
    assert(action_count == before);
    sample.control_busy = false;
    assert(speaker_ui_set_state(&sample, time_ms));
    accept_action = false;
    click();
    assert(action_count == before && notice_visible);
    accept_action = true;
    /* 32 位毫秒时钟回绕仍正确；迟到的按键时刻不应让新提示立刻过期。 */
    time_ms = UINT32_MAX - 100;
    key(SPEAKER_UI_KEY_CONFIRM);
    advance(299);
    assert(action_count == before);
    advance(1);
    assert(action_count == before + 1);
    notice("测试提示", time_ms);
    speaker_ui_send_key(SPEAKER_UI_KEY_VOLUME_UP, time_ms - 1);
    assert(notice_visible);
    advance(2500);
    assert(!notice_visible);
    sample.volume_percent = 101;
    assert(!speaker_ui_set_state(&sample, time_ms));
    sample.volume_percent = 40;
    memset(sample.title, 'x', sizeof(sample.title));
    assert(!speaker_ui_set_state(&sample, time_ms));
    snprintf(sample.title, sizeof(sample.title), "潮汐之间");
    sample.position_ms = UINT32_MAX;
    assert(speaker_ui_set_state(&sample, time_ms));
    assert(lv_bar_get_value(progress) == 100);
    sample.position_known = false;
    sample.duration_known = false;
    sample.lyrics[0][0] = sample.lyrics[1][0] = sample.lyrics[2][0] = 0;
    assert(speaker_ui_set_state(&sample, time_ms));
    assert(strcmp(lv_label_get_text(elapsed_label), "--:--") == 0);
    assert(strcmp(lv_label_get_text(lyric_labels[1]), "暂无歌词") == 0);
    /* 检查常见中文、完整基本汉字区及文字缺失回退。 */
    lv_font_glyph_dsc_t glyph;
    assert(lv_font_get_glyph_dsc(&speaker_font_16, &glyph, 0x97f3, 0));
    assert(lv_font_get_glyph_dsc(&speaker_font_16, &glyph, 0x9f99, 0));
    sample.position_known = sample.duration_known = true;
    sample.position_ms = 83000;
    sample.duration_ms = 256000;
    snprintf(sample.lyrics[0], sizeof(sample.lyrics[0]), "晚风轻轻掠过海面");
    snprintf(sample.lyrics[1], sizeof(sample.lyrics[1]), "把心事唱给深蓝听");
    snprintf(sample.lyrics[2], sizeof(sample.lyrics[2]), "等下一阵潮汐回应");
    sample.brightness_available = true;
    assert(speaker_ui_set_state(&sample, time_ms));
    advance(3000);
    show_page(PAGE_HOME);
    refresh();
    lv_obj_update_layout(root);
    check_bounds(root);
    save_frame("home.ppm");
    show_page(PAGE_SETTINGS);
    lv_obj_update_layout(root);
    check_bounds(root);
    save_frame("settings.ppm");
    show_page(PAGE_BRIGHTNESS);
    lv_obj_update_layout(root);
    check_bounds(root);
    save_frame("brightness.ppm");
    show_page(PAGE_INFO);
    lv_obj_update_layout(root);
    check_bounds(root);
    save_frame("info.ppm");
    assert(flush_count >= 4);
    /* 静态页面不应在每个调度周期重刷整屏。 */
    unsigned rendered = flush_count;
    assert(speaker_ui_set_state(&sample, time_ms));
    lv_refr_now(NULL);
    assert(flush_count == rendered);
    lv_deinit();
    puts("speaker UI: PASS (real LVGL, actions, timing, state and rendering)");
    return 0;
}
