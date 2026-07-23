#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 上传两段 WAV 到后端 /api/generate/pipeline
 * env_wav / env_size      : 环境音 WAV (必填)
 * speech_wav / speech_size: 语音 WAV (可为 NULL, 此时用 user_text)
 * user_text               : 文字输入 (可为 NULL, 此时用 speech_wav)
 * duration_sec            : 期望音乐时长
 * out_url                 : 接收 output_url 的缓冲区 (至少 256 字节)
 * 返回 true=成功, false=失败 */
bool uploader_upload(const uint8_t *env_wav, size_t env_size,
                     const uint8_t *speech_wav, size_t speech_size,
                     const char *user_text, int duration_sec,
                     char *out_url, size_t out_url_size);

#ifdef __cplusplus
}
#endif
