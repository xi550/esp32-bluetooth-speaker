#pragma once

#include "sdkconfig.h"
#include "driver/gpio.h"

#if !CONFIG_IDF_TARGET_ESP32
#error "Only classic ESP32 is supported"
#endif

/* 经典 ESP32-WROOM-32E/32D 的板级接线；GPIO 编号不是模组焊盘序号。
 * GPIO6～11 用于 Flash；GPIO34～39 只能输入，且没有内部上拉或下拉。
 * GPIO0/2/5/12/15 的复位电平影响启动配置；GPIO1/3 通常用于串口控制台。
 * GPIO13/14 与 JTAG 复用；在带 PSRAM 的模组上使用 GPIO16/17 前需重新核对接线。
 * 产品参数统一在本文件配置；sdkconfig.h 仅提供 ESP-IDF 的芯片型号、蓝牙协议栈、
 * Flash 等系统配置。 */

/* 固件版本号同时用于构建描述和设备信息页，最多 31 个 ASCII 字符。 */
#define BOARD_FIRMWARE_VERSION "0.1.0"

/* 蓝牙设备名称与启动时的 PCM 音量：0 为静音，127 保持原始幅度。
 * 手机的绝对音量命令可以覆盖此启动音量。 */
#define BOARD_BT_NAME        "ESP32-Speaker-Lab"
#define BOARD_INITIAL_VOLUME 40

/* I2S Philips 格式、PCM16 立体声：两块 MAX98357A 共用 BCLK/WS/DIN。
 * 左右声道、GAIN 和 SD_MODE 由硬件接线决定，不通过 GPIO 软件控制。 */
#define BOARD_I2S_BCLK  GPIO_NUM_26
#define BOARD_I2S_LRCLK GPIO_NUM_25
#define BOARD_I2S_DOUT  GPIO_NUM_22

/* 五个独立按键；物理序号映射为逻辑键值，与业务处理分离。 */
#define BOARD_KEY0_PIN     GPIO_NUM_13
#define BOARD_KEY1_PIN     GPIO_NUM_14
#define BOARD_KEY2_PIN     GPIO_NUM_27
#define BOARD_KEY3_PIN     GPIO_NUM_32
#define BOARD_KEY4_PIN     GPIO_NUM_33
#define BOARD_BUTTON_COUNT 5
#define BOARD_BUTTON_PINS                                                                          \
    {BOARD_KEY0_PIN, BOARD_KEY1_PIN, BOARD_KEY2_PIN, BOARD_KEY3_PIN, BOARD_KEY4_PIN}
#define BOARD_KEY_ACTIVE_LEVEL 0
#define BOARD_KEY_PULL_UP      GPIO_PULLUP_ENABLE
#define BOARD_KEY_PULL_DOWN    GPIO_PULLDOWN_DISABLE

#define BOARD_KEY_UP_INDEX          0
#define BOARD_KEY_DOWN_INDEX        1
#define BOARD_KEY_CONFIRM_INDEX     2
#define BOARD_KEY_VOLUME_UP_INDEX   3
#define BOARD_KEY_VOLUME_DOWN_INDEX 4

/* 扫描、连续稳定消抖、长按门限和按住连发周期，单位毫秒。
 * 按下立即产生 KRS，持续按住产生一次 KRL，此后每个周期产生 KRH。 */
#define BOARD_KEY_SCAN_MS     10
#define BOARD_KEY_DEBOUNCE_MS 30
#define BOARD_KEY_LONG_MS     600
#define BOARD_KEY_HOLD_MS     150

/* 单双击间隔及调节步长；音量和亮度均使用百分比。 */
#define BOARD_UI_DOUBLE_CLICK_MS 300
#define BOARD_UI_VOLUME_STEP     5
#define BOARD_UI_BRIGHTNESS_STEP 10
/* 100Hz 系统下使用 30ms 调度，避免 33ms 被轮询粒度推迟至 40ms。
 * 这是局部刷新约 30 帧的调度目标，实际帧率仍需实测。 */
#define BOARD_UI_REFRESH_MS   30
#define BOARD_UI_BUFFER_LINES 24

/* ST7789V2 使用只写 SPI，时钟设为 40MHz。
 * 屏幕为 1.69 英寸、240×280；窗口偏移、颜色顺序和方向仍需实板确认。
 * 检查板级配置时，也可通过编译器宏覆盖 BOARD_LCD_ENABLE。 */
#ifndef BOARD_LCD_ENABLE
#define BOARD_LCD_ENABLE 1
#endif
#define BOARD_LCD_WIDTH        240
#define BOARD_LCD_HEIGHT       280
#define BOARD_LCD_X_GAP        0
#define BOARD_LCD_Y_GAP        0
#define BOARD_LCD_INVERT       0
#define BOARD_LCD_SCLK         GPIO_NUM_18
#define BOARD_LCD_MOSI         GPIO_NUM_23
#define BOARD_LCD_CS           GPIO_NUM_21
#define BOARD_LCD_DC           GPIO_NUM_19
#define BOARD_LCD_RST          GPIO_NUM_17
#define BOARD_LCD_SPI_HOST     SPI2_HOST
#define BOARD_LCD_SPI_CLOCK_HZ (40 * 1000 * 1000)
#define BOARD_LCD_SPI_MODE     0
#define BOARD_LCD_COLOR_ORDER  LCD_RGB_ELEMENT_ORDER_RGB
#define BOARD_LCD_SWAP_XY      false
#define BOARD_LCD_MIRROR_X     false
#define BOARD_LCD_MIRROR_Y     false

/* GPIO16 分配为背光 PWM 控制信号，不作为 LED 大电流供电端。
 * 卖家说明 BLK 为 GPIO 控制输入，低电平关闭、默认打开，采用高有效配置。
 * 卖家未提供 PWM 频率范围，20kHz 的实际调光效果仍需实板验证。
 * PWM 占用 LEDC 低速组的定时器 0、通道 0，其他模块不要复用。
 * 初始化先关闭背光，UI 首帧发送完成后设置启动亮度。 */
#define BOARD_LCD_BL_PIN          GPIO_NUM_16
#define BOARD_LCD_BL_ACTIVE_LEVEL 1
#define BOARD_LCD_BL_PWM_HZ       20000
#define BOARD_LCD_BL_PWM_BITS     10
#define BOARD_INITIAL_BRIGHTNESS  60
