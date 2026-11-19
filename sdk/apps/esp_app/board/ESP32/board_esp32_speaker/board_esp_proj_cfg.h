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

/* 蓝牙设备名称与启动时的 PCM 音量：0 为静音，127 保持原始幅度。
 * 手机的绝对音量命令可以覆盖此启动音量。 */
#define BOARD_BT_NAME        "ESP32-Speaker-Lab"
#define BOARD_INITIAL_VOLUME 40

/* I2S Philips 格式、PCM16 立体声：两块 MAX98357A 共用 BCLK/WS/DIN。
 * 左右声道、GAIN 和 SD_MODE 由硬件接线决定，不通过 GPIO 软件控制。 */
#define BOARD_I2S_BCLK  GPIO_NUM_26
#define BOARD_I2S_LRCLK GPIO_NUM_25
#define BOARD_I2S_DOUT  GPIO_NUM_22

/* 五个独立按键；序号对应物理按键，不代表产品功能。 */
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

/* ST7789 使用只写 SPI；背光接线尚未确认，因此不分配 BL GPIO。
 * 屏幕尺寸、颜色顺序和方向需要实板确认。
 * 检查板级配置时，也可通过编译器宏覆盖 BOARD_LCD_ENABLE。 */
#ifndef BOARD_LCD_ENABLE
#define BOARD_LCD_ENABLE 0
#endif
#define BOARD_LCD_WIDTH        240
#define BOARD_LCD_HEIGHT       240
#define BOARD_LCD_X_GAP        0
#define BOARD_LCD_Y_GAP        0
#define BOARD_LCD_INVERT       0
#define BOARD_LCD_SCLK         GPIO_NUM_18
#define BOARD_LCD_MOSI         GPIO_NUM_23
#define BOARD_LCD_CS           GPIO_NUM_21
#define BOARD_LCD_DC           GPIO_NUM_19
#define BOARD_LCD_RST          GPIO_NUM_17
#define BOARD_LCD_SPI_HOST     SPI2_HOST
#define BOARD_LCD_SPI_CLOCK_HZ (10 * 1000 * 1000)
#define BOARD_LCD_SPI_MODE     0
#define BOARD_LCD_COLOR_ORDER  LCD_RGB_ELEMENT_ORDER_RGB
#define BOARD_LCD_SWAP_XY      false
#define BOARD_LCD_MIRROR_X     false
#define BOARD_LCD_MIRROR_Y     false
