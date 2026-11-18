#include "speaker_bsp.h"
#include "speaker_service.h"
#include "esp_check.h"
#include "app.h"
void app_start(void) {
 /* TODO(intern): create app state machine, key task and self-built UI task. */
}
void app_main(void) {
 ESP_ERROR_CHECK(speaker_bsp_init());
 ESP_ERROR_CHECK(speaker_service_start());
 app_start();
}
