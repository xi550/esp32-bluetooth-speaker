"""编译真实按键扫描及分发代码，以虚拟时间检查消抖、长按和多键，无设备访问。"""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
from test_board_config import GPIO_STUB


def main():
    root = Path(__file__).resolve().parents[2]
    app = root / 'sdk/apps/esp_app'
    board = app / 'board/ESP32/board_esp32_speaker'
    work = Path('E:/Codex/work' if os.name == 'nt' else '/mnt/e/Codex/work')
    work.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='keys-', dir=work) as directory:
        temp = Path(directory)
        stubs = {
            'sdkconfig.h': '#define CONFIG_IDF_TARGET_ESP32 1\n',
            'driver/gpio.h': GPIO_STUB,
            'esp_log.h': '#define ESP_LOGE(...) ((void)0)\n',
            'esp_timer.h': '#include <stdint.h>\nint64_t esp_timer_get_time(void);\n',
            'freertos/FreeRTOS.h': '''#pragma once
#include <stdint.h>
#include <stddef.h>
typedef unsigned TickType_t;
#define pdTRUE 1
#define pdPASS 1
#define portMAX_DELAY UINT32_MAX
#define configTICK_RATE_HZ 100
#define pdMS_TO_TICKS(ms) ((ms)/10)
''',
            'freertos/queue.h': '''#pragma once
#include "freertos/FreeRTOS.h"
typedef void *QueueHandle_t;
QueueHandle_t xQueueCreate(unsigned count, unsigned size);
int xQueueSend(QueueHandle_t q, const void *item, TickType_t wait);
int xQueueReceive(QueueHandle_t q, void *item, TickType_t wait);
unsigned uxQueueSpacesAvailable(QueueHandle_t q);
void vQueueDelete(QueueHandle_t q);
''',
            'freertos/task.h': '''#pragma once
#include "freertos/FreeRTOS.h"
TickType_t xTaskGetTickCount(void);
void xTaskDelayUntil(TickType_t *wake, TickType_t period);
void vTaskDelay(TickType_t ticks);
int xTaskCreate(void (*fn)(void *), const char *name, unsigned stack, void *arg, unsigned priority, void *handle);
''',
        }
        for name, content in stubs.items():
            file = temp / name
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_text(content)
        source = temp / 'check.c'
        source.write_text('''#include <assert.h>
#include <string.h>
#include "''' + str(board / 'keypad/board_keyevent.c') + '''"
#include "''' + str(board / 'system/board_app.c') + '''"
static board_key_event_t queued[32];
static unsigned head, count, creates, deletes, task_calls, input_calls, retries;
static bool initialized, fail_queue, fail_task;
static TickType_t waited;
static speaker_ui_key_t last_key;
static uint32_t last_time;
QueueHandle_t xQueueCreate(unsigned n, unsigned size)
{
 assert(n==32 && size==sizeof(board_key_event_t));
 ++creates;
 return fail_queue ? NULL : queued;
}
int xQueueSend(QueueHandle_t q, const void *item, TickType_t wait)
{
 assert(q==queued && wait==0);
 if (count==32) return 0;
 queued[(head+count++)%32]=*(const board_key_event_t *)item;
 return pdTRUE;
}
int xQueueReceive(QueueHandle_t q, void *item, TickType_t wait)
{
 assert(q==queued); waited=wait;
 if (!count) return 0;
 *(board_key_event_t *)item=queued[head]; head=(head+1)%32; --count;
 return pdTRUE;
}
unsigned uxQueueSpacesAvailable(QueueHandle_t q) { return 32-count; }
void vQueueDelete(QueueHandle_t q) { ++deletes; }
int xTaskCreate(void (*fn)(void *), const char *name, unsigned stack, void *arg, unsigned priority, void *handle)
{ ++task_calls; return fail_task ? 0 : pdPASS; }
TickType_t xTaskGetTickCount(void) { return 0; }
void xTaskDelayUntil(TickType_t *wake, TickType_t period) { }
void vTaskDelay(TickType_t ticks) { assert(ticks==1); }
int64_t esp_timer_get_time(void) { return 0; }
esp_err_t buttons_read(unsigned i, bool *pressed)
{ *pressed=false; return initialized ? ESP_OK : ESP_ERR_INVALID_STATE; }
esp_err_t lvgl_port_input_at(speaker_ui_key_t key, uint32_t now)
{
 ++input_calls; last_key=key; last_time=now;
 if (retries) { --retries; return ESP_ERR_NOT_FINISHED; }
 return ESP_OK;
}
static void take(uint16_t code, uint32_t time)
{
 board_key_event_t event;
 assert(board_keys_wait(&event,0)==ESP_OK);
 assert(event.code==code && event.time_ms==time);
}
int main(void)
{
 board_key_event_t event;
 assert(board_keys_wait(NULL,0)==ESP_ERR_INVALID_ARG);
 assert(board_keys_wait(&event,0)==ESP_ERR_INVALID_STATE);
 assert(board_keys_start()==ESP_ERR_INVALID_STATE);
 initialized=true; fail_queue=true;
 assert(board_keys_start()==ESP_ERR_NO_MEM && !events);
 fail_queue=false; fail_task=true;
 assert(board_keys_start()==ESP_ERR_NO_MEM && !events && deletes==1);
 fail_task=false;
 assert(app_start()==ESP_OK && task_calls==3);
 assert(board_keys_start()==ESP_ERR_INVALID_STATE);
 assert(board_keys_wait(&event,1)==ESP_ERR_TIMEOUT && waited==1);
 assert(board_keys_wait(&event,UINT32_MAX)==ESP_ERR_TIMEOUT && waited==portMAX_DELAY);
 /* 抖动必须连续稳定 30ms 后才按下，松手也需消抖。 */
 scan_key(3,true,0); scan_key(3,false,10); scan_key(3,true,20);
 scan_key(3,true,49); assert(count==0);
 scan_key(3,true,50); take(KRS(RKEY_VOL_UP),50);
 scan_key(3,false,60); scan_key(3,true,70); scan_key(3,true,100); assert(count==0);
 scan_key(3,false,110); scan_key(3,false,139); assert(count==0);
 scan_key(3,false,140); take(KRU(RKEY_VOL_UP),140);
 /* 长按门限、连发周期、调度落后不补发，长按松手只发 KRLU。 */
 scan_key(3,true,200); scan_key(3,true,230); take(KRS(RKEY_VOL_UP),230);
 scan_key(3,true,829); assert(count==0);
 scan_key(3,true,830); take(KRL(RKEY_VOL_UP),830);
 scan_key(3,true,979); assert(count==0);
 scan_key(3,true,980); take(KRH(RKEY_VOL_UP),980);
 scan_key(3,true,3000); take(KRH(RKEY_VOL_UP),3000); assert(count==0);
 scan_key(3,false,3150); scan_key(3,false,3180); take(KRLU(RKEY_VOL_UP),3180);
 /* 多键独立；32 位时钟回绕不改变消抖与长按。 */
 memset(keys,0,sizeof(keys));
 uint32_t start=UINT32_MAX-15;
 scan_key(0,true,start); scan_key(1,true,start);
 scan_key(0,true,start+30u); scan_key(1,true,start+30u);
 take(KRS(RKEY_UP),start+30u); take(KRS(RKEY_DOWN),start+30u);
 scan_key(0,true,start+630u); take(KRL(RKEY_UP),start+630u);
 scan_key(0,true,start+780u); take(KRH(RKEY_UP),start+780u);
 /* 积压时省略连发，保留五键按下及松手空间。 */
 for (unsigned i=0;i<22;++i) post_event(KRS(RKEY_UP),i);
 post_event(KRH(RKEY_UP),22); assert(count==22);
 post_event(KRU(RKEY_UP),23); assert(count==23);
 head=count=0;
 /* 使用生产分发函数验证单双击只收到按下，音量/方向可连发，重试保留时间。 */
#if BOARD_LCD_ENABLE
 app_key_event(KRS(RKEY_CONFIRM),123); assert(input_calls==1 && last_key==SPEAKER_UI_KEY_CONFIRM && last_time==123);
 app_key_event(KRL(RKEY_CONFIRM),200); app_key_event(KRH(RKEY_CONFIRM),250); app_key_event(KRLU(RKEY_CONFIRM),300);
 assert(input_calls==1);
 retries=2; app_key_event(KRH(RKEY_VOL_UP),400);
 assert(input_calls==4 && last_key==SPEAKER_UI_KEY_VOLUME_UP && last_time==400);
 app_key_event(KRS(RKEY_VOL_DOWN),500); assert(last_key==SPEAKER_UI_KEY_VOLUME_DOWN);
 app_key_event(KRH(RKEY_UP),600); assert(last_key==SPEAKER_UI_KEY_UP);
 app_key_event(KRH(RKEY_DOWN),700); assert(last_key==SPEAKER_UI_KEY_DOWN);
#else
 app_key_event(KRS(RKEY_VOL_UP),123); assert(input_calls==0);
#endif
 return 0;
}
''')
        executable = temp / 'check'
        command = shlex.split(os.environ.get('CC', 'cc')) + [
            '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
            '-Wno-unused-variable', '-fsanitize=undefined', '-I'+str(temp),
            '-I'+str(board), '-I'+str(app/'device/include'), '-I'+str(app/'ui'),
            '-I'+str(root/'sdk/esp-idf/components/esp_common/include'),
            '-I'+str(root/'sdk/esp-idf/components/soc/esp32/include'),
            str(source), '-o', str(executable),
        ]
        for lcd in (0, 1):
            subprocess.run(command+['-DBOARD_LCD_ENABLE='+str(lcd)], check=True)
            subprocess.run([str(executable)], check=True)
    print('PASS: key startup/failures, debounce, short/long/repeat/release, multi-key, clock wrap, queue reserve and UI dispatch (LCD on/off)')


if __name__ == '__main__':
    main()
