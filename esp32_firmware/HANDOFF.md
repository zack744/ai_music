# ai_music ESP32 固件 — Stage 2 交接文档（给下一个 Agent）

> 本文档由上一个 Agent 在 Stage 2 固件**编译成功**后生成，供接手烧录与后续开发的 Agent 使用。
> 生成时间：2026-07-17。务必从头读完再动手。

---

## 0. 一句话现状

**2026-07-17 排查结论：屏幕点不亮的根因是硬件（背光链路），软件侧已全部排除。** 证据链见 §0.1。Stage 2 固件本身（点亮流程/引脚/驱动配置）与 LilyGO 官方完全一致，编译通过、烧录即可用；**用户已决定换板（可能换触控版），等新板到货后烧录验收**。若换触控版，先读 §0.2 的迁移要点。

### 0.1 背光不亮排查证据链（2026-07-17）

1. 串口确认板上跑的就是当前源码：`[POWER] GPIO15(PWR_EN)=1 GPIO38(BL)=1`、`[INIT] display OK, size=320x170`，setup 正常跑完。
2. 纯 GPIO 诊断（当时临时加的 `-DDIAG_BL_TEST` 代码块，**已随清理移除**）：对 (15,38) 四种电平组合各保持 4s 循环，读回电平均正确翻转，但 4 种组合背光全不亮（用户目视确认）。
3. 刷 LilyGO 官方 **ScreenDetection 工具**：串口报 `DRIVER:7789 - should be 0x7789`——**屏幕通过并行总线正常应答**，证明排线数据脚、GPIO15 电源轨、全部引脚定义均正常。
4. 刷 LilyGO 官方**出厂固件（非触版）**：屏幕依旧完全不亮（用户目视确认）。
5. 结论：MCU→焊盘电平正常、面板逻辑/总线/供电正常，**唯有背光子电路（GPIO38→三极管→LED / 排线背光脚）无响应**，且官方固件同样点不亮 → 硬件故障（DOA）。
6. **清理说明**：排查用的 `diag/`（官方 bin）、`_meta.py`、`capture_pio.py`、`serial_capture.py` 均已删除。官方 ScreenDetection/出厂固件如需复用（新板到货先验硬件很推荐），从官方固件目录重新下载即可：`github.com/Xinyuan-LilyGO/T-Display-S3/tree/main/firmware`，刷法 `esptool --chip esp32s3 --baud 921600 write_flash -z --flash_mode dio --flash_freq 80m 0x0 <bin>`。
7. 另注意：用户最后观察到 **V3V 绿灯闪烁**（USB 供电下应常亮），提示 V3V 电源轨在打嗝（后级过流/短路特征），与硬件故障结论一致。旧板若重插排线救回可留作备机。

### 0.2 若换触控版（T-Display-S3 Touch）的迁移要点

- 触控版 = 同一块板 + 电容触摸（**CST816**，I2C）。屏还是同一块 ST7789V 170×320 并行屏，**显示部分代码/platformio.ini 无需任何改动**，Arduino_GFX 配置原样可用。
- 触摸引脚（占用）：I2C **SDA=18 / SCL=17**，**INT=GPIO16，RST=GPIO21**。
- **冲突**：当前 config.h 把 I2S 播放（MAX98357A）定义为 `I2S_PLAY_LRC=16`、`I2S_PLAY_DIN=21`——与触摸 INT/RST 冲突，换触控版必须改这两个引脚。候选空脚：GPIO3、GPIO43/44（USB CDC 启用时 UART0 引脚可作 GPIO）、或重新统筹 P2 排针分配（详见 docs/esp32_design.md，需同步更新）。
- 触摸驱动库：官方 `examples/touch_test` 示例 + 现成 Arduino 库（CST816_TouchLib / TDisplayS3_Touch）；I2C 地址/手势参考 LilyGO repo。
- 其他：触控版不能用外壳（shell）；PIO 板型仍是 `lilygo-t-display-s3`，无单独触控 variant。
- 建议：新板到货**先刷官方出厂固件/检测工具确认屏幕能亮**，再烧项目固件，避免再陷"分不清软硬件"的排查。

---

## 1. 项目目标（背景）

- `ai_music` 是 Flask 编排的云端音乐生成流水线（环境音理解→ASR→LLM 作词→MiniMax 生成），跑在 PC 上。
- ESP32 端做「方案 B 混合」：**只负责录音 + 上传 + 播放 + 屏幕 UI**，所有 AI 推理留在远程 Flask。ESP32 不跑任何 AI。
- 分 6 个 Stage：Stage2(屏+菜单) → Stage3(I2S 录音) → Stage4(multipart 上传) → Stage5(MP3 流式播放) → Stage6(历史列表)。
- **当前完成度：Stage 2 代码已写完并编译通过，待烧录验收。**

