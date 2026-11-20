# ESP32 蓝牙音箱 SDK 工程

整个目录是个人仓库。官方 ESP-IDF v5.5.6 源码及递归依赖已作为普通文件纳入 sdk/esp-idf，
无需再克隆另一个 SDK 仓库。目标固定为经典 ESP32-WROOM-32E/32D，使用 Classic Bluetooth/A2DP Sink。

## 开发入口

日常产品开发集中到：
`sdk/apps/esp_app/board/ESP32/board_esp32_speaker/`

```text
sdk/
  esp-idf/                           官方 SDK 源码及完整依赖
  apps/esp_app/
    CMakeLists.txt                   应用构建入口
    device/                          I2S、ST7789、原始按键驱动
    interface/                       蓝牙接收、PCM 队列、音频工作任务
    ui/                              LVGL 页面、状态/操作接口及薄适配层
    board/ESP32/board_esp32_speaker/
      CMakeLists.txt                 本产品的源码清单与依赖
      board_esp_proj.c/.h            GPIO/I2S/LCD 初始化编排
      board_esp_proj_cfg.h           板级接线与产品参数
      board_app.h                   产品应用入口声明
      sdkconfig.defaults            首次构建默认配置
      partitions.csv                本产品分区表
      task/board_main.c             app_main，启动顺序
      system/board_app.c            app_start，产品状态与任务接入位置
      keypad/board_keyevent.c       按键扫描、消抖与短按/长按/连发事件
  tools/                            Linux / Windows 安装、构建、校验
  .vscode/                          打开 sdk 目录使用的任务与补全
esp-idf.snapshot.json                来源、依赖 commit、逐文件 SHA256
build/linux/                        Linux 构建输出（不入库）
build/windows/                      Windows 构建输出（不入库）
```

参照杰理项目组织产品目录，继续使用 ESP-IDF 官方 CMake/Kconfig 构建机制。
当前只有一个 board，作为一个原生 IDF 组件编译，公共驱动由该组件列入源码；没有额外的板型选择框架。
新增本产品源文件时，在 board 的 CMakeLists.txt 中加入。新增硬件能力时可扩展 device/interface。

## 应用代码书写规范

sdk/apps/esp_app/.clang-format 固定应用目录的格式：4 空格缩进、Allman 花括号、控制分支必须带花括号、每行一条语句，长初始化列表展开。官方 SDK 不在此配置范围内。
使用 clang-format 18 检查所有应用 C/头文件：

```bash
rg --files sdk/apps/esp_app -g '*.c' -g '*.h' -0 | xargs -0 clang-format --dry-run --Werror
```

## 配置入口

- board_esp_proj_cfg.h：统一的产品配置入口，包括 GPIO_NUM_x 接线、按键有效电平/上下拉、LCD 开关/尺寸/偏移/反色/总线/方向、蓝牙名称、初始音量和固件版本号 BOARD_FIRMWARE_VERSION。构建与设备信息页共用此版本，修改后正常编译即可。产品配置不再使用 Kconfig.projbuild。
- sdkconfig.defaults：首次配置的项目默认值；已有 sdkconfig 时，不会覆盖其中已保存的选项。
- sdk/apps/esp_app/sdkconfig：ESP-IDF 系统配置的当前选择（蓝牙协议栈、Flash、系统任务等）；通过 bash sdk/tools/build.sh menuconfig 修改，Windows 使用 sdk/tools/build.ps1 -Action menuconfig。
- build/linux/config/sdkconfig.h：自动生成供 C 代码使用的 CONFIG_* 宏，不手工修改；Windows 使用对应 build/windows 目录。

启动时先检查启用外设的 GPIO 有效性、输出/上下拉能力、Flash 占用和重复分配，再初始化驱动。
启动配置脚和默认 UART 引脚会告警，仍需按原理图核对模组引出、上电电平及 JTAG 等复用。
背光分配 GPIO16，BLK 高有效，20kHz PWM，启动亮度 60%；功放控制脚仍由硬件接线决定。五键映射、300ms 双击间隔、调节步长及 UI 刷新参数也在 cfg.h 配置。

## 构建

Linux / WSL（当前工程位于 /home/ax2869/esp32-speaker）：

```bash
bash sdk/tools/setup.sh      # 首次安装或 SDK 路径改变后执行
bash sdk/tools/build.sh
bash sdk/tools/build.sh menuconfig
bash sdk/tools/build.sh size
```

Windows PowerShell（本地 Windows 检出目录）：

```powershell
.\sdk\tools\setup.ps1
.\sdk\tools\build.ps1
.\sdk\tools\build.ps1 -Action menuconfig
```

源码共用，工具和缓存分开：Linux 工具默认 ~/.espressif；Windows 默认 %USERPROFILE%\.espressif。
两端均可通过 IDF_TOOLS_PATH 指定工具位置，Windows 也支持 -ToolsPath。
WSL 的 SDK、工具与构建输出全部在 Linux 文件系统。详见 docs/BUILD.txt。
生成配置保存在 sdk/apps/esp_app/sdkconfig，Linux/Windows 共用同一份产品配置，请依次运行 menuconfig/构建。

## SDK 版本与维护

