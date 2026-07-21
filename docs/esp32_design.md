# ESP32 端改造设计文档

> 基于 `ai_music` Flask 云端理解流水线，改造为 ESP32 采集+播放 / 远程服务器编排的混合方案。
> **创建日期：2026-07-16**

---

## 1. 背景与目标

### 1.1 原项目
`ai_music` 是一个 Flask 编排的云端理解流水线：环境音 + 语音/文字 -> 理解环境音 -> ASR -> LLM 创作歌词/风格 -> MiniMax 生成音乐。全部逻辑跑在 PC 上的 Flask，前端是浏览器界面。

### 1.2 改造目标
把「输入采集 + 音乐播放」下沉到 ESP32 端，做成一个带屏幕 UI 的独立设备：
- ESP32 录环境音 + 录语音，上传到远程服务器
- 远程服务器跑现有 Flask 编排（理解/ASR/作词/生成）
- ESP32 拿到生成的音乐 URL，流式播放
- ESP32 带屏幕，三页菜单 UI 操作

### 1.3 选定方案
**方案 B（混合）**：ESP32 只做录音 + 上传 + 播放，编排逻辑全部留在远程 Flask。Flask 核心代码几乎不动。

---

## 2. 方案选型与决策

### 2.1 两条路径对比

| 维度 | 方案 A：ESP32 纯单机 | 方案 B：混合（选定） |
|------|---------------------|---------------------|
| 编排逻辑 | C++ 重写 4 步流水线 | 保留 Flask，不动 |
| 多模态音频上传 | 必须走 REST + base64（接口风险） | 不涉及，Flask 用 SDK |
| ESP32 代码量 | 800-1200 行 C++ | 300-400 行 C++ |
| 稳定性 | 内存管理难，长期运行易崩 | ESP32 逻辑简单，稳 |
| 灵活性 | 去掉服务器依赖 | 依赖一台服务器 |

### 2.2 选 B 的理由
- Flask real 模式尚未联调跑通，先用 ESP32 把链路打通，顺便验证云端
- ESP32 单机稳定性远不如 Flask 编排
- 方案 B 跑通后对音频格式/API 响应都摸熟，再考虑下沉

### 2.3 方案 A 的瓶颈定性（备查）
不是性能/算力瓶颈（推理全在云端），真正瓶颈是：
1. **接口可行性风险**：DashScope 多模态 REST 要求音频为公网 URL 或 base64，ESP32 无公网 IP 只能 base64，百炼是否支持 data URI 需实测，可能直接否决方案
2. **工程稳定性**：C++ 无 GC，4 步串行 + TLS + 大 JSON，内存碎片化易崩
3. **开发量**：编排 + 宽松 JSON 解析 + 兜底用 C++ 状态机重写

---

## 3. 硬件设计

### 3.1 主控板实测配置

板子：**LilyGO T-Display-S3**（核心板/精简版）。通过 esptool 实测：

| 项目 | 实测值 |
|------|--------|
| 芯片 | ESP32-S3 (QFN56) rev v0.2 |
| 核心 | 双核 + LP Core，240MHz |
| 无线 | Wi-Fi + BT 5 (LE) |
| Flash | 16MB（quad，3.3V） |
| PSRAM | 8MB（Embedded PSRAM，AP_3v3） |
| USB | 原生 USB-Serial/JTAG |
| MAC | ec:da:3b:98:c0:d0 |
| 屏幕 | 1.9" ST7789V IPS TFT，170×320，I8080 8位并行，非触控 |

> 等同 ESP32-S3-N16R8，满足两条路径硬件底线。原生 USB 免外置串口芯片，下载/调试/供电一根线搞定。

### 3.2 引脚分配

#### 板载已占用（不可外接）

| 功能 | 引脚 |
|------|------|
| 屏幕 ST7789V BL / PWR_EN / RST / CS / DC / WR / RD | GPIO38 / 15 / 5 / 6 / 7 / 8 / 9 |
| 屏幕 D0-D7 | GPIO39 / 40 / 41 / 42 / 45 / 46 / 47 / 48 |
| 板载按键 BOOT / User | GPIO0 / GPIO14 |
| 电池 ADC | GPIO4 |
| I2C (QWIIC) SDA / SCL | GPIO18 / GPIO17 |
| UART0 TX / RX | GPIO43 / GPIO44 |
| USB D- / D+ | GPIO19 / GPIO20 |

#### 外接设备分配（用空闲 GPIO，已避开 strapping/屏幕/USB/UART）