---

## 2. 硬件配置（已实测确认，引脚勿改）

- 板子：**LilyGO T-Display-S3（核心板/精简版）**，ESP32-S3-N16R8
  - 芯片 ESP32-S3 (QFN56)，双核 240MHz，**16MB Quad Flash，8MB OPI PSRAM**
  - 屏幕 1.9" ST7789V IPS TFT，170×320，**I8080 8 位并行接口（非 SPI）**，非触控
  - USB：原生 USB-Serial/JTAG，下载/调试/供电一根线
  - MAC：`ec:da:3b:98:c0:d0`（可用于识别设备）
- 屏幕引脚（板载占用，不可外接）：BL=38, PWR_EN=15, RST=5, CS=6, DC=7, WR=8, RD=9；D0-D7=39/40/41/42/45/46/47/48
- 板载按键：BOOT=GPIO0（上/长按返回），User=GPIO14（下）
- 外接：INMP441 麦克风 BCLK=10/WS=11/DIN=12；MAX98357A 功放 BCLK=13/LRC=16/DIN=21；外接确定键=GPIO1；状态 LED=GPIO2
- 设计依据全文：`d:\project\ai_music\docs\esp32_design.md`（必读）

---

## 3. 工程位置与结构

```
d:\project\ai_music\esp32_firmware\        ← PlatformIO 工程根
├── platformio.ini                         ← 已配好（板/Flash/PSRAM/库依赖/build_flags）
├── partitions.csv                         ← 16M: 3M APP(OTA×2) + 9.9MB FATFS
├── src/
│   ├── main.cpp          # 入口 + 状态机调度（三页菜单）
│   ├── config.h          # 全部引脚/常量（单一真理源，所有 .h 都 include 它）
│   ├── display.h/.cpp    # Arduino_GFX 初始化(ST7789V I8080 并行) + UI 渲染
│   ├── input.h/.cpp      # 3 键非阻塞状态机（上/下/确定/长按返回）
│   └── (recorder/uploader/player/history .h/.cpp 为 Stage 3-6 占位，当前为空壳/未实现)
└── .pio\build\esp32s3\firmware.bin        ← ★已生成的固件，待烧录
```

---

## 4. ★ 开发环境关键注意事项（极易踩坑，必读）

### 4.1 PIO Core 目录是非默认的 `D:\platformio`
**任何 `pio` 命令前必须先设环境变量**，否则 PIO 会去找默认的 `C:\Users\PC\.platformio` 并重新下载一切：
```powershell
$env:PLATFORMIO_CORE_DIR="D:\platformio"
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run
```
（PIO 的可执行入口在 `C:\Users\PC\.platformio\penv\Scripts\pio.exe`，但 core 数据/包/缓存在 `D:\platformio`。）

### 4.2 火绒（Huorong）杀软会拦截 PIO 的包镜像
- PIO 用 `usc1.contabostorage.com`（Contabo 对象存储）作官方 CDN 镜像。**火绒会拦截该域名的 DNS 解析**，导致下载失败/卡死。
- **这是已知共性现象**（全球用户 Malwarebytes/火绒都会拦），`contabostorage.com` 是安全的（PIO 官方 CDN + 下载后 SHA256 校验）。
- **当前状态：所有平台包 + 3 个库都已下载缓存到 `D:\platformio\packages` 和 `.pio\libdeps`，烧录/编译不需要联网。** 所以**烧录时火绒可以保持开启**。
- 仅当未来需要重新下载新包/新库时，才需要：临时关火绒，或把 `usc1.contabostorage.com`、`eu2/sin1/sg1/it1.contabostorage.com`、`dl.registry.platformio.org`、`api.registry.platformio.org` 加入火绒白名单。

