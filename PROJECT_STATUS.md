# ai_music · 音乐生成流水线

> 本项目只做一件事：基于环境音 + 语音/文字，走云端理解流水线生成音乐。
> 不做成品、不含产品形态。本文档记录目标、架构、配置、进展与待办。
> **最近更新：2026-07-20** - 音乐后端新增 fun-music-v1（阿里云百炼）并设为默认，可与 minimax 切换/对比

---

## 1. 测试目标

基于环境音 + 语音/文字生成音乐：先理解环境音、ASR 转写、LLM 创作歌词/风格，再生成音乐。

核心目标：验证「先理解再做提示词工程」的云端流水线，在环境音 + 用户语音/文字输入下的生成效果。

---

## 2. 方案说明

**云端理解流水线（唯一方案）**：音频+文本不直接当 prompt，而是先走云端三步理解，再做提示词工程，最后生成音乐。

- 思路：理解环境音 -> ASR -> LLM 创作歌词/风格 -> 生成
- 核心组件：DashScopeClient + CloudPipeline
- 云端依赖：阿里云百炼 DashScope
- 模式开关：`PIPELINE_MODE` + `DASHSCOPE_API_KEY`
- 音乐后端：`PIPELINE_MUSIC_BACKEND` 可插拔（默认 `funmusic`，支持 `funmusic` / `minimax`）
- 路由：`/api/pipeline/*`
- 前端面板：录音/上传/文字 -> 分步骤结果

云端三步（理解/ASR/LLM）与音乐生成步分别可切 mock/real、分别配置，互不影响。

---

## 3. 整体架构

```
┌────────────────────────────┐
│  Web 测试界面（浏览器）      │  录音/上传/文本输入
│  templates/index.html       │  流水线面板
└──────────┬─────────────────┘
           │ HTTP
┌──────────┴─────────────────┐
│  编排层（Flask，本项目）     │  D:\project\ai_music
│  DashScopeClient +          │
│  CloudPipeline              │
└──────┬─────────────────────┘
       │
   云端四步
       │
┌──────┴──────────────────────┐
│ 阿里云百炼 DashScope         │
│  qwen3-omni-flash            │  -> 环境音理解
│  qwen3-asr-flash             │  -> 语音识别(ASR)
│  qwen-plus                   │  -> 歌词/风格创作
└──────────┬──────────────────┘
           │
┌──────────┴──────────────────┐
│ MiniMax Music API           │  -> 最终音乐
└─────────────────────────────┘
```

关键边界：百炼云端负责理解/ASR/创作三步；音乐生成步走 MiniMax 云端 API，不占本地 GPU。

---

## 4. 关键选型

| 决策项 | 选择 | 理由 |
|--------|------|------|
| 音频理解 | **qwen3-omni-flash**（百炼） | 全模态，直接吃环境音输出场景/情绪/节奏；100万 token 90天免费 |
| 语音识别 | **qwen3-asr-flash**（百炼） | 专业 ASR，10 小时免费额度；本地音频 SDK 自动上传 |
| 歌词创作 | **qwen-plus**（百炼） | 融合场景+用户的话，输出歌词+风格 JSON；100万 token 90天免费 |
| 音乐后端 | **fun-music-v1**（阿里云百炼，默认）/ MiniMax Music API | 云端生成；fun-music 复用百炼 Key、支持男/女声与纯音乐；通过可插拔接口便于多后端对比 |

> 旧模型 `qwen-audio-turbo` 官方明确「仅供免费体验、用完不可调用」，`paraformer-realtime-v2` 无免费额度，均已弃用，改用 qwen3 系列。

---

## 5. 当前进展

- [x] 本地 Flask 骨架（mock 模式跑通）
- [x] DashScopeClient + CloudPipeline（理解/ASR/创作/生成四步，mock/real）
- [x] Web 流水线界面（含 MediaRecorder 录音）
- [x] mock 模式端到端验证通过（流水线全链路、WAV 合法可下载）
- [x] 5 个免费音频素材下好
- [ ] real 模式联调验证（API Key 已配置，待实际跑通全链路）

---

## 6. 待办清单

### 短期：real 模式联调验证

1. **real 联调**：`.env` 已配置 `PIPELINE_MODE=real` + `PIPELINE_MUSIC_MODE=real`，跑 `/api/generate/pipeline` 验证理解->ASR->创作->生成全链路
2. **效果试听**：下载 `static/outputs/` 下生成的音频，评估语义还原度与音乐质量

### 中期（如验证成功）

- 音乐后端扩展：接入 ACE-Step / YuE / MusicGen+CosyVoice，做多后端横向对比
- 歌词接入带人声模型（MiniMax 已支持人声，可进一步调优）

---

## 7. 已识别的风险 / 坑

| 风险 | 说明 | 应对 |
|------|------|------|
| ASR 格式 | 浏览器录的是 webm/opus，识别效果以实际为准 | 建议 16kHz 单声道 wav；给了文字则跳过 ASR |
| LLM 输出 JSON 不规范 | Qwen 偶尔带代码块/多余文字 | 已做宽松解析 + 兜底，不会崩 |
| 百炼计费 | real 模式按调用量计费 | mock 模式不花钱；测试时控制调用次数 |

