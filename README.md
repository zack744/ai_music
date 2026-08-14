# 🎵 ai_music · 有趣的魔法音乐盒

一个会"听"的魔法音乐盒：录下一段环境音、说一句话，它就能变出一首属于你的歌。

**Cloud music pipeline** (env audio + speech/text → generated track) with an **ESP32-S3** client for record/upload and full-file download playback.

```
[ESP32-S3 录音/上传]              [浏览器 Web 控制台]
        │  HTTP                        │
        ▼                              ▼
   ┌─────────────────────────────────────────┐
   │  Flask 后端 + CloudPipeline 流水线       │
   │  ① 环境音理解 qwen3-omni-flash          │
   │  ② ASR        qwen3-asr-flash           │
   │  ③ 歌词+风格  qwen-plus                 │
   │  ④ 音乐生成  Fun-Music / MiniMax        │
   └─────────────────────────────────────────┘
        │  MP3
        ▼
   [云端成片 → ESP32 整首下载播放 / 网页播放]
```

## 样例输入

`audio_samples/` 里放了 5 段可用的环境音（鸟鸣 / 森林 / 溪流 / 海风等），直接上传即可试跑流水线。

## 流水线 / Pipeline

1. **采集** Capture：浏览器或 ESP32 录环境音 + 语音（或打字）
2. **环境音理解** Env understanding：`qwen3-omni-flash`
3. **ASR**：`qwen3-asr-flash`（已有文字则跳过）
4. **歌词 + 风格** Lyrics/style：`qwen-plus` → JSON（短歌词，目标约 60–90s）
5. **音乐生成** Music：默认 `fun-music-v1`，可热切换 MiniMax / Mock

## 快速开始 / Quick start

```powershell
python -m venv .venv
.\.venv\Scripts\Activate.ps1
pip install -r requirements.txt
copy .env.example .env
# 编辑 .env：填入 DASHSCOPE_API_KEY（及可选 MINIMAX_API_KEY）
python app.py   # http://localhost:5000
```

- Real 云端：`.env` 中 `PIPELINE_MODE=real`、`PIPELINE_MUSIC_MODE=real`，并配置 API Key  
- ESP32 下载成片：局域网设 `BASE_URL=http://<本机IP>:5000`；公网设 `BASE_URL=https://你的域名` 或 `http://公网IP:端口`（须与固件 Host/Port/HTTPS 一致）  
- **简易鉴权（公网推荐）**：设 `SITE_PASSWORD`；浏览器登录；设备请求头 `X-API-Key`（默认与密码相同，可用 `API_ACCESS_KEY` 单独设）。留空则关闭鉴权。  
- 生产（Linux）：`gunicorn -b 127.0.0.1:5000 -w 1 --timeout 360 "app:app"`，前面用 Nginx 反代（超时 ≥360s，body ≥40m）  
- **切勿提交 `.env`**（已在 `.gitignore`）

## Web 控制台

页面顺序：日志监控 → 历史歌曲 → 上传录音 → 流水线。

- 日志仅展示生成过程（过滤 health/轮询），可清空  
- 历史/上传可删除  
- 热切换：`funmusic` / `minimax`、纯音乐、Mock（`POST /api/pipeline/music-backend`，不改 `.env`）  
- 成片命名：`funmusic-YYYYMMDD-HHMMSS.mp3` 等  

## API（摘要）

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/pipeline/health` | 健康检查 |
| POST | `/api/generate/pipeline` | 完整流水线（multipart / form） |
| GET | `/api/history` | 成片列表 |
| DELETE | `/api/history/<file>` | 删成片 |
| GET/DELETE | `/api/logs` | 生成日志 |
| GET/POST | `/api/pipeline/music-backend` | 查/改音乐后端与 mock |
| GET | `/outputs/<file>.mp3` | 成片下载（ESP32 播放） |

流水线字段：`env_audio` 或 `env_sample` + `speech_audio` 或 `user_text` + 可选 `duration_sec`(30–90) + `source=esp32`。

## ESP32 固件

- **唯一正式工程**：`esp32_firmware_idf55/`（pioarduino / IDF 5.5）  
- 板卡示例：VIEWE UEDX24320028E-WB-A（ESP32-S3-N16R8）  
- 接线：`docs/wiring_guide.md`  
- 迁移与坑：`esp32_firmware_idf55/HANDOFF_IDF55.md`  
- 进展：`PROJECT_STATUS.md`  

通信：可配 **Host（IP/域名）+ Port + HTTPS**（NVS；WiFiManager 门户与屏上 Settings）。  
局域网默认 `http://IP:5000`；公网推荐 `https://域名:443`。上传等响应最长约 **360s**。

| 动作 | 路径 |
|------|------|
| 上传生成 | `POST /api/generate/pipeline`（`source=esp32`） |
| 历史 | `GET /api/history` |
| 播放 | `GET /outputs/<mp3>`（或响应里的完整 `output_url`） |

播放：**整首下载到 PSRAM → MP3 → I2S**（支持 http/https）；流式代码保留但默认关闭。  
配网 AP：`AI-Music-Setup`（可填 Backend Host / Port / HTTPS）。

### 构建烧录（示例）

```powershell
cd esp32_firmware_idf55
# 按本机 PlatformIO 路径与串口修改
$env:PLATFORMIO_CORE_DIR = 'D:\platformio_diag'   # 可选
& 'C:\Users\PC\.platformio\penv\Scripts\pio.exe' run -e BOARD_VIEWE_UEDX24320028E_WB_A -t upload --upload-port COMx
```

### 近期固件要点

- 触摸：CHSC6540 raw 线性校准（`TOUCH_CAL_*` in `config.h`；`TOUCH_CALIB_LOG=1` 可采点）  
- 返回停播：`player_stop_async()` — UI 不堵，后台持锁释放；保留 SoftAudio I2S 软停 + mutex  
- 历史列表后台拉取，避免 LVGL 线程阻塞  

## 时长说明

Fun-Music / MiniMax **无硬时长参数**。靠短歌词与 prompt 软提示落在约 90s 内，便于 ESP32 整首缓存。**不截断音频**。

## 安全 / Security

- API Key 只放在本地 `.env`，参考 `.env.example`  
- 不要把局域网 IP、WiFi 名称、串口日志中的真实环境信息当密钥泄露；公开仓库中的示例已用占位符  
- 公网部署勿用 Vercel 跑本 Flask（长超时 + 本地磁盘）；需长运行主机并设好 `BASE_URL`  

## 目录

```
app.py / services/           Flask 编排与云端客户端
templates/index.html         Web 控制台
static/uploads|outputs/      录音 / 成片（运行时生成，不入库）
esp32_firmware_idf55/        ★ 正式固件
docs/wiring_guide.md         功放 / 麦克风接线
PROJECT_STATUS.md            状态与待办
.env.example                 环境变量模板
```

## License

见 `LICENSE`。