### 4.3 PIO 的 Python 包被打了补丁（已生效，勿动）
为绕过「requests/urllib3 对 platformio 域名 TLS 握手失败 + 火绒拦 contabostage DNS」的双重问题，上一个 Agent 修改了 PIO 自带库（位置 `C:\Users\PC\.platformio\penv\Lib\site-packages\platformio\`）：
- `http.py`：给 `HTTPClient.send_request` 加了 **stdlib urllib 回退**（requests 失败时用 urllib 重试）；加了断点续传下载器 `_urllib_robust_download`；`_UrllibResponse` 包装类补了 `headers`/`iter_content`/`json`；加了 `_NoRedirectHandler` 支持 `allow_redirects=False`；默认 requests 超时改为 `(10, 30)`。
- `package/download.py`：`FileDownloader.__init__` 在 requests 失败/超时时回退到 urllib 断点续传下载。
- 这些补丁是**当前能下载/编译的前提**。**不要重装/升级 PIO Core**，否则补丁丢失、需重做。如果某天 PIO 升级覆盖了补丁，重新应用上述思路即可（核心：requests→urllib 回退 + 断点续传 + 超时）。

### 4.4 库依赖（platformio.ini lib_deps，已全部装好）
- `https://github.com/moononournation/Arduino_GFX.git#v1.4.9` — **用 git 源**（绕开 contabostage 镜像抽风）。**不要改成 `^1.4.0` 注册表引用**：注册表里这个库的 name 是 "GFX Library for Arduino"，`moononournation/Arduino_GFX` 名字搜索会返回 0 条（UnknownPackageError）。而且 1.6.x 需要 arduino-esp32 core 3.0+ 的 `esp32-hal-periman.h`，当前 core 是 2.0.17，所以**必须用 1.4.x**，v1.4.9 已验证可编译。
- `earlephilhower/ESP8266Audio @ ^1.9.0`（已装 1.9.9，Stage 5 MP3 流式播放用）
- `bblanchon/ArduinoJson @ ^7.0.0`（已装 7.4.3，Stage 4+ JSON 解析用）

### 4.5 平台/框架版本（已固定）
- platform `espressif32` 7.0.1，framework `framework-arduinoespressif32` 3.20017.241212（= arduino-esp32 **2.0.17**）。此平台只捆绑 core 2.0.x，无 core 3.x 选项。所以 Arduino_GFX 限 1.4.x。
- 工具链：toolchain-xtensa-esp32s3 8.4.0、toolchain-riscv32-esp 8.4.0、tool-esptoolpy 2.41100（esptool 4.11.0）。

---

## 5. 已验证的构建命令

在 `d:\project\ai_music\esp32_firmware` 目录下，PowerShell：
```powershell
$env:PLATFORMIO_CORE_DIR="D:\platformio"
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run
```
结果：**SUCCESS**，RAM 5.8%（18928/327680），Flash 9.9%（310677/3145728）。产物 `\.pio\build\esp32s3\firmware.bin`。

> 重新编译是增量、纯本地、不联网，火绒开着也没问题。首次全量编译约 8-9 分钟（ESP8266Audio 的 libopus 很耗时）。

---

## 6. ★ 下一步：烧录固件（本次交接的核心任务）

### 6.1 烧录命令
板子用 USB 接 PC 后，在工程目录下：
```powershell
$env:PLATFORMIO_CORE_DIR="D:\platformio"
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -t upload
```
PIO 会用 esptoolpy 把 `firmware.bin` 烧到 0x10000（app），同时烧 bootloader.bin(0x0) 和 partitions.bin(0x8000)。

### 6.2 确定 COM 口（关键）
- T-Display-S3 用原生 USB-Serial/JTAG，插上 USB 后 Windows 会出一个 **COM 口**（设备管理器看，或 `pio device list`）。
- platformio.ini **当前没有写 `upload_port`**。如果 PIO 自动识别不到端口，需手动指定：
  ```powershell
  & "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -t upload --upload-port COM5   # 换成实际端口
  ```
  或在 platformio.ini 的 `[env:esp32s3]` 加一行 `upload_port = COM5`。
- 可用 `pio device list`（同样要先设 `PLATFORMIO_CORE_DIR`）列出可用端口确认。

### 6.3 进下载模式
- ESP32-S3 原生 USB-Serial/JTAG 通常**能自动复位进下载模式**，直接 `pio run -t upload` 即可。
- 若烧录报「连接失败/Failed to connect」：按住 **BOOT 键(GPIO0)**，按一下 **RST/EN** 复位键，松开 BOOT，手动进下载模式再烧。
- 板载 USB 配置：`ARDUINO_USB_CDC_ON_BOOT=1`、`ARDUINO_USB_MODE=1`（Hardware CDC and JTAG），已在 build_flags 设好。

### 6.4 串口监视（看启动日志）
```powershell
$env:PLATFORMIO_CORE_DIR="D:\platformio"
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" device monitor
```
`monitor_speed = 115200`，已配 `esp32_exception_decoder` 过滤器（崩溃时自动解码栈）。
> 注意：USB CDC On Boot=1 时，串口就是 USB CDC 虚拟口（即 upload 那个 COM 口），不是 UART0 的 43/44。

### 6.5 烧录后验收标准（Stage 2 Definition of Done）
1. **屏幕点亮**，横屏 320×170，显示主菜单（两行：「生成歌曲」「历史歌曲」，当前项高亮）。
2. **BOOT 键(GPIO0) 短按 = 上**，**User 键(GPIO14) 短按 = 下**：能切换高亮项。
3. **外接键(GPIO1) 短按 = 确定**：从主菜单进入对应子页面（生成流程页 / 历史列表页）。
4. **BOOT 键长按(≥600ms) = 返回**：从子页面回主菜单。
5. 串口能打印启动/页面切换日志（`CORE_DEBUG_LEVEL=3` INFO 级）。

