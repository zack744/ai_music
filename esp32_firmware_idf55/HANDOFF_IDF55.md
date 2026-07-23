# ai_music ESP32 固件迁移交接文档（IDF 5.5）

> 更新时间：2026-07-22 (清理后)  
> 板卡：VIEWE UEDX24320028E-WB-A / ESP32-S3-N16R8  
> 串口：COM8  
> WiFi：`asus`，设备 IP `192.168.50.65`  
> Flask：`http://192.168.50.246:5000`

---

## 0. 一句话状态

完整固件已从旧版 Arduino 3.1.1 / ESP-IDF 5.3.2 成功迁移到 **pioarduino 55.03.39 + Arduino-ESP32 3.3.9 + ESP-IDF 5.5.4**。新版完整固件已验证：WiFi、屏幕、触摸、LVGL 六页 UI、HTTP 下载到 PSRAM、MP3 解码、I2S1 播放均可工作，且不再发生旧版的 OPI PSRAM Cache panic。

正式版固件（`PLAYER_BOOT_SELF_TEST=0`）已烧录并验证通过。
**播放策略（2026-07-23 起）：只做整首下载播放。** 流式播放代码仍在仓库中，但已搁置，不再作为当前目标。

---

## 1. 工程目录

| 路径 | 用途 | 当前状态 |
|---|---|---|
| `esp32_firmware_idf55/` | **新版完整固件主工程** | 唯一固件工程 |
| `esp32_diagnostics/` | 曾用于 IDF 5.5 最小诊断 | **2026-07-23 已删除**（结论已验证） |
| `D:\platformio_diag` | PlatformIO 包目录 | 新工程使用（唯一） |

旧版 `esp32_firmware/` 目录和旧 `D:\platformio` 环境已删除。旧安全固件二进制已归档至 `releases/legacy-idf53-safe/`。

---

## 2. 本次定位和解决的根问题

### 2.1 最初错误：错误选择 Quad PSRAM

旧 HybridCompile 生成的配置使用：

```text
CONFIG_SPIRAM_MODE_QUAD=y
```

而板载是 8MB Octal/OPI PSRAM，导致：

```text
quad_psram: PSRAM ID read error: 0x00ffffff
```

正确板卡配置：

```json
"memory_type": "qio_opi"
```

新版预编译库确认：

```text
CONFIG_SPIRAM_MODE_OCT=1
CONFIG_SPIRAM_BOOT_INIT=1
```

### 2.2 旧版播放器真正崩溃点

旧 Arduino 3.1.1 / IDF 5.3.2 下，在 HTTP 下载并写 OPI PSRAM 时出现：

```text
Cache disabled but cached memory region accessed
Write back error occurred while dcache tries to write back to flash
```

回溯涉及：

```text
wDev_ProcessRxSucData
ppTask
tcp_input
NetworkClient::readBytes
player_play
```

即问题发生于：

```text
WiFi/LWIP + OPI PSRAM 并发写入/Cache 写回
```

不是 CPU 性能不足，也不是 I2S、功放或 MP3 解码算力问题。

### 2.3 旧版 HybridCompile 还存在假成功

旧 pioarduino 53.03.11 中，第二阶段 Arduino 构建通过：

```python
shutil.which("platformio.exe")
```

定位子进程。在本机返回 `None`，实际运行：

```text
"None" run -e ...
```

随后第二阶段失败，但外层仍显示 SUCCESS，生成的约 309KB 固件只是 dummy 固件，不是完整应用。

新版迁移已完全绕过该路径，不再使用旧 HybridCompile。

---

## 3. 最小诊断结论：板卡硬件正常

诊断环境：

| 项目 | 版本/配置 |
|---|---|
| pioarduino | 55.03.39 |
| Arduino-ESP32 | 3.3.9 |
| ESP-IDF | 5.5.4 |
| Flash | 16MB QIO 80MHz |
| PSRAM | 8MB Octal/OPI 80MHz |
| DCache | 32KB，32-byte line |
| WiFi/LWIP 优先 PSRAM | 否 |

诊断结果：

| 阶段 | 内容 | 结果 |
|---|---|---|
| A | 2MB PSRAM，8轮全量写入/读回 | PASS，0 error |
| B0 | 连接 WiFi `asus` | PASS |
| B1 | WiFi 在线时，2MB PSRAM 12轮写入/读回 | PASS，0 error |
| C1 | HTTP 下载 602419 bytes 到内部 SRAM sink | PASS |
| C2 | HTTP 4KB SRAM 中转并复制到 PSRAM | PASS，checksum `fc99e7f4` |
| 存活 | 完成后在线约110秒 | PASS，无 panic/重启 |

原始日志：

```text
logs/serial_diag_55_03_39_20260722.txt
esp32_diagnostics/DIAGNOSTIC_RESULTS.md（工程已删，结论见上文 §2）
```

结论：**板子性能和 PSRAM 硬件没有问题；旧崩溃属于旧软件栈。**

---

## 4. 新版完整工程配置

主工程：

```text
esp32_firmware_idf55/
```

关键版本：

```ini
platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.39/platform-espressif32.zip
framework = arduino
```

