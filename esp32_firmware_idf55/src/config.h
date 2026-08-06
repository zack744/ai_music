#pragma once

/* =====================================================================
 * ai_music ESP32 硬件引脚定义
 * 板子: VIEWE UEDX24320028E-WB-A (ESP32-S3-N16R8)
 * 官方引脚表(gitee): LCD=40/41/42/45/39/13, 触摸=1/3, SD卡=15/16/17/18,
 *   USB=19/20, UART=43/44, RGB_LED=0, 蜂鸣器=38, IM0/IM1=47/48
 * 空闲可用: 2,4,5,6,7,8,9,10,11,12,14 (2/4 触摸预留,尽量不用)
 * ===================================================================== */

/* INMP441 I2S 麦克风 (I2S0, RX only) */
#define I2S_REC_BCLK      GPIO_NUM_10
#define I2S_REC_WS        GPIO_NUM_11
#define I2S_REC_DIN       GPIO_NUM_12
#define REC_SAMPLE_RATE   16000
#define REC_DURATION_SEC  30

/* MAX98357A I2S 功放 (I2S1, TX only) - 避开 SD 卡(15/16/17/18) 和背光(13) */
#define I2S_PLAY_BCLK     GPIO_NUM_14
#define I2S_PLAY_LRC      GPIO_NUM_5
#define I2S_PLAY_DIN      GPIO_NUM_6

/* 公网演示默认后端（NVS 为空时使用；配网页可覆盖） */
#ifndef AI_MUSIC_DEFAULT_HOST
#define AI_MUSIC_DEFAULT_HOST "192.168.1.100"
#endif
#ifndef AI_MUSIC_DEFAULT_PORT
#define AI_MUSIC_DEFAULT_PORT 5000
#endif
#ifndef AI_MUSIC_DEFAULT_TLS
#define AI_MUSIC_DEFAULT_TLS 0
#endif
/* 与云端 SITE_PASSWORD / API_ACCESS_KEY 一致；空=不发 X-API-Key */
#ifndef AI_MUSIC_DEFAULT_API_KEY
#define AI_MUSIC_DEFAULT_API_KEY ""
#endif

/* 临时/现场播放器自检；正式固件保持 0，需要时改成 1。 */
#define PLAYER_BOOT_SELF_TEST 0
#define PLAYER_BOOT_SELF_TEST_STREAM 0
#define PLAYER_BOOT_SELF_TEST_URL "http://192.168.1.100:5000/outputs/compare_funmusic_vocals.mp3"

/* 流式播放（已搁置，勿改默认）：产品路径只用整首下载 */
#define STREAM_BUFFER_SIZE        (64 * 1024)
#define STREAM_RECONNECT_TRIES    5
#define STREAM_RECONNECT_DELAY_MS 1000
#define PLAYER_DEFAULT_STREAM     0

/* 录音自检：开机自动录 3 秒并打印 PCM 统计 */
#define RECORDER_BOOT_SELF_TEST 0

/* ---- 触摸校准（CHSC6540 raw → 240x320 逻辑坐标）----
 * 默认 TOUCH_CALIB_LOG=0：行为与原先完全一致，只是常数集中到这里。
 * 校准步骤：
 *   1) 设 TOUCH_CALIB_LOG=1，烧录，串口 115200 看 [TouchCal]
 *   2) 尽量贴边点 左上/右上/左下/右下，记录 raw_x/raw_y
 *   3) 令 x_min=最左 raw, x_max=最右, y_min=最上, y_max=最下
 *      TOUCH_CAL_X0 = x_min
 *      TOUCH_CAL_XSPAN = x_max - x_min
 *      TOUCH_CAL_Y0 = y_min
 *      TOUCH_CAL_YSPAN = y_max - y_min
 *      TOUCH_CAL_Y_BIAS 先 0，若整体偏上/下再微调
 *   4) 改完常数后把 TOUCH_CALIB_LOG 改回 0 再烧正式固件
 */
#define TOUCH_CALIB_LOG   0
/* 2026-07-23 四角实测: TL(23,19) TR(224,17) BL(22,315) BR(223,316) */
#define TOUCH_CAL_X0      22
#define TOUCH_CAL_XSPAN   202
#define TOUCH_CAL_Y0      17
#define TOUCH_CAL_YSPAN   299
#define TOUCH_CAL_Y_BIAS  0



