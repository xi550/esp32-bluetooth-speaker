# ESP32 蓝牙音箱基础框架

工程：`E:\Codex\outputs\esp32-speaker-foundation`。本次新建独立目录，没有覆盖已有工程。

目标是 ESP32-WROOM-32E/32D，构建目标为 `esp32`，使用 Bluedroid Classic Bluetooth/A2DP Sink。不能改成 ESP32-S3/C3；这两个系列不满足本项目的经典蓝牙要求。实际模组型号仍需查看板上丝印确认。

## SDK 与兼容性

- 官方仓库：https://github.com/espressif/esp-idf
- 固定稳定版本：v5.5.6；commit `880a203ae603d08a94d06aa01e0d6761958e5e9e`。
- SDK：`E:\Codex\work\esp32-speaker-foundation\esp-idf-v5.5.6`。
- 官方参考：同一 tag 下的 `examples/bluetooth/bluedroid/classic_bt/a2dp_sink_stream`，尤其是 main/main.c、common/bredr_app_common_utils 与 common/a2dp_utils/a2dp_sink_int_codec_utils。
- 参考该版本官方初始化顺序与内部 SBC 解码模式，工程直接依赖官方 `bt`、`esp_driver_i2s`、`esp_lcd` 等组件；没有复制整个带业务行为的演示工程。
- 当前使用 `esp_a2d_sink_register_data_callback()`：接收内部解码后的 PCM。该接口在 5.5.6 的 legacy 头中，仍被该版本官方内部解码示例使用。它已标记为未来移除，因此升级 IDF 必须重新评估。
- 新的 `esp_a2d_sink_register_audio_data_callback()` 接收未解码音频，不能直接接到本项目 PCM/I2S 输出。当前明确关闭外部解码器配置。
- `sdk.lock.json` 记录版本；构建脚本核对 commit，工程 CMake 核对版本和目标。

## 目录与职责

```text
esp32-speaker-foundation/
  CMakeLists.txt              工程入口、目标与版本检查
  sdkconfig.defaults         Classic BT/A2DP、4MB Flash 默认值
  partitions.csv             NVS/PHY/3MB factory；无 OTA
  sdk.lock.json              SDK 来源与 commit
  components/
    board/include/board_pins.h 单一引脚定义源
    speaker_drivers/          I2S 输出、ST7789 flush、原始按键
    speaker_bsp/              驱动启动编排
    speaker_service/          A2DP 接收、PCM 队列、音频工作任务
  main/
    app_main.c               启动入口及应用接入位置
    app_ui.c                 空白学习任务
    app_controls.c           空白学习任务
  tools/                     setup.ps1、build.ps1；Linux 使用 setup.sh、build.sh
  DOCUMENTATION.txt          外置 Markdown 文档位置
```

`app → service/BSP → driver → board`；底层不依赖应用。按键和显示接口可由应用直接调用；禁止驱动回调里操作页面或处理曲目。

基础数据路径：手机 SBC → 官方 Bluedroid 解码 → 借用 PCM 回调 → 复制到有界队列 → audio_tx 工作任务 → I2S Philips、16bit、双声道。队列满时丢弃当前回调剩余数据并记录溢出；回调不阻塞等待 I2S。采样率随 SBC 协商切换，连接代号用于丢弃旧队列数据。

左右声道按原有交错立体声顺序发送，两块 MAX98357A 的 SD_MODE 硬件决定各自取哪个声道。软件不拆分或重排左右声道。当前单声道协商只记录不支持并丢弃音频，后续需单独评估，不能宣称支持所有音源格式。

## 应用层边界

已提供底层启动和真实音频接入点；默认广播名称为 ESP32-Speaker-Lab。没有实现音量缩放、AVRCP、曲目管理、播放控制、按键行为、UI 页面、字体、绘图器或业务状态机，也没有引入 LVGL 等 UI 框架。

LCD 默认不初始化；尺寸、偏移、颜色反转可在 menuconfig 配置。按键仅提供原始电平。服务启动一次，失败由 app_main 终止启动；动态停止/销毁、重试恢复不是当前接口能力。

Markdown 文档按用户约定集中存放在桌面 doc 目录：同名前缀的 BUILD、HANDOFF、HARDWARE，以及构建验收记录。工程根目录 README.txt 与 docs 下的文本副本用于便携交接。

官方资料：[v5.5.6 发布说明](https://github.com/espressif/esp-idf/releases/tag/v5.5.6)、[该版本 A2DP API](https://docs.espressif.com/projects/esp-idf/en/v5.5.6/esp32/api-reference/bluetooth/esp_a2dp.html)、[工程构建系统](https://docs.espressif.com/projects/esp-idf/en/v5.5.6/esp32/api-guides/build-system.html)。

## Linux / WSL 编译

需要 Git、Python >= 3.9（含 venv）、Ninja，以及官方 Linux 依赖。
setup.sh 会额外安装 SDK 推荐的 CMake 3.30.2，避免 Ubuntu 20.04 自带 CMake 3.16 导致蓝牙库链接路径错误：

```bash
sudo apt-get update
sudo apt-get install git wget flex bison gperf python3 python3-pip python3-venv cmake ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0
# Ubuntu 20.04 默认 Python 3.8，还需要并行安装受支持的版本：
sudo apt-get install python3.9 python3.9-venv
bash tools/setup.sh
bash tools/build.sh
bash tools/build.sh menuconfig
bash tools/build.sh size
# 有设备且确认串口后：
bash tools/build.sh -p /dev/ttyUSB0 flash monitor
```

Linux 默认安装 SDK 到 `~/esp/esp-idf-v5.5.6`，工具链与 Python 环境放在
`~/.espressif`，构建缓存放在当前工程的 `build/linux`；WSL 下全部位于 Linux 文件系统。
Windows 继续使用原有 PowerShell 脚本及 E 盘上的 `idf-tools` 与 `build`，两端不共用 SDK、工具或构建缓存。
sdkconfig 是工程配置，两端共享；SDK 版本仍由 sdk.lock.json 校验。
安装脚本不会替换已有的不同版本 SDK，也不会修改系统默认 Python。
脚本使用官方 install.sh/export.sh；首次安装需要联网下载 Linux 工具与 Python 包。

非 WSL 或自定义目录时，设置 `SDK_PATH`、`IDF_TOOLS_PATH`；构建目录通过 `BUILD_PATH` 指定，例如：

```bash
export SDK_PATH=/path/to/esp-idf-v5.5.6
export IDF_TOOLS_PATH=/path/to/idf-tools-linux
export BUILD_PATH=/path/to/build-linux
bash tools/setup.sh
bash tools/build.sh
```

`.gitattributes` 固定 shell 脚本使用 LF 换行，以兼容 Windows Git 检出。