| 外设 | 信号 | GPIO |
|------|------|------|
| INMP441 麦克风 | BCLK / WS / DIN | 10 / 11 / 12 |
| MAX98357A 功放 | BCLK / LRC / DIN | 13 / 16 / 21 |
| 外接按键（确定） | - | 1 |
| 状态 LED（可选） | - | 2 |
| 板载 BOOT 键 | 上 / 返回 | 0 |
| 板载 User 键 | 下 | 14 |

> 上下用板载两键，确定外接一个到 GPIO1。三键够"上下确定 + 长按返回"。
> 空闲可用 GPIO：1, 2, 10, 11, 12, 13, 16, 21（已避开 GPIO3 等 strapping pin）。

### 3.3 外接硬件清单

| 元件 | 用途 | 备注 |
|------|------|------|
| INMP441 | I2S 录音 | 录环境音 + 语音 |
| MAX98357A + 4Ω3W 喇叭 | I2S 播放 | 板子无扬声器 |
| 轻触按键 ×1 | 确定 | 接 GPIO1 |
| 杜邦线/面包板 | 连接 | - |

### 3.4 屏幕驱动
T-Display-S3 屏幕是 **I8080 8位并行接口**（非 SPI）。用 **Arduino_GFX**（LilyGO 官方示例同款，原生支持并行，配置比 TFT_eSPI 并行模式简单）。

---

## 4. 软件架构

### 4.1 总体架构

```
┌──────────────────────┐        HTTPS/WiFi        ┌─────────────────────────┐
│  ESP32-S3-N16R8      │  ──────────────────────>  │  远程服务器（Flask）      │
│  INMP441 录音         │  POST multipart 音频      │  /api/generate/pipeline  │
│  MAX98357A 播放       │  <──────────────────────  │  DashScope + MiniMax     │
│  ST7789V 屏幕 UI      │  JSON(含完整 output_url)  │  /api/history            │
│  3×按键 操作          │                           │  static/outputs/*.mp3    │
└──────────────────────┘                           └─────────────────────────┘
        │                                                       │
        │  GET mp3 URL 流式播放                                  │
        └───────────────────────────────────────────────────────┘
```

ESP32 只做三件事：录音、上传、播放。编排逻辑全部留在远程 Flask。

### 4.2 云端 Flask 改动

| 改动 | 文件 | 说明 |
|------|------|------|
| 返回完整 URL | app.py / cloud_pipeline.py | `output_url` 现为 `/outputs/xxx.mp3`，ESP32 拼不出主机。加 `BASE_URL` 配置，返回 `https://域名/outputs/xxx.mp3` |
| 新增历史接口 | app.py | `GET /api/history` 列出 `static/outputs/` 下 mp3 + 元数据，给 ESP32 历史列表页用 |
| 接口契约 | 无需改 | `/api/generate/pipeline` 已是 multipart form，ESP32 直接能用 |
| 生产化 | 部署阶段 | gunicorn + nginx + HTTPS 替代 `app.run(debug=True)` |

### 4.3 ESP32 固件模块（PlatformIO，Arduino 框架）

| 模块 | 职责 | 关键库 |
|------|------|--------|
| display | Arduino_GFX 初始化 + UI 渲染（三级菜单） | Arduino_GFX |
| input | 3 键状态机（上/下/确定/长按返回） | - |
| recorder | I2S 录 PCM -> WAV 头封装 -> PSRAM 缓冲 | driver/i2s |
| uploader | multipart POST 到 `/api/generate/pipeline` | HTTPClient |
| player | 流式播放 mp3 URL | ESP8266Audio |
| history | GET `/api/history` -> 列表 -> 选播 | ArduinoJson + HTTPClient |
| config | WiFi / 服务器地址 / API 配置（NVS 存储） | Preferences |

固件库依赖（`platformio.ini`）：
- `Arduino_GFX` - 屏幕驱动
- `ESP8266Audio` - MP3 流式播放
- `ArduinoJson` - JSON 解析
- `WiFiClientSecure` / `HTTPClient` - HTTPS + multipart 上传

---

## 5. UI 设计

三页菜单状态机：

```
主菜单（上下选，确定进）
 ├─ 生成歌曲 ─确定─> 生成流程
 │                 ├─ ① 记录心里话：[录制] [取消] [下一步]
 │                 └─ ② 记录环境音：[录制] [取消] [生成] ─> 上传 ─> 播放
 └─ 历史歌曲 ─确定─> 历史列表
                   └─ [上] [下] [播放]
```

操作映射：
- 板载 BOOT 键（GPIO0）= 上 / 返回（长按）
- 板载 User 键（GPIO14）= 下
- 外接键（GPIO1）= 确定 / 生成

