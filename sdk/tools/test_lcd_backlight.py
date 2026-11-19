"""以 SDK LEDC 类型检查生产背光实现；不访问实际 GPIO。"""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
from test_board_config import GPIO_STUB


def main():
    root = Path(__file__).resolve().parents[2]
    app = root / 'sdk/apps/esp_app'
    sdk = root / 'sdk/esp-idf/components'
    work = Path('E:/Codex/work' if os.name == 'nt' else '/mnt/e/Codex/work')
    work.mkdir(parents=True, exist_ok=True)
    # 直接编译生产文件中的背光部分；屏幕 SPI 和渲染由固件构建单独验证。
    production = (app / 'device/lcd_hal.c').read_text().split('static bool on_done', 1)[0]
    with tempfile.TemporaryDirectory(prefix='lcd-backlight-', dir=work) as directory:
        temp = Path(directory)
        stubs = {
            'sdkconfig.h': '#define CONFIG_IDF_TARGET_ESP32 1\n',
            'driver/gpio.h': GPIO_STUB,
            'driver/spi_master.h': '',
            'esp_intr_alloc.h': 'typedef void *intr_handle_t;\n',
            'esp_lcd_panel_io.h': '',
            'esp_lcd_panel_ops.h': 'typedef void *esp_lcd_panel_handle_t;\n',
            'esp_lcd_panel_vendor.h': '',
            'freertos/FreeRTOS.h': '',
            'freertos/semphr.h': 'typedef void *SemaphoreHandle_t;\n',
            'esp_check.h': '''#pragma once
#define ESP_RETURN_ON_ERROR(expr, ...) do { esp_err_t err = (expr); if (err) return err; } while (0)
''',
        }
        for name, content in stubs.items():
            file = temp / name
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_text(content)
        source = temp / 'check.c'
        source.write_text('''#include <assert.h>
#include <stddef.h>
#include "board_esp_proj_cfg.h"
#ifdef TEST_ACTIVE_LOW
#undef BOARD_LCD_BL_ACTIVE_LEVEL
#define BOARD_LCD_BL_ACTIVE_LEVEL 0
#endif
''' + production + '''
static esp_err_t timer_result, channel_result, duty_result, update_result;
static unsigned channel_calls, update_calls;
static uint32_t pending_duty, hardware_duty;
esp_err_t ledc_timer_config(const ledc_timer_config_t *timer)
{
 assert(timer->speed_mode==LEDC_LOW_SPEED_MODE);
 assert(timer->timer_num==LEDC_TIMER_0);
 assert(timer->freq_hz==20000 && timer->duty_resolution==10);
 return timer_result;
}
esp_err_t ledc_channel_config(const ledc_channel_config_t *channel)
{
 ++channel_calls;
 assert(channel->gpio_num==GPIO_NUM_16 && channel->duty==0);
 assert(channel->channel==LEDC_CHANNEL_0 && channel->timer_sel==LEDC_TIMER_0);
 assert(channel->flags.output_invert==(BOARD_LCD_BL_ACTIVE_LEVEL==0));
 return channel_result;
}
esp_err_t ledc_set_duty(ledc_mode_t mode, ledc_channel_t channel, uint32_t duty)
{
 assert(mode==LEDC_LOW_SPEED_MODE && channel==LEDC_CHANNEL_0);
 if (!duty_result) pending_duty=duty;
 return duty_result;
}
esp_err_t ledc_update_duty(ledc_mode_t mode, ledc_channel_t channel)
{
 assert(mode==LEDC_LOW_SPEED_MODE && channel==LEDC_CHANNEL_0);
 ++update_calls;
 if (!update_result) hardware_duty=pending_duty;
 return update_result;
}
int main(void)
{
 uint8_t percent=255;
 assert(panel==NULL && done==NULL);
 assert(lcd_hal_get_brightness(NULL)==ESP_ERR_INVALID_ARG);
 assert(lcd_hal_get_brightness(&percent)==ESP_ERR_INVALID_STATE && percent==255);
 assert(lcd_hal_set_brightness(0)==ESP_ERR_INVALID_STATE);
 assert(lcd_hal_set_brightness(101)==ESP_ERR_INVALID_ARG);
 timer_result=ESP_FAIL;
 assert(backlight_init()==ESP_FAIL && !backlight_ready && channel_calls==0);
 timer_result=ESP_OK; channel_result=ESP_FAIL;
 assert(backlight_init()==ESP_FAIL && !backlight_ready);
 channel_result=ESP_OK;
 assert(backlight_init()==ESP_OK && backlight_ready);
 assert(lcd_hal_get_brightness(&percent)==ESP_OK && percent==0);
 uint32_t previous=0;
 for (unsigned value=0; value<=100; ++value)
 {
  assert(lcd_hal_set_brightness(value)==ESP_OK);
  assert(lcd_hal_get_brightness(&percent)==ESP_OK && percent==value);
  assert(hardware_duty==(1024u*value+50u)/100u && hardware_duty>=previous);
  previous=hardware_duty;
 }
 assert(hardware_duty==1024);
 unsigned calls=update_calls;
 duty_result=ESP_FAIL;
 assert(lcd_hal_set_brightness(10)==ESP_FAIL && update_calls==calls);
 assert(lcd_hal_get_brightness(&percent)==ESP_OK && percent==100);
 duty_result=ESP_OK; update_result=ESP_FAIL;
 assert(lcd_hal_set_brightness(20)==ESP_FAIL && hardware_duty==1024);
 assert(lcd_hal_get_brightness(&percent)==ESP_OK && percent==100);
 update_result=ESP_OK;
 assert(lcd_hal_set_brightness(50)==ESP_OK && hardware_duty==512);
 assert(lcd_hal_get_brightness(&percent)==ESP_OK && percent==50);
 return 0;
}
''')
        executable = temp / ('check.exe' if os.name == 'nt' else 'check')
        includes = [temp, app / 'board/ESP32/board_esp32_speaker', app / 'device/include',
                    sdk / 'esp_driver_ledc/include', sdk / 'hal/include',
                    sdk / 'soc/esp32/include', sdk / 'soc/include', sdk / 'esp_common/include']
        command = shlex.split(os.environ.get('CC', 'cc')) + [
            '-std=c11', '-Wall', '-Wextra', '-Werror',
        ] + ['-I' + str(path) for path in includes] + [str(source), '-o', str(executable)]
        for options in ([], ['-DTEST_ACTIVE_LOW']):
            subprocess.run(command + options, check=True)
            subprocess.run([str(executable)], check=True)
    print('PASS: actual backlight code, SDK LEDC types, polarity, 0..100%, init/duty/update errors')


if __name__ == '__main__':
    main()