实际包：

```text
Arduino-ESP32 3.3.9
ESP-IDF 5.5.4
ESP8266Audio 1.9.9
LVGL 8.4.0
WiFiManager 2.0.17
ArduinoJson 7.4.3
```

构建命令：

```powershell
$env:PLATFORMIO_CORE_DIR='D:\platformio_diag'
$env:PATH='C:\Users\PC\.platformio\penv\Scripts;' + $env:PATH
& 'C:\Users\PC\.platformio\penv\Scripts\pio.exe' run -e BOARD_VIEWE_UEDX24320028E_WB_A
```

烧录命令：

```powershell
$env:PLATFORMIO_CORE_DIR='D:\platformio_diag'
$env:PATH='C:\Users\PC\.platformio\penv\Scripts;' + $env:PATH
& 'C:\Users\PC\.platformio\penv\Scripts\pio.exe' run `
  -e BOARD_VIEWE_UEDX24320028E_WB_A `
  -t upload --upload-port COM8
```

---

## 5. 迁移时修复的 UI 字库问题

### 问题一：写只读 Flash 字体描述符

生成字库在 LVGL 8 下声明为：

```cpp
const lv_font_t lv_font_zh_16;
const lv_font_t lv_font_zh_22;
```

旧代码运行时直接执行：

```cpp
lv_font_zh_16.fallback = &lv_font_montserrat_14;
lv_font_zh_22.fallback = &lv_font_montserrat_14;
```

IDF 5.5 正确报错：

```text
Dbus write to cache rejected
```

### 修复

在 `src/ui.cpp` 中创建 RAM 字体描述符副本：

```cpp
static lv_font_t s_font_zh_16_runtime;
static lv_font_t s_font_zh_22_runtime;
```

通过 `ensure_fonts_ready()` 复制只读字体描述符并配置 fallback。

### 问题二：启动提示先于 UI 使用字体

`ui_show_boot_msg()` 在 `ui_create()` 前使用 `f22`，初版修复中 RAM 字体尚未初始化，导致空函数指针：

```text
InstrFetchProhibited
PC=0x00000000
lv_font_get_glyph_width
```

最终修复：`ui_show_boot_msg()` 和 `ui_create()` 都先调用：

```cpp
ensure_fonts_ready();
```

新版完整启动已验证通过。

---

## 6. 新版完整固件验证结果

启动日志：

```text
[boot] init WiFi (before LVGL)
[net] WiFi connected, IP=192.168.50.65
[boot] init board
Board begin success
[boot] init LVGL
[boot] create UI
[boot] init player
[play] MAX98357A I2S1 init OK
[boot] boot done
```

对应日志：

```text
logs/serial_migration_fontlazy_20260722.txt
```

播放器自检日志：

```text
[play] downloading http://192.168.50.246:5000/outputs/compare_funmusic_vocals.mp3
[play] allocating 602419 bytes in PSRAM
[play] downloaded 602419 bytes
[play] mp3 bitrate 128 kbps -> duration ~37651 ms
[play] playback started
[SelfTest] player_play result=OK
[boot] boot done
[play] finished
```

全程没有 Cache panic、看门狗或重启。

对应日志：

```text
logs/serial_migration_player_selftest_20260722.txt
```

---

## 7. 当前播放器实现和内存生命周期

文件：

```text
esp32_firmware_idf55/src/player.cpp
```

当前架构：

```text
HTTPClient / WiFiClient
        ↓
4KB 内部 DMA-capable SRAM 中转缓冲
        ↓ memcpy
整首 MP3 PSRAM 缓冲
        ↓
AudioFileSourceMEM
        ↓
AudioGeneratorMP3
        ↓
AudioOutputI2S(I2S1)
```

播放引脚：

| 信号 | GPIO |
|---|---:|
| MAX98357A BCLK | 14 |
| MAX98357A LRC/WS | 5 |
| MAX98357A DIN | 6 |

录音引脚：

| 信号 | GPIO |
|---|---:|
| INMP441 BCLK | 10 |
| INMP441 WS | 11 |
| INMP441 DOUT→ESP DIN | 12 |

当前内存行为：

- 新播放开始时 `player_play()` 首先调用 `player_stop()`；
- 上一首的 `AudioFileSourceMEM` 与 `s_mp3_buf` 会释放；
- 同时最多保存一首 MP3；
- 数据只在 PSRAM，不写 Flash/SD；
- 自然播放结束时目前只设 `PLAYER_FINISHED`，缓冲在离开页面、停止或播放下一首时释放；
- 可选优化：自然结束后立即释放 PSRAM，但重播需要重新下载。

当前代码还保留了 4KB 内部 SRAM 网络中转缓冲。新版软件栈已经稳定，但建议继续保留，避免让网络读取路径直接写外部 PSRAM。

---

## 8. 流式播放（已搁置）

**结论（2026-07-23）：流式播放搁置，产品路径统一为整首下载播放。**

原因：
1. 当前成片目标约 60–90s，体积可控，8MB PSRAM 足够整首缓存；
2. 下载模式已验证稳定（WiFi + PSRAM + MP3 + I2S）；
3. 流式路径存在跨核竞态：`player_stop()`（UI/Core0）与 `player_loop()`（Arduino loop/Core1）无互斥，触摸切页会 `LoadProhibited`。

