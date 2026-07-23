# ai_music · 音乐生成流水线 + ESP32 设备

> 基于环境音 + 语音/文字，走云端理解流水线生成音乐；ESP32 负责采集/播放。
> **最近更新：2026-07-23** — real 全链路已通；ESP32 IDF5.5 下载播放稳定；流式播放搁置

---

## 1. 目标

验证「先理解再做提示词工程」的云端流水线，并在 ESP32 上跑通：

**录音 → 上传 → 云端生成 → 整首下载 → 功放播放**

---

## 2. 架构

```
ESP32 / 浏览器
  录音/上传/文字
        │ HTTP
        ▼
Flask 编排 (本机)
  DashScopeClient + CloudPipeline
        │
   ┌────┴────┐
   ▼         ▼
百炼三步    音乐后端（可插拔）
理解/ASR/LLM  funmusic（默认）/ minimax
```

- 云端三步：`PIPELINE_MODE` + `DASHSCOPE_API_KEY`
- 音乐步：`PIPELINE_MUSIC_BACKEND` + `PIPELINE_MUSIC_MODE`（默认 funmusic / real）
- 播放策略：**仅整首下载到 PSRAM**（流式播放代码保留但搁置）

---

## 3. 当前进展

### 云端流水线
- [x] Flask 骨架 + mock 全链路
- [x] DashScope 理解 / ASR / LLM（real）
- [x] fun-music-v1 / MiniMax 可插拔后端
- [x] **real 端到端已通**（2026-07-22）：理解→ASR→创作→funmusic→输出 mp3，总耗时约 45s
- [x] ESP32 调 `POST /api/generate/pipeline` 并下载播放成功

### ESP32 固件（`esp32_firmware_idf55/`，IDF 5.5）
- [x] 迁移到 pioarduino 55.03.39 / Arduino-ESP32 3.3.9 / IDF 5.5.4
- [x] WiFi、LCD、触摸、LVGL 六页 UI
- [x] HTTP 整首下载 → PSRAM → MP3 解码 → I2S1 功放
- [x] 历史上传/历史列表播放
- [ ] 功放+喇叭实机听感（今日接线测试）
- [ ] INMP441 麦克风完整实测
- [~] 流式播放：**搁置**（有跨核竞态；默认 `PLAYER_DEFAULT_STREAM=0`）

---

## 4. 时长与体积策略（ESP32 友好）

两端音乐 API **均无硬 duration 字段**。

| 手段 | 说明 |
|------|------|
| 短歌词 | LLM 约束主歌+副歌、中文约 ≤80 字，目标成片 60–90s |
| MiniMax | prompt 追加 `about N seconds`；bitrate 128kbps |
| funmusic | 不截断、不改 API；靠短歌词间接变短 |
| 上限 | 请求 `duration_sec` clamp 到 30–90，默认 90 |

约 90s @128kbps ≈ 1.4MB，可整首进 8MB PSRAM。

---

## 5. 待办（近期）

1. **今日**：接 MAX98357A + 喇叭，验证真实播放；接 INMP441 验证录音上传
2. 复现 2–3 次「录音→生成→下载播放」
3. ASR 空文本时的兜底提示（语音过短/过轻）
4. 播放器小优化：结束释 PSRAM、下载体积上限
5. ~~流式播放~~ 搁置，后续如需再开

---

## 6. 配置速查

| 配置 | 说明 |
|------|------|
| `DASHSCOPE_API_KEY` | 百炼 Key；空则 mock |
| `PIPELINE_MODE` | mock / real（理解/ASR/LLM） |
| `PIPELINE_MUSIC_BACKEND` | `funmusic`（默认）/ `minimax` |
| `PIPELINE_MUSIC_MODE` | mock / real |
| `PIPELINE_MUSIC_IS_INSTRUMENTAL` | 纯音乐 true / 有人声 false |
| `FUNMUSIC_*` | model / gender / format |
| `MINIMAX_API_KEY` | minimax 时必填 |
| `BASE_URL` | ESP32 用完整 URL 前缀，如 `http://192.168.50.246:5000` |

---

## 7. 关键路径

```
app.py / services/          Flask + 流水线
templates/index.html        Web 测试页
esp32_firmware_idf55/       正式固件（唯一）
esp32_diagnostics/          PSRAM/WiFi 诊断（历史参考）
docs/wiring_guide.md        功放/麦接线
esp32_firmware_idf55/HANDOFF_IDF55.md  固件交接
```

---

## 8. 常用命令

```powershell
# Flask
.\.venv\Scripts\python.exe app.py

# 固件构建/烧录（COM8）
cd esp32_firmware_idf55
$env:PLATFORMIO_CORE_DIR='D:\platformio_diag'
$env:PATH='C:\Users\PC\.platformio\penv\Scripts;' + $env:PATH
pio run -e BOARD_VIEWE_UEDX24320028E_WB_A -t upload --upload-port COM8
```
