#include <string.h>
#include <stdatomic.h>
#include "speaker_service.h"
#include "audio_output.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"
#include "esp_a2dp_legacy_api.h"
#include "nvs_flash.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#if !CONFIG_BT_CLASSIC_ENABLED || !CONFIG_BT_A2DP_ENABLE || defined(CONFIG_BT_A2DP_USE_EXTERNAL_CODEC)
#error "Requires Classic A2DP with internal SBC decoder"
#endif
#define PCM_BLOCK 2048
#define PCM_DEPTH 12
typedef struct {uint32_t rate,epoch,len;uint8_t data[PCM_BLOCK];} pcm_block_t;
static QueueHandle_t pcm_queue;
static atomic_uint sample_rate,epoch,dropped;
static void pcm_cb(const uint8_t *data,uint32_t len) {
 uint32_t generation=atomic_load(&epoch),hz=atomic_load(&sample_rate);
 if(generation!=atomic_load(&epoch)) return;
 if(!hz || !data || len%4) return;
 while(len) {
  /* IDF invokes this callback serially from its sink task. Avoid a 2KB stack buffer. */
  static pcm_block_t block;
  block.rate=hz;block.epoch=generation;
  block.len=len>PCM_BLOCK ? PCM_BLOCK : len;
  memcpy(block.data,data,block.len);
  if(xQueueSend(pcm_queue,&block,0)!=pdTRUE) {atomic_fetch_add(&dropped,1);return;}
  data+=block.len;len-=block.len;
 }
}
static void audio_worker(void *arg) {
 pcm_block_t block;
 for(;;) {
  if(xQueueReceive(pcm_queue,&block,pdMS_TO_TICKS(1000))==pdTRUE && block.epoch==atomic_load(&epoch)) {
   ESP_ERROR_CHECK(audio_output_set_rate(block.rate));
   esp_err_t err=audio_output_write(block.data,block.len);
   if(err!=ESP_OK) ESP_LOGW("audio","I2S write: %s",esp_err_to_name(err));
  }
  unsigned lost=atomic_exchange(&dropped,0);
  if(lost) ESP_LOGW("audio","PCM overflow: %u blocks",lost);
 }
}
static void a2dp_cb(esp_a2d_cb_event_t event,esp_a2d_cb_param_t *p) {
 if(event==ESP_A2D_AUDIO_CFG_EVT) {
  atomic_store(&sample_rate,0);atomic_fetch_add(&epoch,1);
  if(p->audio_cfg.mcc.type!=ESP_A2D_MCT_SBC) return;
  esp_a2d_cie_sbc_t cfg=p->audio_cfg.mcc.cie.sbc_info;
  if(cfg.ch_mode&ESP_A2D_SBC_CIE_CH_MODE_MONO) {
   ESP_LOGW("a2dp","Mono is unsupported by stereo baseline");return;
  }
  uint32_t hz=(cfg.samp_freq&ESP_A2D_SBC_CIE_SF_48K)?48000:
   (cfg.samp_freq&ESP_A2D_SBC_CIE_SF_44K)?44100:
   (cfg.samp_freq&ESP_A2D_SBC_CIE_SF_32K)?32000:
   (cfg.samp_freq&ESP_A2D_SBC_CIE_SF_16K)?16000:0;
  atomic_store(&sample_rate,hz);
  ESP_LOGI("a2dp","Stereo PCM16: %lu Hz",(unsigned long)hz);
 } else if(event==ESP_A2D_CONNECTION_STATE_EVT) {
  ESP_LOGI("a2dp","Connection: %d",p->conn_stat.state);
  if(p->conn_stat.state==ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
   atomic_store(&sample_rate,0);atomic_fetch_add(&epoch,1);
  }
 } else if(event==ESP_A2D_PROF_STATE_EVT) {
  if(p->a2d_prof_stat.init_state==ESP_A2D_INIT_SUCCESS) {
   ESP_ERROR_CHECK(esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE,ESP_BT_GENERAL_DISCOVERABLE));
  } else {
   ESP_LOGE("a2dp","Profile state: %d",p->a2d_prof_stat.init_state);
  }
 } else if(event==ESP_A2D_AUDIO_STATE_EVT) {
  ESP_LOGI("a2dp","Audio state: %d",p->audio_stat.state);
 }
}
esp_err_t speaker_service_start(void) {
 if(pcm_queue) return ESP_ERR_INVALID_STATE;
 /* Do not silently erase user NVS. Diagnose initialization failure explicitly. */
 ESP_RETURN_ON_ERROR(nvs_flash_init(),"bt","NVS init; inspect before erase");
 pcm_queue=xQueueCreate(PCM_DEPTH,sizeof(pcm_block_t));if(!pcm_queue) return ESP_ERR_NO_MEM;
 if(xTaskCreate(audio_worker,"audio_tx",4096,NULL,5,NULL)!=pdPASS) return ESP_ERR_NO_MEM;
 ESP_RETURN_ON_ERROR(esp_bt_controller_mem_release(ESP_BT_MODE_BLE),"bt","release BLE");
 esp_bt_controller_config_t cfg=BT_CONTROLLER_INIT_CONFIG_DEFAULT();
 ESP_RETURN_ON_ERROR(esp_bt_controller_init(&cfg),"bt","controller");
 ESP_RETURN_ON_ERROR(esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT),"bt","enable");
 ESP_RETURN_ON_ERROR(esp_bluedroid_init(),"bt","bluedroid");
 ESP_RETURN_ON_ERROR(esp_bluedroid_enable(),"bt","host");
 esp_bt_io_cap_t cap=ESP_BT_IO_CAP_NONE;
 ESP_RETURN_ON_ERROR(esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE,&cap,sizeof(cap)),"bt","pairing");
 ESP_RETURN_ON_ERROR(esp_bt_gap_set_device_name(CONFIG_SPEAKER_BT_NAME),"bt","name");
 ESP_RETURN_ON_ERROR(esp_a2d_register_callback(a2dp_cb),"bt","events");
 ESP_RETURN_ON_ERROR(esp_a2d_sink_init(),"bt","sink");
 return esp_a2d_sink_register_data_callback(pcm_cb);
}
