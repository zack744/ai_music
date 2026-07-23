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

/* 临时/现场播放器自检；正式固件保持 0，需要时改成 1。 */
#define PLAYER_BOOT_SELF_TEST 0
#define PLAYER_BOOT_SELF_TEST_STREAM 0
#define PLAYER_BOOT_SELF_TEST_URL "http://192.168.50.246:5000/outputs/compare_funmusic_vocals.mp3"

/* 流式播放（已搁置，勿改默认）：产品路径只用整首下载 */
#define STREAM_BUFFER_SIZE        (64 * 1024)
#define STREAM_RECONNECT_TRIES    5
#define STREAM_RECONNECT_DELAY_MS 1000
#define PLAYER_DEFAULT_STREAM     0

/* 录音自检：开机自动录 3 秒并打印 PCM 统计 */
#define RECORDER_BOOT_SELF_TEST 0



