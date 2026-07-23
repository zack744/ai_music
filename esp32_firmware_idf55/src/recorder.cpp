#include "recorder.h"
#include "config.h"
#include <string.h>
#include <stdlib.h>
#include "driver/i2s.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

static const char *TAG = "rec";
static bool s_i2s_installed = false;
static volatile bool s_stop_req = false;

void recorder_stop(void)
{
    s_stop_req = true;
}

/* 写 44 字节 WAV 头 */
static void make_wav_header(uint8_t *hdr, uint32_t data_size, uint32_t sample_rate, uint16_t channels, uint16_t bits)
{
    uint32_t byte_rate = sample_rate * channels * bits / 8;
    uint16_t block_align = channels * bits / 8;
    memcpy(hdr, "RIFF", 4);
    *(uint32_t *)(hdr + 4) = data_size + 36;
    memcpy(hdr + 8, "WAVE", 4);
    memcpy(hdr + 12, "fmt ", 4);
    *(uint32_t *)(hdr + 16) = 16;
    *(uint16_t *)(hdr + 20) = 1;            /* PCM */
    *(uint16_t *)(hdr + 22) = channels;
    *(uint32_t *)(hdr + 24) = sample_rate;
    *(uint32_t *)(hdr + 28) = byte_rate;
    *(uint16_t *)(hdr + 32) = block_align;
    *(uint16_t *)(hdr + 34) = bits;
    memcpy(hdr + 36, "data", 4);
    *(uint32_t *)(hdr + 40) = data_size;
}

bool recorder_init(void)
{
    if (s_i2s_installed) return true;

    /* 用旧(legacy) I2S API, 与 ESP8266Audio 一致 — 新驱动(driver/i2s_std.h)
     * 与旧驱动不能共存, ESP-IDF 5.x 检测到会直接 abort */
    i2s_config_t cfg = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = REC_SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,   /* INMP441 每槽 32bit, 内含 24bit 数据 */
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 4,
        .dma_buf_len = 256,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0,
    };
    i2s_pin_config_t pins = {
        .mck_io_num = -1,
        .bck_io_num = I2S_REC_BCLK,
        .ws_io_num = I2S_REC_WS,
        .data_out_num = -1,
        .data_in_num = I2S_REC_DIN,
    };

    esp_err_t err = i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_driver_install failed: %s", esp_err_to_name(err));
        return false;
    }

    err = i2s_set_pin(I2S_NUM_0, &pins);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_set_pin failed: %s", esp_err_to_name(err));
        i2s_driver_uninstall(I2S_NUM_0);
        return false;
    }

    err = i2s_start(I2S_NUM_0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_start failed: %s", esp_err_to_name(err));
        i2s_driver_uninstall(I2S_NUM_0);
        return false;
    }

    s_i2s_installed = true;
    ESP_LOGI(TAG, "INMP441 I2S init OK (legacy API, I2S0, rate=%d, mono, 16bit)", REC_SAMPLE_RATE);
    return true;
}

uint8_t *recorder_record(int duration_sec, size_t *out_size, recorder_progress_cb_t progress_cb)
{
    if (!s_i2s_installed) {
        ESP_LOGE(TAG, "not initialized");
        return NULL;
    }

    size_t total_samples = (size_t)REC_SAMPLE_RATE * duration_sec;   /* 16-bit mono -> 2 bytes/sample */
    size_t data_size = total_samples * 2;
    size_t wav_size = data_size + 44;

    /* 分配 PSRAM 缓冲区 */
    uint8_t *wav_buf = (uint8_t *)heap_caps_calloc(1, wav_size, MALLOC_CAP_SPIRAM);
    if (!wav_buf) {
        ESP_LOGE(TAG, "PSRAM alloc %d failed", (int)wav_size);
        return NULL;
    }

    make_wav_header(wav_buf, data_size, REC_SAMPLE_RATE, 1, 16);
    uint8_t *pcm = wav_buf + 44;

    /* 丢弃前几帧(INMP441 启动噪声) */
    uint8_t flush[1024];
    size_t flushed;
    for (int i = 0; i < 3; i++)
        i2s_read(I2S_NUM_0, flush, sizeof(flush), &flushed, 100 / portTICK_PERIOD_MS);

    ESP_LOGI(TAG, "recording up to %ds (%d bytes)...", duration_sec, (int)data_size);

    s_stop_req = false;

    /* 32-bit slot 缓冲读取, 再右移取高 16-bit */
    size_t chunk_samples = 512;
    size_t chunk_bytes = chunk_samples * 4;   /* 32-bit per slot */
    int32_t *i32buf = (int32_t *)heap_caps_malloc(chunk_bytes, MALLOC_CAP_SPIRAM);
    if (!i32buf) {
        ESP_LOGE(TAG, "chunk buf alloc failed");
        free(wav_buf);
        return NULL;
    }

    size_t recorded = 0;
    int last_sec = -1;
    while (recorded < total_samples && !s_stop_req) {
        size_t want = (total_samples - recorded < chunk_samples) ? (total_samples - recorded) : chunk_samples;
        size_t got_bytes = 0;
        esp_err_t err = i2s_read(I2S_NUM_0, i32buf, want * 4, &got_bytes, 200 / portTICK_PERIOD_MS);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "i2s_read err: %s", esp_err_to_name(err));
            break;
        }
        size_t got_samples = got_bytes / 4;
        for (size_t i = 0; i < got_samples && recorded < total_samples; i++) {
            /* 取高 16 位 (INMP441 24-bit data 左对齐在 32-bit slot) */
            int16_t s16 = (int16_t)(i32buf[i] >> 14);
            ((int16_t *)pcm)[recorded] = s16;
            recorded++;
        }
        int cur_sec = recorded / REC_SAMPLE_RATE;
        if (cur_sec != last_sec) {
            last_sec = cur_sec;
            if (progress_cb) progress_cb(cur_sec);
        }
    }

    free(i32buf);
    *out_size = recorded * 2 + 44;
    /* 修正 WAV 头里的实际 data size */
    *(uint32_t *)(wav_buf + 4) = recorded * 2 + 36;
    *(uint32_t *)(wav_buf + 40) = recorded * 2;

    ESP_LOGI(TAG, "recorded %d samples (%d bytes)%s", (int)recorded, (int)*out_size,
             s_stop_req ? " [stopped by user]" : "");
    return wav_buf;
}
