# ai_music · 音乐生成流水线 + ESP32

基于 Flask 的「环境音 + 语音/文字 → 音乐」云端流水线；ESP32-S3 负责录音上传与整首下载播放。

## 流水线

1. **采集**：浏览器 / ESP32 录环境音 + 语音（或打字）
2. **理解环境音**：`qwen3-omni-flash`
3. **ASR**：`qwen3-asr-flash`（有文字则跳过）
4. **歌词+风格**：`qwen-plus` → JSON（短歌词，目标 60–90s 成片）
5. **音乐生成**：默认 `fun-music-v1`，可切 MiniMax

## 快速开始

```powershell
python -m venv .venv
.\.venv\Scripts\Activate.ps1
pip install -r requirements.txt
copy .env.example .env
python app.py   # http://localhost:5000
```

real 模式见 `.env.example`：`PIPELINE_MODE=real`、`PIPELINE_MUSIC_MODE=real`、`DASHSCOPE_API_KEY=...`。

ESP32 访问时设 `BASE_URL=http://<本机局域网IP>:5000`。

## API

| 方法 | 路径 | 说明 |
|------|------|------|
| GET  | `/api/pipeline/health` | 健康检查 |
| POST | `/api/generate/pipeline` | 完整流水线 |
| GET  | `/api/history` | 已生成曲目列表 |
| GET  | `/api/samples` | 内置素材 |
| GET  | `/api/uploads` | 上传文件列表 |

流水线 form：`env_audio` 或 `env_sample` + `speech_audio` 或 `user_text` + `duration_sec`(30–90，默认90) + 可选 `source=esp32`。

## 时长说明

Fun-Music / MiniMax **均无硬时长参数**。项目通过短歌词（LLM 约束）与 MiniMax prompt 软提示尽量落在约 90 秒内，便于 ESP32 整首下载。**不截断音频**，保持成片完整。

## ESP32

- 固件：`esp32_firmware_idf55/`（IDF 5.5）
- 接线：`docs/wiring_guide.md`
- 交接：`esp32_firmware_idf55/HANDOFF_IDF55.md`
- 播放：**仅下载播放**；流式代码保留但搁置

## 目录

```
app.py / services/          编排与云端客户端
templates/index.html        Web 面板
esp32_firmware_idf55/       正式固件
docs/wiring_guide.md        接线
PROJECT_STATUS.md           进展与待办
```
