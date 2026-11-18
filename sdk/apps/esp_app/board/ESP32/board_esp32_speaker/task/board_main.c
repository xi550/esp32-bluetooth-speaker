#include "board_esp_proj.h"
#include "speaker_service.h"
#include "esp_check.h"
#include "board_app.h"

void app_main(void) {
 ESP_ERROR_CHECK(board_init());
 ESP_ERROR_CHECK(speaker_service_start());
 app_start();
}
