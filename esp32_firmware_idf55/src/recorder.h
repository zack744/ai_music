#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化 I2S 麦克风 (INMP441, I2S0) */
bool recorder_init(void);

/* 请求停止当前录音（异步，recorder_record 会在下次循环检测到并返回） */
void recorder_stop(void);

/* 录音最多 duration_sec 秒，可被 recorder_stop() 提前停止。
 * *out_size 收到 WAV 文件总大小(含 44 字节头)。
 * progress_cb: 每读一批数据回调一次, 参数为已录秒数, 可为 NULL。
 * 返回 NULL = 失败。 */
typedef void (*recorder_progress_cb_t)(int seconds_done);
uint8_t *recorder_record(int duration_sec, size_t *out_size, recorder_progress_cb_t progress_cb);

#ifdef __cplusplus
}
#endif
