"""Host check of the production service with SDK types and fake transport/RTOS.

Run: python3 sdk/tools/test_speaker_service.py
Requires a host C compiler; never accesses a device. Temporary files default to
E:\\Codex\\work (Windows) or /mnt/e/Codex/work (WSL); override with --work-dir.
"""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
from test_board_config import GPIO_STUB


STUBS = {
    'driver/gpio.h': GPIO_STUB,
    'esp_bt.h': '''#pragma once
#include "esp_err.h"
typedef struct { int unused; } esp_bt_controller_config_t;
#define BT_CONTROLLER_INIT_CONFIG_DEFAULT() ((esp_bt_controller_config_t){0})
#define ESP_BT_MODE_BLE 1
#define ESP_BT_MODE_CLASSIC_BT 2
esp_err_t esp_bt_controller_mem_release(int mode);
esp_err_t esp_bt_controller_init(esp_bt_controller_config_t *cfg);
esp_err_t esp_bt_controller_enable(int mode);
''',
    'nvs_flash.h': '#pragma once\n#include "esp_err.h"\nesp_err_t nvs_flash_init(void);\n',
    'esp_check.h': '''#pragma once
#include "esp_err.h"
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#undef ESP_ERROR_CHECK
#define ESP_ERROR_CHECK(expr) assert((expr) == ESP_OK)
#define ESP_RETURN_ON_ERROR(expr, ...) do { esp_err_t e = (expr); if (e) return e; } while (0)
''',
    'freertos/FreeRTOS.h': '''#pragma once
#include <stdint.h>
#include <stddef.h>
typedef uint32_t TickType_t;
typedef int BaseType_t;
#define pdTRUE 1
#define pdPASS 1
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(ms) (ms)
''',
    'freertos/queue.h': '''#pragma once
#include "freertos/FreeRTOS.h"
typedef struct fake_queue *QueueHandle_t;
QueueHandle_t xQueueCreate(unsigned depth, unsigned size);
int xQueueSend(QueueHandle_t queue, const void *data, TickType_t timeout);
int xQueueOverwrite(QueueHandle_t queue, const void *data);
int xQueueReceive(QueueHandle_t queue, void *data, TickType_t timeout);
''',
    'freertos/semphr.h': '''#pragma once
#include "freertos/FreeRTOS.h"
typedef void *SemaphoreHandle_t;
int xSemaphoreTake(SemaphoreHandle_t mutex, TickType_t timeout);
int xSemaphoreGive(SemaphoreHandle_t mutex);
SemaphoreHandle_t xSemaphoreCreateMutex(void);
''',
    'freertos/task.h': '''#pragma once
#include "freertos/FreeRTOS.h"
TickType_t xTaskGetTickCount(void);
int xTaskCreate(void (*fn)(void *), const char *name, unsigned stack, void *arg, unsigned priority, void *handle);
''',
    'sdkconfig.h': '''#define CONFIG_BT_CLASSIC_ENABLED 1
#define CONFIG_BT_A2DP_ENABLE 1
#define CONFIG_BT_AVRCP_ENABLED 1
#define CONFIG_IDF_TARGET_ESP32 1
''',
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work-dir', type=Path, default=Path(
        'E:/Codex/work' if os.name == 'nt' else '/mnt/e/Codex/work'))
    args = parser.parse_args()
    args.work_dir.mkdir(parents=True, exist_ok=True)
    root = Path(__file__).resolve().parents[2]
    components = root / 'sdk/esp-idf/components'
    with tempfile.TemporaryDirectory(prefix='speaker-service-', dir=args.work_dir) as temp:
        temp = Path(temp)
        for name, contents in STUBS.items():
            path = temp / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(contents, encoding='utf-8')
        executable = temp / ('check.exe' if os.name == 'nt' else 'check')
        command = shlex.split(os.environ.get('CC', 'cc')) + [
            '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
            '-Wno-unused-function', '-ffunction-sections', '-fdata-sections',
            '-Wl,--gc-sections', '-I' + str(temp),
            '-I' + str(components / 'esp_common/include'),
            '-I' + str(components / 'soc/esp32/include'),
            '-I' + str(root / 'sdk/apps/esp_app/board/ESP32/board_esp32_speaker'),
            '-I' + str(components / 'bt/host/bluedroid/api/include/api'),
            '-I' + str(root / 'sdk/apps/esp_app/interface/include'),
            '-I' + str(root / 'sdk/apps/esp_app/device/include'),
            str(root / 'sdk/tools/test_speaker_service.c'), '-o', str(executable),
        ]
        for max_vol, step in ((50, 1), (1, 1), (127, 7)):
            subprocess.run(command + ['-DTEST_MASTER_MAX=' + str(max_vol),
                                      '-DTEST_MASTER_STEP=' + str(step)], check=True)
            subprocess.run([str(executable)], check=True)


if __name__ == '__main__':
    main()
