#pragma once
/* Intern-owned entry point, called once after BSP and service initialization.
 * Create key/UI tasks here. Bluetooth/player/volume state and AVRCP are owned
 * by speaker_service.h: consume snapshots and call its control/volume APIs. */
void app_start(void);