各页面元素：
- 主菜单：两行选项，高亮当前项
- 录音页：倒计时 + 录制波形/状态 + 三个操作提示
- 生成中：进度/等待提示 + 旋转动画
- 播放页：歌曲信息 + 播放进度
- 历史列表：歌曲名列表，上下滚动，确定播放

---

## 6. 接口契约

### 6.1 生成流水线（已有，ESP32 直接用）

```
POST /api/generate/pipeline
Content-Type: multipart/form-data

env_audio: <wav 文件>        # 环境音
speech_audio: <wav 文件>      # 语音（想说的话）
duration_sec: 30

-> 200 JSON:
{
  "output_url": "https://域名/outputs/xxx.mp3",  # 改为完整 URL
  "steps": {...},
  "lyrics": "...",
  "music_style": "...",
  "total_elapsed_sec": 45.2
}
```

### 6.2 历史列表（新增）

```
GET /api/history

-> 200 JSON:
{
  "songs": [
    {"name": "minimax_1737000000.mp3", "url": "https://域名/outputs/xxx.mp3", "created": "..."},
    ...
  ]
}
```

### 6.3 音频文件服务（已有）
`GET /outputs/<filename>` 返回 mp3，ESP32 流式拉取播放。

---

## 7. 分阶段任务计划

| 阶段 | 内容 | 依赖 |
|------|------|------|
| 1 | Flask：`output_url` 完整化 + `/api/history` 接口 | 无（纯本机，可立即做） |
| 2 | ESP32：PlatformIO 工程骨架 + Arduino_GFX 点亮屏 + 3键菜单 UI | 板子 + PlatformIO 装好 |
| 3 | ESP32：I2S 录音 + WAV 封装（录到 PSRAM，串口回放验证） | 阶段 2 |
| 4 | ESP32：multipart 上传到 Flask + 解析响应（本机 HTTP 联调） | 阶段 1 + 3 |
| 5 | ESP32：ESP8266Audio 流式播放 output_url | 阶段 4 |
| 6 | ESP32：历史歌曲页 + `/api/history` 对接 | 阶段 5 |
| 7 | 部署：Flask 上远程服务器 + HTTPS，ESP32 改连公网 | 全部本机联调通过 |

### 当前状态
- [x] 硬件实测确认（ESP32-S3 N16R8 + ST7789V）
- [x] 方案选型（方案 B 混合）
- [x] 引脚分配确定
- [x] 设计文档归档（本文件）
- [ ] PlatformIO 安装（用户自行在 IDE 装插件）
- [ ] 阶段 1：Flask 适配

---

## 8. 风险与注意事项

| 风险 | 说明 | 应对 |
|------|------|------|
| 屏幕型号未实测验证 | esptool 读不到屏幕 IC，型号靠丝印 | 阶段 2 点亮屏即验证；若不亮再排查引脚/驱动 |
| HTTPS 内存 | ESP32 TLS 握手占 ~30-50KB RAM | 本机联调先用 HTTP，部署再上 HTTPS |
| multipart 上传 | HTTPClient 不直接支持文件+字段混发 | 手拼 boundary body |
| MP3 流式播放 | 内存有限不能整段装 | ESP8266Audio 边下边播不落盘 |
| 录音格式 | I2S 录的是 PCM | 手写 44 字节 WAV 头；ASR 建议 16kHz 单声道 |
| 音频上传大小 | 8s×16kHz×2B = 256KB | 放 PSRAM，分段上传 |
| 服务器环境未定 | "远程服务器"具体环境暂未确定 | 先本机联调，部署阶段再定 |

---

## 9. 开发环境

- **ESP32 固件**：PlatformIO（用户自行在 VSCode/Trae 装插件，插件自动装 core 到 `~/.platformio`）
- **板设置**（参考 T-Display-S3 官方）：
  - Board: ESP32S3 Dev Module
  - Flash Size: 16MB
  - Partition: 16M Flash (3M APP / 9.9MB FATFS)
  - PSRAM: OPI PSRAM
  - USB CDC On Boot: Enable
  - Upload Mode: UART0/Hardware CDC
- **云端**：现有 Flask + `.venv`，部署用 gunicorn + nginx

---

## 10. 相关文件

- [PROJECT_STATUS.md](../PROJECT_STATUS.md) - 原项目状态
- [README.md](../README.md) - 原项目说明
- [app.py](../app.py) - Flask 入口
- [services/cloud_pipeline.py](../services/cloud_pipeline.py) - 流水线编排
- [services/dashscope_client.py](../services/dashscope_client.py) - 百炼三能力
- [services/minimax_music_client.py](../services/minimax_music_client.py) - MiniMax 音乐生成
