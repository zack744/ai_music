# ai_music · 项目状态文档

> **最近更新：2026-07-23**  
> 目标：环境音 + 语音/文字 → 云端流水线生成音乐；ESP32 录音上传 + 整首下载播放。  
> 当前阶段：**局域网开发测试**；公网演示方案已讨论，固件暂不改，后续再动。

---

## 0. 一句话状态

- **Flask 后端 + 网页**：可用；支持历史/上传删除、生成日志监控、音乐后端热切换、Mock 热切换。
- **ESP32 正式固件**：功放播放已通；返回停播崩溃已修；麦克风+全流程可测。
- **通信**：ESP32 ↔ Flask 为 **HTTP 明文**，固件写死 **`{server_ip}:5000`**。
- **公网演示**：思路可行（热点上网 + 公网宿主）；**不要用 Vercel 跑本 Flask**；域名/HTTPS 需后续改固件。

---

## 1. ESP32 工程

| 目录 | 角色 |
|------|------|
| **`esp32_firmware_idf55/`** | **唯一正式固件**（UI / 录音 / 上传 / 历史 / 下载播放） |

- 交接细节：`esp32_firmware_idf55/HANDOFF_IDF55.md`
- 原 `esp32_diagnostics/`（PSRAM/WiFi 最小诊断）已于 2026-07-23 删除；结论曾写入 HANDOFF，无需再保留工程
- 旧版 IDF 5.3 工程已删除；安全二进制在 `releases/legacy-idf53-safe/`（若仍存在）

**PlatformIO 包目录：** `platformio.ini` 里 `core_dir = D:\platformio_diag`。

---

## 2. 架构（当前）

```
浏览器 / ESP32
    │  HTTP
    ▼
Flask (app.py)  ──编排──► CloudPipeline
    │                        │
    │              ┌─────────┴──────────┐
    │              ▼                    ▼
    │         DashScope 三步      音乐后端（可热切换）
    │         理解 / ASR / LLM    funmusic（默认）/ minimax
    │
    ├── static/uploads/        浏览器录音
    ├── static/uploads/esp32/  板子上传
    └── static/outputs/        成片（命名：模型简称-日期时间.mp3）
```

**ESP32 通信（本版固件）：**

| 动作 | 方法 | 路径 |
|------|------|------|
| 上传生成 | POST multipart | `/api/generate/pipeline`（`source=esp32`） |
| 历史列表 | GET | `/api/history` |
| 播放下载 | GET | `/outputs/<file>.mp3` |

- WiFi：`WiFiManager`，配网 AP 名 **`AI-Music-Setup`**
- 服务器：NVS `server_ip` + **端口写死 5000** + **仅 HTTP**
- 播放：整首下载到 PSRAM → MP3 解码 → I2S1（MAX98357A）；流式代码保留但默认关闭

---

## 3. 硬件接线（摘要）

板子：VIEWE UEDX24320028E-WB-A（ESP32-S3-N16R8）  
完整表：`docs/wiring_guide.md`

| 外设 | 关键脚 |
|------|--------|
| MAX98357A | BCLK=IO14, LRC=IO5, DIN=IO6, VIN=5V, SD=3.3V |
| INMP441 | BCLK=IO10, WS=IO11, DOUT=IO12, VDD=3.3V, L/R=GND |

3.3V 孔少：麦 VDD 与功放 SD 可共用一分二。

---

## 4. 后端 / 网页（2026-07-23 已做）

### 页面结构（`templates/index.html`）

1. **流水线日志监控**（仅生成相关；过滤 health/轮询；可清空）
2. **历史生成歌曲**（播放 + 删除）
3. **上传录音**（浏览器 / ESP32 分区 + 删除）
4. **云端理解流水线**（最下方：录音/跑流水线）

### 热切换（不改 `.env`、不重启）

- 音乐后端：`funmusic` / `minimax`
- 纯音乐开关
- **Mock 模式**（理解+音乐全 mock；mock 成片尽量复制内置 mp3 方便 ESP32 播）

### API 补充

| 路由 | 说明 |
|------|------|
| `GET /api/logs` | 过滤后的生成日志 |
| `DELETE /api/logs` | 清空 pipeline.log |
| `DELETE /api/history/<file>` | 删成片 |
| `DELETE /api/uploads/...` | 删上传 |
| `GET/POST /api/pipeline/music-backend` | 查/改后端与 mock |

### 成片命名