若屏幕不亮：优先排查 `USE_HW_I8080_BUS`（config.h 默认 1=硬件 I8080 `Arduino_ESP32LCD8`，已验证可编译）。若点亮异常（花屏/偏移），检查 `Arduino_ST7789` 的 col_offset/row_offset（display.cpp begin() 里写的 35/0），以及 `TFT_ROTATION=1`。必要时把 `USE_HW_I8080_BUS` 改 0 用软件并行 `Arduino_ESP32PAR8Q`（LilyGO 官方示例同款，更稳但慢）兜底。

---

## 7. 当前代码的功能边界（Stage 2 只做 UI 骨架）

- `display.cpp`：实现了主菜单、录音页、生成中(旋转动画)、播放页、历史列表页的**静态渲染**函数；文字为 ASCII（内置字体无中文，中文需后续接 GFX 字库）。
- `input.cpp`：3 键去抖 + 长按检测的非阻塞状态机，`poll()` 每次至多返回一个事件。
- `main.cpp`：状态机调度，在 `MainMenu / RecordSpeech / RecordEnv / Generating / Playing / History` 页面间切换，**目前不接任何业务**（录音/上传/播放/历史都还没实现，Stage 3-6 做）。
- `recorder/uploader/player/history` 的 .h/.cpp 是占位空壳。

---

## 8. 后续 Stage 路线（烧录验收通过后）

- **Stage 3**：I2S 录音（INMP441, GPIO10/11/12, 16kHz 单声道 16bit）→ 录到 PSRAM（8s≈256KB）→ 手写 44 字节 WAV 头。注意 `BOARD_HAS_PSRAM` 已开，`memory_type=qio_opi` 已配。
- **Stage 4**：WiFi 连接（SSID/密码用 Preferences 存 NVS）→ 手拼 multipart/form-data → POST `http://<PC_IP>:5000/api/generate/pipeline`（带 env_audio + speech_audio + duration_sec）→ ArduinoJson 解析 output_url。`BASE_URL` 默认 `http://192.168.1.100:5000`，需改成实际 PC IP。HTTP 联调阶段全用 HTTP 不上 HTTPS。
- **Stage 5**：ESP8266Audio 从 output_url 流式播放 mp3（边下边播不落盘），MAX98357A 输出（GPIO13/16/21）。
- **Stage 6**：GET `/api/history` 拉列表 → 历史页渲染 → 选中流式播放。
- Flask 端（`d:\project\ai_music\app.py`，监听 0.0.0.0:5000）已就绪，4 个接口：`POST /api/generate/pipeline`、`GET /api/history`、`GET /outputs/<file>`、`GET /api/pipeline/health`。

---

## 9. 常见问题速查

| 现象 | 原因/处理 |
|------|-----------|
| `pio` 命令找不到包/重新下载 | 没设 `PLATFORMIO_CORE_DIR=D:\platformio`，先设再跑 |
| 下载卡住/SSLError/contabostage 报错 | 火绒拦了 contabostage DNS；关火绒或加白名单后重试 |
| `UnknownPackageError: moononournation/Arduino_GFX` | 不能用注册表名引用，必须用 git URL（见 4.4） |
| `esp32-hal-periman.h: No such file` | Arduino_GFX 装成 1.6.x 了，需降到 1.4.x（当前 git#v1.4.9 已修） |
| `'class Arduino_GFX' has no member 'textWidth'` | Arduino_GFX 无 textWidth()，用 `getTextBounds()` 包了个 `gfxTextWidth()` 辅助函数（display.cpp） |
| `BTN_UP_PIN not declared` | input.h 没 include config.h（已修） |
| 烧录连不上板 | 手动进下载模式（按住 BOOT + 按 RST）；或指定 `--upload-port COMx` |

---

## 10. 立即行动清单（给接手 Agent）

1. 读 `d:\project\ai_music\docs\esp32_design.md` 和本文件。
2. 确认 `d:\project\ai_music\esp32_firmware\.pio\build\esp32s3\firmware.bin` 存在（已编译好，可直接烧）。
3. 用 USB 线接上 T-Display-S3，`pio device list`（记得设 `PLATFORMIO_CORE_DIR`）确认 COM 口。
4. `pio run -t upload` 烧录（必要时 `--upload-port COMx` 或手动进下载模式）。
5. 烧完看屏幕 + 按 3 键，按 6.5 验收。开 `pio device monitor` 看日志。
6. 验收通过 → 交付 Stage 2，再进 Stage 3。
7. 若需改代码重编：改完 `pio run`（增量快）→ `pio run -t upload` 重烧。
