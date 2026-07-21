#pragma once
#include <Arduino.h>

// =====================================================================
// 全局配置：引脚 / 常量。引脚分配严格遵守 docs/esp32_design.md，勿改。
// 板子: LilyGO T-Display-S3 (ESP32-S3-N16R8)
// =====================================================================

// ---------- 屏幕 ST7789V (I8080 8位并行) ----------
#define TFT_WIDTH       170     // 物理短边
#define TFT_HEIGHT      320     // 物理长边
#define TFT_BL          38      // 背光
#define TFT_PWR_EN      15      // 屏幕电源使能（电池供电时必须拉高）
#define TFT_RST         5
#define TFT_CS          6
#define TFT_DC          7
#define TFT_WR          8
#define TFT_RD          9
#define TFT_D0          39
#define TFT_D1          40
#define TFT_D2          41
#define TFT_D3          42
#define TFT_D4          45
#define TFT_D5          46
#define TFT_D6          47
#define TFT_D7          48
#define TFT_ROTATION    1       // 1=横屏 320x170

// 屏幕总线驱动选择（也可在 platformio.ini build_flags 覆盖）
//   1 = 硬件 I8080 (Arduino_ESP32LCD8, esp_lcd i80 + DMA, 更快)
//   0 = 软件并行   (Arduino_ESP32PAR8Q, LilyGO 官方示例同款, 兜底)
#ifndef USE_HW_I8080_BUS
#define USE_HW_I8080_BUS 1
#endif

// ---------- 按键（均低电平有效，内部上拉）----------
#define BTN_UP_PIN      0       // 板载 BOOT 键 = 上 / 长按返回
#define BTN_DOWN_PIN    14      // 板载 User 键 = 下
#define BTN_OK_PIN      1       // 外接轻触按键 = 确定 / 生成
#define BTN_DEBOUNCE_MS 20
#define BTN_LONGPRESS_MS 600

// ---------- I2S 录音 INMP441 (Stage 3) ----------
#define I2S_REC_BCLK    10
#define I2S_REC_WS      11
#define I2S_REC_DIN     12
#define REC_SAMPLE_RATE 16000   // 16kHz 单声道 16bit
#define REC_BITS        16
#define REC_CHANNELS    1
#define REC_DURATION_SEC 8      // 8s 录音

// ---------- I2S 播放 MAX98357A (Stage 5) ----------
#define I2S_PLAY_BCLK   13
#define I2S_PLAY_LRC    16
#define I2S_PLAY_DIN    21
#define PLAY_SAMPLE_RATE 44100

// ---------- 状态 LED (可选) ----------
#define STATUS_LED_PIN  2

// ---------- 录音缓冲（放 PSRAM）----------
// 8s × 16kHz × 2B = 256000 字节
#define REC_BUFFER_BYTES (REC_DURATION_SEC * REC_SAMPLE_RATE * (REC_BITS / 8))

// ---------- WiFi / 服务器 (Stage 4+) ----------
// 实际值用 Preferences 存 NVS；此处为编译期默认占位
#define DEFAULT_WIFI_SSID  "YOUR_SSID"
#define DEFAULT_WIFI_PASS  "YOUR_PASSWORD"
#define DEFAULT_BASE_URL   "http://192.168.1.100:5000"
#define API_GENERATE       "/api/generate/pipeline"
#define API_HISTORY        "/api/history"
#define API_HEALTH         "/api/pipeline/health"
#define HTTP_TIMEOUT_MS    180000   // 云端推理可能耗时数分钟
#define MAX_HISTORY_ITEMS  32
