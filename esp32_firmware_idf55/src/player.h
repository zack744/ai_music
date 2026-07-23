#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PLAYER_IDLE = 0,       /* 空闲 */
    PLAYER_DOWNLOADING,    /* 正在下载 mp3 到 PSRAM */
    PLAYER_CONNECTING,     /* 流式: 正在连接服务器 */
    PLAYER_BUFFERING,      /* 流式: 正在缓冲 */
    PLAYER_PLAYING,        /* 播放中 */
    PLAYER_RECONNECTING,   /* 流式: 网络恢复中 */
    PLAYER_PAUSED,         /* 已暂停 */
    PLAYER_FINISHED,       /* 播放完毕 */
    PLAYER_ERROR           /* 下载/解码失败 */
} player_state_t;

typedef enum {
    PLAYER_SOURCE_MEMORY = 0,
    PLAYER_SOURCE_STREAM
} player_source_mode_t;

/* 下载进度回调, 参数 0-100 */
typedef void (*player_download_cb_t)(int percent);

/* 初始化 I2S 输出 (MAX98357A, I2S1) */
bool player_init(void);

/* 下载 mp3 到 PSRAM 并开始播放(阻塞, 建议放后台任务调用)。
 * dl_cb: 下载进度回调, 可为 NULL。返回 true=已开始播放。 */
bool player_play(const char *url, player_download_cb_t dl_cb);

/* 流式播放: 不下载整首, 使用 HTTP 流 + PSRAM 环形缓冲。
 * 返回 true=已开始播放。 */
bool player_play_stream(const char *url);

/* 停止播放并释放缓冲 */
void player_stop(void);

/* 暂停 / 恢复 (暂停时解码挂起, 恢复后继续, 不重新下载) */
void player_pause(void);
void player_resume(void);

/* 当前状态 */
player_state_t player_state(void);

/* 当前播放来源模式 */
player_source_mode_t player_source_mode(void);

/* 流式缓冲填充百分比 (0-100), 非流式返回 0 */
int player_buffer_fill_pct(void);

/* 已播放毫秒数(墙钟, 扣除暂停时间) */
int player_elapsed_ms(void);

/* 估算总时长毫秒数(从 MP3 首帧码率推算), 未知返回 0 */
int player_duration_ms(void);

/* 在 loop() 中调用, 喂解码器 */
void player_loop(void);

#ifdef __cplusplus
}
#endif
