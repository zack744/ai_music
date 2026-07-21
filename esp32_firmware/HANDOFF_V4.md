# ai_music ESP32 固件 - 交接文档 V4(给下一个 Agent)

> 生成时间:2026-07-20。Stage 2 UI 按 Figma 重写后,卡在按钮点击无响应,需排查。
> 前序:`HANDOFF_V3.md`(Stage0+2 初版)、`HANDOFF_V2.md`(新板重建)、`docs/esp32_design.md`(原始设计)。

---

## 0. 一句话现状

Stage 2 UI 已按 Figma 设计稿重写(深色极光绿主题,五页,30秒倒计时,双字号字库)。**但按钮点击无响应**:抓串口 40 秒,用户点按钮,无任何 `ESP_LOGI` 日志输出(只有启动日志)。根因未明,需下一个 Agent 排查触摸/事件/日志链路。

---

## 1. 当前固件状态(已烧录运行)

### 1.1 UI 设计(按 Figma,深色极光绿)
- 配色:背景 `#121414` / 主色极光绿 `#00E38B` / 亮绿 `#00FF9D` / 次级灰 `#333535` / 卡片 `#282A2B`
- 字体:simhei 生成 16px(`lv_font_zh_16`)+ 22px(`lv_font_zh_22`),fallback 到 `lv_font_montserrat_14`(LVGL 内置,含 FontAwesome 符号)
- 五页:主菜单 / 录音 / 生成中 / 播放 / 历史

### 1.2 功能实现
- **主菜单**:绝对定位,标题"AI音乐" + "生成歌曲"(绿发光)/"历史歌曲"(灰描边) + 底部导航(Home/Mic/History)
- **录音页**:绝对定位,标题(录环境音/录心里话)+ 胶囊(1/2/2/2)+ 倒计时圆(30s)+ 开始/取消/下一步。**步骤顺序:step1=录环境音, step2=录心里话**
- **倒计时**:点"开始"-> 标题变"录音中..." + 中心 30->0 倒计时(lv_timer 1秒/次,30次自动停)-> "已录制"
- **生成中**:spinner + "创作中..." + 按钮隐藏 -> 3秒后 lv_timer -> "已生成" + 显示"完成"按钮 -> 点完成进播放
- **播放页**:flex 布局,唱片圆 + 歌名 + 进度条 + 暂停/返回
- **历史页**:flex 布局,列表(5首英文歌名)+ 返回

### 1.3 日志
所有 `goto_*` 和按钮回调加了 `ESP_LOGI(TAG, "...")`(TAG="ui"),走 USB-Serial/JTAG(COM8 可见)。

---

## 2. 关键问题(卡点,待排查)

### 问题1:按钮点击无 ESP_LOGI 日志
- 现象:抓串口 40 秒,用户点按钮(生成歌曲/开始/下一步等),**无任何 `ui:` 日志**输出,只有板子启动日志
- 矛盾:用户上一轮反馈"首页按钮可以正常使用",这一轮重写后说"打不开"——反馈不稳定
- 可能原因(未验证):
  1. **打开串口触发板子重启**:PowerShell `SerialPort.Open()` 触发 ESP32 USB-Serial/JTAG 的 DTR reset(每次抓日志开头都是 `rst:0x15 USB_UART_CHIP_RESET`),用户点按钮的日志可能被重启冲掉/时序错位
  2. **按钮 CLICKED 真没触发**:触摸坐标没落在按钮上,或按钮被遮挡,或事件绑定失效
  3. **ESP_LOGI 没输出**:虽然 esp_log 走 USB,但可能日志级别/缓冲问题

### 问题2:用户反馈"变回之前那样"
- 用户说"固件烧错了,变回之前那样"——但日志显示新固件启动正常(Board init success,GC9A01/CHSC6540 识别,无 panic)
- 可能:用户看到的是按钮点不动(和之前 O0 panic 前的现象类似),误以为"变回之前"

---

## 3. 已踩坑与修复(供参考)

| 坑 | 现象 | 修复 |
|---|---|---|
| GCC internal compiler error | `new_btn` 函数 reload1.cc ICE(Xtensa GCC 13.2 -Os) | `__attribute__((noinline, optimize("O2")))` |
| Cache panic 循环重启 | `optimize("O0")` 代码膨胀 + LVGL 栈 6KB 溢出 -> `Guru Meditation Error: Cache disabled` | 改 O2 + 栈扩到 16KB(`lvgl_v8_port.h` LVGL_PORT_TASK_STACK_SIZE) |
| 主菜单白框 | `new_box` 没清 LVGL 默认 border | `lv_obj_set_style_border_width(box,0,0)` + `pad_all=0` |
| 主菜单按钮点不动 | flex 布局 + 主题 pad 导致按钮位置/触摸异常 | 主菜单+录音页改**绝对定位**(`lv_obj_set_pos`) |
| 字库缺字 | simsun_16_cjk 是繁体+日文,简体缺字 | 用 lv_font_conv 从 simhei.ttf 生成简体子集 |
| PowerShell 传中文给 node 乱码 | GBK 编码问题 | Python subprocess 调 node(宽字符 API) |
| C 字符串内中文引号 | `"按"开始"录音"` 编译错 | 改文案避免引号 |

---

## 4. 排查建议(下一个 Agent 优先做)

