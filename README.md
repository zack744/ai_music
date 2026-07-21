# ai_music · 音乐生成流水线

基于 Flask 的「音频+文本 -> 音乐」云端理解流水线：先理解环境音、ASR 转写语音、LLM 创作歌词/风格，再生成音乐。

核心组件：DashScopeClient（阿里云百炼理解/ASR/LLM）+ CloudPipeline（流水线编排）+ 可插拔音乐后端（默认 fun-music-v1，走云端；可切 minimax）。

## 流水线（对应赛题五步）

1. **采集输入**：前端用 `MediaRecorder` 录环境音 + 语音；也支持上传/选内置素材，"想说的话"可直接打字。
2. **理解环境音**：qwen3-omni-flash 输出"场景/情绪/节奏/适合的音乐风格"。
3. **语音识别**：qwen3-asr-flash 把语音转文字；给了文字则跳过。
4. **创作歌词+风格**：Qwen 融合场景与用户的话，输出 JSON（lyrics / music_style / mood）。
5. **音乐生成**：用 `music_style` 当 prompt，交给可插拔音乐后端（默认 fun-music-v1，走云端；可切 minimax）。

每一步的中间产物都回传前端，方便观察"提示词工程"的效果。

## 目录结构

```
ai_music/
├── app.py                          # Flask 入口：流水线装配 + 路由
├── compare_music.py                # fun-music vs minimax 同提示词对比脚本
├── services/
│   ├── dashscope_client.py         # 百炼三能力（理解/ASR/LLM，mock/real）
│   ├── cloud_pipeline.py           # 流水线编排（音乐后端可插拔）
│   ├── funmusic_client.py          # fun-music-v1 音乐生成客户端（mock/real）
│   └── minimax_music_client.py     # minimax 音乐生成客户端（mock/real）
├── templates/index.html            # 单页：流水线交互界面
├── audio_samples/                  # 内置免费音频素材
├── static/uploads/                 # 用户上传音频
├── static/outputs/                 # 生成 wav/mp3 落盘
├── requirements.txt
├── .env.example
└── README.md
```

## 快速开始

```powershell
python -m venv .venv
.\.venv\Scripts\Activate.ps1
pip install -r requirements.txt
copy .env.example .env   # 默认流水线为 mock，开箱即跑
python app.py            # http://localhost:5000
```

## 切换到真实后端

**百炼云端**：
```
DASHSCOPE_API_KEY=sk-xxxxxxxx
PIPELINE_MODE=real
# 音乐后端默认 minimax（走云端，需 MINIMAX_API_KEY）
MINIMAX_API_KEY=xxxxxxxx
PIPELINE_MUSIC_MODE=real
```

> 音乐生成步骤默认使用 fun-music-v1（阿里云百炼云端，复用 DASHSCOPE_API_KEY）。
> 想换 MiniMax，设 `PIPELINE_MUSIC_BACKEND=minimax` + `MINIMAX_API_KEY`；
> 想换 ACE-Step / YuE / MusicGen+CosyVoice，在 `app.py` 按 `PIPELINE_MUSIC_BACKEND`
> 增加分支，只要客户端实现 `text_to_music(prompt, duration_sec, lyrics="") -> Path` 即可接入。
> 同提示词横向对比见 `compare_music.py`。

## API 一览

| 方法 | 路径 | 说明 |
|------|------|------|
| GET  | `/api/pipeline/health` | 流水线健康检查 |
| POST | `/api/generate/pipeline` | 完整流水线 |
| GET  | `/api/samples` | 内置素材列表 |

流水线入参（form）：
- `env_audio`（文件，必填）或 `env_sample`（素材名）
- `speech_audio`（文件，可选）或 `user_text`（文字，二选一）
- `duration_sec`

## 配置项说明

| 配置项 | 说明 |
|--------|------|
| `DASHSCOPE_API_KEY` | 百炼平台 API Key。留空 -> 流水线自动降级为 mock |
| `PIPELINE_MODE` | `mock` \| `real`。控制「理解/ASR/LLM」三步是否真调云端 |
| `DS_AUDIO_MODEL` | 环境音理解模型（默认 `qwen3-omni-flash`） |
| `DS_ASR_MODEL` | 语音识别模型（默认 `qwen3-asr-flash`） |
| `DS_LLM_MODEL` | 歌词/风格创作模型（默认 `qwen-plus`） |
| `PIPELINE_MUSIC_BACKEND` | 音乐后端类型（默认 `funmusic`，可选 `minimax`） |
| `PIPELINE_MUSIC_MODE` | `mock` \| `real`。控制音乐生成是否真调云端 |
| `MINIMAX_API_KEY` | MiniMax API Key（使用 minimax 后端时必填） |
| `FUNMUSIC_MODEL` | fun-music 模型（默认 `fun-music-v1`，复用 DASHSCOPE_API_KEY） |
| `FUNMUSIC_GENDER` | 演唱性别 `male`/`female`（默认 `female`，纯音乐时无效） |
| `FUNMUSIC_FORMAT` | fun-music 输出格式 `mp3`/`wav`（默认 `mp3`） |
| `PIPELINE_MUSIC_IS_INSTRUMENTAL` | 是否生成纯音乐（无人声）。`true`=纯音乐，`false`=有人声（会演唱歌词）。默认 `false` |
| `MAX_PROMPT_CHARS` | 一次最多接收的 prompt 文本长度（保护后端，默认 500） |

## 内置音频素材

| 文件 | 时长 | 来源 | 协议 |
|------|------|------|------|
| `wind_ambient_840139.mp3` | 75s | freesound (Exder) | CC0 |
| `birds_spring_forest_679904.mp3` | 133s | freesound (mfraczek) | CC0 |
| `forest_birds_505195.mp3` | 21s | freesound (shelbyshark) | CC0 |
| `brazil_woods_birds_wind_859943.mp3` | 54s | freesound (pa_909) | CC0 |
| `stream_water_339924.mp3` | 61s | freesound (InspectorJ) | CC-BY 4.0 |

## 注意事项

- 流水线默认是 mock：返占位的场景/歌词/风格 + 静音 wav，用于跑通链路。
- real 模式：本地音频由 dashscope SDK 自动上传 OSS，无需自建文件服务。
- ASR（`qwen3-asr-flash`）建议 16kHz 单声道 wav；浏览器录的 webm/opus 也能用，识别效果以实际为准。
- 真实音乐推理 30 秒音频通常需要 30 秒~几分钟。