- 新文件：`funmusic-YYYYMMDD-HHMMSS.mp3` / `minimax-...` / `*-mock-...`
- 旧 `funmusic_时间戳.mp3` 仍可播

### 播放器固件修复（已烧录验证 boot）

- 软停 I2S（返回不 `uninstall`）
- 历史列表后台拉取
- 单曲下载上限 6MB
- 增益略降；返回路径顺序修正

---

## 5. 环境变量（启动默认；部分可网页覆盖）

| 变量 | 说明 |
|------|------|
| `DASHSCOPE_API_KEY` | 百炼（理解三步 + funmusic） |
| `MINIMAX_API_KEY` | MiniMax |
| `PIPELINE_MODE` | mock/real（理解三步启动默认） |
| `PIPELINE_MUSIC_MODE` | mock/real（音乐启动默认） |
| `PIPELINE_MUSIC_BACKEND` | funmusic / minimax |
| `PIPELINE_MUSIC_IS_INSTRUMENTAL` | 纯音乐 |
| `BASE_URL` | 返回完整 `output_url` 前缀；ESP32 公网时必设正确 |
| `FUNMUSIC_*` / `DS_*_MODEL` | 模型细节 |
| `MAX_PROMPT_CHARS` | 文字上限 |

示例：`.env.example`

---

## 6. 公网演示方案（讨论结论，固件暂不改）

| 项 | 结论 |
|----|------|
| ESP32 连手机热点 | ✅ 可行（WiFiManager 配热点 SSID） |
| Flask 上公网 + 板子访问 | ✅ 可行；**不必与 Flask 同局域网** |
| **Vercel 跑本 Flask** | ❌ 不合适（长超时 + 本地磁盘） |
| Cloudflare 域名 DNS | ✅ 可用 |
| 简单密码页（无用户库） | ✅ 演示够用；ESP32 API 宜另开 Key 或暂不鉴权 |
| 本版固件 | 仅 **HTTP + IP（或可解析主机）+ 端口 5000** |
| 域名 + HTTPS:443 | 需 **后续改固件** |

推荐宿主：轻量云 / Railway / Render 等长运行+磁盘；演示也可本机 + 端口映射。

---

## 7. 待办

### 近期（开发测试，本版固件）

- [ ] 麦 + 功放全流程：录音 → 上传 → 生成 → 历史播放（可用 Mock 彩排）
- [ ] 复现 2–3 次 real 全链路；关注 Fun-Music 超时（300s）
- [ ] 网页/ESP32 联调日志是否清晰

### 中期（演示 / 公网）

- [ ] 选定公网宿主并部署；设好 `BASE_URL`
- [ ] 板子 `server_ip` 填公网 IP；热点配网彩排
- [ ] 网页简单密码（可选）；演示完删实例

### 后期（固件改造）

- [ ] 支持 host（域名）+ 可配端口
- [ ] 可选 HTTPS
- [ ] 上传等待超时与云端生成时长对齐

### 搁置

- 流式播放（`PLAYER_DEFAULT_STREAM=0`）

---

## 8. 关键路径

```
app.py / services/                 Flask + 流水线
templates/index.html               Web 控制台
static/uploads/  static/outputs/   录音 / 成片
logs/pipeline.log                  业务日志
docs/wiring_guide.md               接线
esp32_firmware_idf55/              ★ 正式固件（唯一）
esp32_firmware_idf55/HANDOFF_IDF55.md
```

---

## 9. 常用命令

```powershell
# Flask（项目根）
.\.venv\Scripts\python.exe app.py
# 浏览器 http://localhost:5000  或  http://<本机局域网IP>:5000

# 固件（完整工程）
cd esp32_firmware_idf55
$env:PLATFORMIO_CORE_DIR = 'D:\platformio_diag'
& 'C:\Users\PC\.platformio\penv\Scripts\pio.exe' run -e BOARD_VIEWE_UEDX24320028E_WB_A -t upload --upload-port COM8
```

联调默认：Flask `192.168.50.246:5000`，ESP32 约 `192.168.50.65`，串口 **COM8**，波特率 **115200**。

---

## 10. 会话内关键结论备忘

1. **杂音**：多为接线/电源/增益；软件增益已略降。  
2. **返回重启**：I2S uninstall + LVGL 线程重活；已软停 + 异步历史。  
3. **「没生成」**：非 mock，卡在 Fun-Music 超时 502。  
4. **闪卡**：历史/上传轮询整表重绘；已改为内容不变不重绘。  
5. **ESP32 工程**：仅保留 `esp32_firmware_idf55/`（诊断工程已删）。
