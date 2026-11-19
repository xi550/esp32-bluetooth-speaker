#pragma once

/* 实习生负责的应用入口，在板级硬件和服务初始化后调用一次。
 * 在这里创建按键和界面任务；蓝牙连接、播放、音量状态及 AVRCP 由服务维护。
 * 通过 speaker_service.h 接收状态快照，并调用播放控制和音量接口。 */
void app_start(void);