1. **换抓日志方式**:用 `pio device monitor`(pio 串口监视)替代 PowerShell SerialPort,看是否还触发 reset。命令:`& $pio device monitor --port COM8`。让用户点按钮,看是否出现 `ui: -> record` 等日志
2. **加触摸坐标日志**:在 LVGL touch indev 回调或事件里打印触摸坐标,确认点击位置是否落在按钮区域。或临时在 `new_btn` 里给每个按钮加 `LV_EVENT_PRESSED` 回调打印"btn pressed"
3. **验证 ESP_LOGI 输出**:在 `ui_create` 开头加 `ESP_LOGI(TAG, "ui_create called")`,确认启动时能看到这条(验证 ESP_LOGI 链路通)
4. **检查按钮可点击性**:确认 `new_btn` 创建的 `lv_btn` 默认 `LV_OBJ_FLAG_CLICKABLE`,没被父容器遮挡。绝对定位的按钮 parent 是 screen(screen pad=0),应可点
5. **排查 flex 页面**:播放页/历史页/生成中页仍是 flex 布局,如果主菜单(绝对定位)能点但 flex 页不能,说明 flex 容器拦截触摸——把所有页都改绝对定位

---

## 5. 文件状态

| 文件 | 状态 | 说明 |
|---|---|---|
| `src/ui.cpp` | 最新(455行) | 五页 UI,绝对定位(主菜单+录音)+ flex(其他),倒计时,ESP_LOGI 日志 |
| `src/ui.h` | 最新 | 声明 `lv_font_zh_16/22` + `ui_create()` |
| `src/lv_font_zh_16.c` | 141KB | 16px simhei 简体字库(146字+ASCII) |
| `src/lv_font_zh_22.c` | 229KB | 22px simhei 简体字库(标题/按钮) |
| `src/main.cpp` | 稳定 | board init + lvgl_port_init + ui_create() |
| `src/lvgl_v8_port.h` | 已改 | 栈 6KB->16KB |
| `lib/lv_conf.h` | 稳定 | LVGL 配置,montserrat_14 启用(fallback) |
| `lib/lvgl-release-v8.4/` | **未纳入 git**(160MB 上游源码) | 从 https://github.com/lvgl/lvgl/archive/refs/tags/v8.4.0.zip 下载解压到 `lib/lvgl-release-v8.4/` 后方可编译 |
| `tools/gen_font.py` | 稳定 | 字库生成脚本(16+22px),`python tools/gen_font.py` |
| `platformio.ini` | 稳定 | BOARD_VIEWE_UEDX24320028E_WB_A |

### Figma 设计稿(已用 MCP 读取)
- 文件:`https://www.figma.com/design/YZiwA8S2LKbxNM4ndBP6hM/AI-music`
- file_key:`YZiwA8S2LKbxNM4ndBP6hM`
- 5 个 frame:主菜单/录音页/生成中/播放页/历史页(都标注"2.8寸屏优化")
- Figma MCP 已配在 `C:\Users\PC\.config\opencode\opencode.json`(figma-developer-mcp,token 已从交接文档中移除以防泄露,实际值见本地 opencode 配置)
- 用 `figma_get_figma_data(fileKey=YZiwA8S2LKbxNM4ndBP6hM)` 重新读取设计稿

---

## 6. 板子配置(实测,详见 HANDOFF_V3 第2节)
- 屏幕:GC9307(用 GC9A01 驱动),240x320 竖屏,SPI
- 触摸:CHSC6540,I2C SDA=1/SCL=3
- USB:ESP32-S3 原生 USB-Serial/JTAG(VID:PID=303A:1001),COM8
- 烧录:`pio run -t upload --upload-port COM8`
- 注意:`Serial.print` 走 UART0(COM8 看不到),`esp_log`/`ESP_LOGI` 走 USB(COM8 可见)

---

## 7. 关键命令速查

```powershell
$env:PLATFORMIO_CORE_DIR='D:\platformio'
$pio='C:\Users\PC\.platformio\penv\Scripts\pio.exe'
# 工作目录:D:\project\ai_music\esp32_firmware

# 编译
& $pio run
# 烧录
& $pio run -t upload --upload-port COM8
# 串口监视(推荐,替代 PowerShell SerialPort,避免 DTR reset)
& $pio device monitor --port COM8
# 字库重新生成
python tools/gen_font.py
```

---

## 8. 下一步行动

1. **先排查按钮点击无日志**(第4节排查建议),确认是触摸/事件/日志哪个环节断
2. 修复点击后,验收五页流程:主菜单->录音(环境音30s->心里话30s)->生成中(3s->完成)->播放->返回;主菜单->历史->选歌->播放
3. 验收通过后进 Stage 3:I2S 录音(INMP441 BCLK/WS/DIN=10/11/12,16kHz/单声道/16bit/8s,接 `rec_start_cb` 替换模拟倒计时)
4. Stage 4:multipart 上传 `/api/generate/pipeline`
5. Stage 5:ESP8266Audio 流式播放(MAX98357A 14/21/5)
6. Stage 6:历史页接 `/api/history`

---

## 9. 相关文件
- `esp32_firmware/HANDOFF_V3.md` - 之前交接(Stage0+2 初版,板子配置详情)
- `esp32_firmware/HANDOFF_V2.md` - 新板重建
- `esp32_firmware/src/ui.cpp` - 当前 UI(排查重点)
- `esp32_firmware/tools/gen_font.py` - 字库生成
- `docs/esp32_design.md` - 原始设计
- `app.py` - Flask 端(4 接口已就绪)