上游：https://github.com/espressif/esp-idf.git
版本：v5.5.6；commit：880a203ae603d08a94d06aa01e0d6761958e5e9e。
sdk.lock.json 记录上游版本；esp-idf.snapshot.json 记录导入的 27 个递归子模块及全部文件哈希。
安装入口使用sdk/tools/check_sdk.py 校验全部源码，构建入口校验锁定版本与来源信息，不再把个人仓库 HEAD 当成官方 SDK commit。

导入保留上游源码和许可证；去掉嵌套 .git、.gitignore、.gitattributes、.gitmodules。
16 个上游文件符号链接已展开为目标文件内容，便于 Windows 普通 Git 检出。
新增 version.txt 固定官方版本标识，避免 IDF 把个人仓库版本写成 SDK 版本。
顶层 .gitattributes 保持 SDK 文件原始字节，不做换行转换。
升级时应重新核对上游版本、递归依赖与导入清单，并重新验证音频 API；不要仅修改哈希来绕过检查。

## 功能与边界

数据路径保持：手机 SBC → 官方 Bluedroid 内部解码 → 借用 PCM 回调 → 复制到有界队列 → 音频任务 → I2S。
回调不阻塞等待 I2S；队列满时丢弃剩余数据。采样率随协商切换，连接代号用于丢弃旧队列数据。
交错 PCM16 立体声在音频任务内统一缩放音量，两块 MAX98357A 通过硬件 SD_MODE 选择左右声道。
当前不支持单声道协商；LCD 为 1.69 英寸、240×280，默认启用并启动 LVGL 局部刷新；窗口偏移、方向和背光仍需实物确认。

保留内部解码 PCM 的 legacy API esp_a2d_sink_register_data_callback；升级 SDK 需要重新评估。
不改用 ESP32-S3/C3，不改蓝牙音频协议、引脚、队列大小或硬件参数。
框架维护连接、播放器、音频流与音量状态；speaker_service.h 提供查询、状态变化接收、
播放/暂停/上一首/下一首及音量接口。AVRCP CT 控制手机，TG 接收/同步绝对音量，实习生不实现协议。
UI 已实现深海音乐主页、设置、亮度和设备信息，采用 LVGL 9.3.0 单任务与局部刷新。
按键扫描、消抖和事件分发已由框架实现；system/board_app.c 提供音量加减的完整业务示例及真实状态回传。实习生参考 KRS/KRH 与 UI 动作处理流程接入其他功能。
服务已自动获取歌名、歌手、时长及播放进度，并通过现有状态快照更新主页；歌词需独立来源。背光接口 lcd_hal_set_brightness/get_brightness 已实现，接口用法见 docs/HANDOFF.txt。
应用使用 master_volume.h：master_vol_up/down、set_master_vol、get_master_vol。cfg 配置最大 50 档、默认 16 档、步长 1；换算及百分比由框架提供，不写 NVS。
手机需支持并订阅 AVRCP 绝对音量通知，双向同步才生效；默认初始值不构成硬件最大音量限制。
编译通过不代表配对、实际播放、显示、按键及长时间运行通过实板验收。
详见 docs/HANDOFF.txt、docs/HARDWARE.txt 和 docs/VALIDATION.txt。

## VS Code 一键编译与烧录

VS Code 打开 sdk 目录；命令行示例从仓库根目录执行。首次先执行本平台的 sdk/tools/setup.sh 或 sdk/tools/setup.ps1 安装工具链。
C/C++ 扩展（ms-vscode.cpptools）安装在当前开发环境；WSL 使用 VS Code 的 WSL 扩展打开工程。
Ctrl+Shift+B 默认执行 build + flash：先编译，成功后检查串口；未连接时提示跳过烧录并正常返回。
串口存在时通过 esptool 烧录，并按默认配置复位运行；编译错误或实际烧录失败仍返回失败。
任务在 Windows 调用 PowerShell，在 Linux/WSL 调用 Bash，不需要额外烧录上位机。
需要仅编译时，通过“任务：运行任务”选择 build only；命令行 build.sh / build.ps1 的默认动作仍为仅编译。

默认串口由官方 ESP-IDF/esptool 自动探测。连接多个设备时，在启动 VS Code 的环境中设置 ESPPORT，
例如 Windows 为 COM5、Linux 为 /dev/ttyUSB0；重启 VS Code 使任务继承更新后的环境。
串口必须可访问：Windows 需对应 USB 串口驱动；Linux 需串口权限；WSL2 需先将 USB 串口设备接入 WSL。
板卡需要自动下载/复位电路才能全程一键执行；没有该电路时仍需手动配合 BOOT/EN。
任务不执行整片擦除，不启动串口监视器。实板烧录需实际连接设备后验证。

.vscode/c_cpp_properties.json 按 Linux/Win32 自动选择对应平台的 compile_commands.json，
沿用真实编译命令的宏、头文件路径和编译器。首次先编译一次以生成编译数据库和 sdkconfig.h。
.vscode/settings.json 不参与固件构建：默认搜索跳过 build 和官方 SDK，文件监听跳过 build。
SDK 仍在文件树中可见；需要搜索 SDK 时可取消搜索面板的“使用排除设置和忽略文件”。
