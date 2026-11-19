"""Check the real board validation and button driver with fake GPIO, no device.
Run: python3 sdk/tools/test_board_config.py (host C compiler required).
"""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile


GPIO_STUB = '''#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "soc/gpio_num.h"
#include "soc/soc_caps.h"
#include "esp_bit_defs.h"
#include "esp_err.h"
#define GPIO_PULLUP_ENABLE 1
#define GPIO_PULLUP_DISABLE 0
#define GPIO_PULLDOWN_ENABLE 1
#define GPIO_PULLDOWN_DISABLE 0
#define GPIO_MODE_INPUT 1
#define GPIO_INTR_DISABLE 0
#define GPIO_IS_VALID_GPIO(pin) ((pin) >= 0 && ((1ULL << (pin)) & SOC_GPIO_VALID_GPIO_MASK) != 0)
#define GPIO_IS_VALID_OUTPUT_GPIO(pin) ((pin) >= 0 && ((1ULL << (pin)) & SOC_GPIO_VALID_OUTPUT_GPIO_MASK) != 0)
typedef struct { uint64_t pin_bit_mask; int mode, pull_up_en, pull_down_en, intr_type; } gpio_config_t;
esp_err_t gpio_config(const gpio_config_t *config);
int gpio_get_level(gpio_num_t pin);
'''

def main():
    root = Path(__file__).resolve().parents[2]
    board = root / 'sdk/apps/esp_app/board/ESP32/board_esp32_speaker'
    work = Path('E:/Codex/work' if os.name == 'nt' else '/mnt/e/Codex/work')
    work.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='board-config-', dir=work) as directory:
        temp = Path(directory)
        (temp / 'driver').mkdir()
        (temp / 'sdkconfig.h').write_text('#define CONFIG_IDF_TARGET_ESP32 1\n')
        (temp / 'driver/gpio.h').write_text(GPIO_STUB)
        (temp / 'esp_check.h').write_text('''#pragma once
#include "esp_err.h"
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_RETURN_ON_ERROR(expr, ...) do { esp_err_t e = (expr); if (e) return e; } while (0)
''')
        source = temp / 'check.c'
        source.write_text(
            '#include "board_esp_proj_cfg.h"\n'
            '#ifdef TEST_BL_BAD_PIN\n#undef BOARD_LCD_BL_PIN\n'
            '#define BOARD_LCD_BL_PIN ((gpio_num_t)TEST_BL_BAD_PIN)\n#endif\n'
            '#include "' + str(board / 'board_esp_proj.c') + '"\n'
            '#include "' + str(root / 'sdk/apps/esp_app/device/buttons.c') + '"\n' + '''
static unsigned init_calls;
static int level;
static gpio_config_t configured;
esp_err_t gpio_config(const gpio_config_t *c) { configured=*c; ++init_calls; return ESP_OK; }
int gpio_get_level(gpio_num_t pin) { return level; }
esp_err_t audio_output_init(void) { ++init_calls; return ESP_OK; }
esp_err_t lcd_hal_init(void) { ++init_calls; return ESP_OK; }
int main(void) {
#if defined(TEST_BL_BAD_PIN) && BOARD_LCD_ENABLE
 assert(board_init()==ESP_ERR_INVALID_ARG);
 assert(init_calls==0);
 return 0;
#endif
 bool pressed;
 assert(buttons_read(0,&pressed)==ESP_ERR_INVALID_STATE);
 assert(board_init()==ESP_OK);
#if BOARD_LCD_ENABLE
 assert(init_calls==3);
#else
 assert(init_calls==2);
#endif
 assert(configured.pull_up_en==BOARD_KEY_PULL_UP && configured.pull_down_en==BOARD_KEY_PULL_DOWN);
 assert(configured.pin_bit_mask==((1ULL<<13)|(1ULL<<14)|(1ULL<<27)|(1ULL<<32)|(1ULL<<33)));
 level=BOARD_KEY_ACTIVE_LEVEL; assert(buttons_read(0,&pressed)==ESP_OK && pressed);
 level=!BOARD_KEY_ACTIVE_LEVEL; assert(buttons_read(4,&pressed)==ESP_OK && !pressed);
 assert(buttons_read(5,&pressed)==ESP_ERR_INVALID_ARG);
 assert(buttons_read(0,NULL)==ESP_ERR_INVALID_ARG);
 gpio_num_t pair[]={GPIO_NUM_26,GPIO_NUM_13};
 assert(validate_pins(pair,1,2,true)==ESP_OK);
 pair[1]=GPIO_NUM_26; assert(validate_pins(pair,1,2,true)==ESP_ERR_INVALID_ARG);
 pair[1]=GPIO_NUM_34; assert(validate_pins(pair,1,2,true)==ESP_ERR_INVALID_ARG);
 assert(validate_pins(pair,1,2,false)==ESP_OK);
 pair[0]=GPIO_NUM_34; assert(validate_pins(pair,1,2,false)==ESP_ERR_INVALID_ARG);
 pair[0]=GPIO_NUM_6; assert(validate_pins(pair,1,2,false)==ESP_ERR_INVALID_ARG);
 pair[0]=GPIO_NUM_NC; assert(validate_pins(pair,1,2,false)==ESP_ERR_INVALID_ARG);
 pair[0]=GPIO_NUM_MAX; assert(validate_pins(pair,1,2,false)==ESP_ERR_INVALID_ARG);
 pair[0]=(gpio_num_t)24; assert(validate_pins(pair,1,2,false)==ESP_ERR_INVALID_ARG);
 pair[0]=GPIO_NUM_0; assert(validate_pins(pair,1,2,false)==ESP_OK);
 return 0;
}
''')
        executable = temp / ('check.exe' if os.name == 'nt' else 'check')
        command = shlex.split(os.environ.get('CC', 'cc')) + [
            '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
            '-I' + str(temp), '-I' + str(board),
            '-I' + str(root / 'sdk/esp-idf/components/esp_common/include'),
            '-I' + str(root / 'sdk/esp-idf/components/soc/esp32/include'),
            '-I' + str(root / 'sdk/apps/esp_app/device/include'),
            str(source), '-o', str(executable),
        ]
        for lcd_enabled in (False, True):
            subprocess.run(command + ['-DBOARD_LCD_ENABLE=' + str(int(lcd_enabled))], check=True)
            subprocess.run([str(executable)], check=True)
        for pin in (26, 34, 6):
            for enabled in (0, 1):
                subprocess.run(command + ['-DBOARD_LCD_ENABLE=' + str(enabled),
                                          '-DTEST_BL_BAD_PIN=' + str(pin)], check=True)
                subprocess.run([str(executable)], check=True)
    print('PASS: board init with LCD on/off, GPIO validation/conflicts/reserved/pulls and raw button levels')


if __name__ == '__main__':
    main()