---

## 8. 接口契约

### 8.1 本地编排层（Flask）

| 方法 | 路径 | 说明 |
|------|------|------|
| GET  | `/api/pipeline/health` | 流水线健康检查 |
| POST | `/api/generate/pipeline` | 完整流水线（环境音+语音/文字） |
| GET  | `/api/samples` | 内置素材列表 |

流水线入参（form）：`env_audio`(文件)或`env_sample`(素材名) + `speech_audio`(文件)或`user_text`(文字) + `duration_sec`。

返回 JSON 含各步结果与 `output_url`（指向 `/outputs/<file>`），浏览器 `<audio src>` 直接能播。

---

## 9. 配置项速查（`.env`）

| 配置项 | 说明 |
|--------|------|
| `DASHSCOPE_API_KEY` | 百炼 API Key，留空则降级 mock |
| `PIPELINE_MODE` | mock / real，管控理解/ASR/创作三步 |
| `DS_AUDIO_MODEL` | 默认 `qwen3-omni-flash`（全模态理解环境音） |
| `DS_ASR_MODEL` | 默认 `qwen3-asr-flash`（10小时免费） |
| `DS_LLM_MODEL` | 默认 `qwen-plus`（100万 token 90天免费） |
| `PIPELINE_MUSIC_BACKEND` | 音乐后端类型，默认 `funmusic`（支持 `funmusic` / `minimax`） |
| `PIPELINE_MUSIC_MODE` | 音乐步 mock/real，默认 `mock`（云端生成） |
| `MINIMAX_API_KEY` | MiniMax Music API Key（用 minimax 后端时必填） |
| `FUNMUSIC_MODEL` | fun-music 模型，默认 `fun-music-v1`（复用 DASHSCOPE_API_KEY） |
| `FUNMUSIC_GENDER` | 演唱性别 `male`/`female`，默认 `female`（纯音乐时无效） |
| `FUNMUSIC_FORMAT` | fun-music 输出格式 `mp3`/`wav`，默认 `mp3` |
| `PIPELINE_MUSIC_IS_INSTRUMENTAL` | 是否纯音乐。`true`=纯音乐，`false`=有人声（会演唱歌词），默认 `false` |
| `MAX_PROMPT_CHARS` | prompt 文本长度上限 |

### 9.1 百炼免费额度（2026-07-16 核实）

| 模型 | 免费额度 | 备注 |
|------|----------|------|
| `qwen3-omni-flash` | 100万 token / 90天 | 全模态；理解任务须 `enable_thinking=False` |
| `qwen3-asr-flash` | 36,000秒（10小时）| 专业 ASR |
| `qwen-plus` | 100万 token / 90天 | LLM，0.8元/百万 token（超额付费） |
| ~~`qwen-audio-turbo`~~ | ❌ | 官方说"仅供免费体验、用完不可调用" |
| ~~`paraformer-realtime-v2`~~ | ❌ | 无免费额度，直接扣费 |

- 仅华北2（北京）+ 中国内地部署的模型有免费额度
- 默认未实名用户额度耗尽返回 `AllocationQuota.FreeTierOnly`，**不扣费**
- Token Plan / Coding Plan 专属 API Key **不消耗免费额度**（必须用通用 Key）

---

## 10. 项目结构

```
D:\project\ai_music\
├── app.py                          # Flask 入口：流水线装配 + 路由
├── services/
│   ├── __init__.py
│   ├── dashscope_client.py         # 百炼三能力（理解/ASR/LLM，mock/real）
│   ├── cloud_pipeline.py           # 流水线编排（音乐后端可插拔）
│   └── minimax_music_client.py     # MiniMax Music 客户端（mock/real）
├── templates/
│   └── index.html                  # 流水线界面
├── audio_samples/                  # 5 个免费素材（烟雾测试用）
├── static/
│   ├── uploads/                    # 用户上传音频
│   └── outputs/                    # 生成的 wav
├── logs/                           # pipeline.log 运行日志
├── .venv/                          # Python 虚拟环境
├── requirements.txt                # flask/requests/python-dotenv/dashscope
├── .env / .env.example
├── README.md
└── PROJECT_STATUS.md               # 本文件
```

---

## 11. 音频素材清单（`audio_samples/`，烟雾测试用）

| 文件名 | 时长 | 来源 | 协议 |
|--------|------|------|------|
| `wind_ambient_840139.mp3` | 75s | freesound (Exder) | CC0 |
| `birds_spring_forest_679904.mp3` | 133s | freesound (mfraczek) | CC0 |
| `forest_birds_505195.mp3` | 21s | freesound (shelbyshark) | CC0 |
| `brazil_woods_birds_wind_859943.mp3` | 54s | freesound (pa_909) | CC0 |
| `stream_water_339924.mp3` | 61s | freesound (InspectorJ) | CC-BY 4.0（需署名） |

---

## 12. 关键命令速查

### 本地（Windows PowerShell）

```powershell
# 启动 Flask（.env 已配 real，直接跑真实流水线）
.\.venv\Scripts\python.exe app.py
# 浏览器开 http://localhost:5000

# 如需 mock 模式调试：.env 设 PIPELINE_MODE=mock / PIPELINE_MUSIC_MODE=mock
```