代码状态：
- `player_play_stream()` 仍在 `player.cpp`，编译保留；
- `PLAYER_DEFAULT_STREAM=0`，UI **只走** `player_play()` 下载模式；
- 后续若重开流式，需先加 FreeRTOS mutex，再考虑切默认。

---

## 9. 当前板上固件状态

正式版固件（`PLAYER_BOOT_SELF_TEST=0`）已烧录并验证通过。串口日志见 `logs/verification/serial_release_final_20260722.txt`。构建产物归档在 `releases/2026-07-22-idf55/`，旧版安全固件归档在 `releases/legacy-idf53-safe/`。

重新构建和烧录：

```powershell
cd D:\project\ai_music\esp32_firmware_idf55
$env:PLATFORMIO_CORE_DIR='D:\platformio_diag'
$env:PATH='C:\Users\PC\.platformio\penv\Scripts;' + $env:PATH
& 'C:\Users\PC\.platformio\penv\Scripts\pio.exe' run -e BOARD_VIEWE_UEDX24320028E_WB_A -t upload --upload-port COM8
```

---

## 10. 后续任务优先级

### P0：已完成

- ~~烧录正式版固件~~ 已验证 `boot done`，无自检
- ~~六页 UI 和触摸~~ 已验证

### P1：功放 + 麦克风实机（当前主线）

1. 按 `docs/wiring_guide.md` 接 MAX98357A + 喇叭，听真实输出；
2. 接 INMP441，验证 I2S0 录音 / WAV / 上传；
3. 完整「录音→上传→生成→下载播放」。

### P2：代码质量

1. 自然播放结束时可选立即释放 PSRAM；
2. 为整首下载设置最大文件尺寸，例如 6MB；
3. ~~修复 `estimate_duration_ms()` 在 `size < 4` 时的无符号下溢~~ 已修复；
4. 检查 HTTP 下载超时后是否为完整文件；
5. 更新 ESP32_Display_Panel 配置头，消除 outdated warning。

### P3：流式播放（搁置）

见第 8 节。不作为近期任务。

---

## 11. 不要重复踩的坑

1. 不要把项目文件命名为 `network.h`，Windows 下会覆盖 Arduino 框架的 `Network.h`；保持 `net_helper.h/.cpp`。
2. 不要再使用旧版 HybridCompile 生成的 309KB dummy 固件。
3. 不要直接修改 `const lv_font_t` 的 `fallback` 字段。
4. 不要在 `ensure_fonts_ready()` 前使用 RAM 字体副本。
5. 不要把新版包安装到 `D:\platformio`；新工程固定使用 `D:\platformio_diag`。
6. 不要把流式播放重新设为默认，除非已修好 Core0/Core1 互斥并完成实机验证。
7. 不要同时混用 IDF 新 I2S API 和 ESP8266Audio 的 legacy I2S API；旧冲突曾导致运行时 abort。
8. 烧录失败并提示 Windows error 433 时，先重新枚举 COM8，通常是 USB 设备瞬时重连，不是固件编译问题。

---

## 12. 关键文件

| 文件 | 说明 |
|---|---|
| `platformio.ini` | 新版 55.03.39 配置，使用 `D:\platformio_diag` |
| `boards/BOARD_VIEWE_UEDX24320028E_WB_A.json` | qio_opi / 16MB Flash / USB CDC |
| `src/main.cpp` | WiFi先于LVGL初始化；可选开机播放器自检 |
| `src/ui.cpp` | 六页 UI；RAM字体副本修复 |
| `src/player.cpp` | 下载播放器（默认）；流式代码保留但搁置 |
| `src/player.h` | 播放器状态枚举和接口声明 |
| `src/recorder.cpp` | legacy I2S0录音 |
| `src/uploader.cpp` | multipart上传到Flask |
| `src/net_helper.cpp` | WiFiManager、后端IP、历史列表 |
| `src/config.h` | I2S引脚、自检开关（`PLAYER_DEFAULT_STREAM=0`） |
| `releases/2026-07-22-idf55/` | 正式版固件二进制归档 |
| `releases/legacy-idf53-safe/` | 旧版安全固件归档 |
| ~~`../esp32_diagnostics/`~~ | 诊断工程已删除（2026-07-23） |

---

## 13. 已验证日志

| 日志 | 内容 |
|---|---|
| `logs/verification/serial_release_final_20260722.txt` | 正式版启动验证：boot done，无自检 |

---

## 14. 当前已证实能力

```text
ESP32-S3-N16R8
  ├─ Octal PSRAM 8MB：稳定
  ├─ WiFi + PSRAM 并发：稳定
  ├─ HTTP下载：稳定
  ├─ LCD + Touch + LVGL：稳定启动
  ├─ 六页UI：成功创建
  ├─ MP3完整下载到PSRAM：成功
  ├─ MP3解码：成功
  ├─ I2S1播放：成功运行到finished
  └─ 录音/上传全链路：代码存在，待接麦克风实测
```
