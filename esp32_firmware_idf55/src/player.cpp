#include "player.h"
#include "config.h"
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <stdio.h>
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "AudioGeneratorMP3.h"
#include "AudioOutputI2S.h"
#include "AudioFileSource.h"
#include "AudioFileSourceHTTPStream.h"
#include "AudioFileSourceBuffer.h"

/* 从 PSRAM 内存缓冲读取的 AudioFileSource, 不依赖 NetworkClientSecure */
class AudioFileSourceMEM : public AudioFileSource
{
public:
    AudioFileSourceMEM(const uint8_t *data, uint32_t len) : _data(data), _len(len), _pos(0) {}
    virtual uint32_t read(void *data, uint32_t len) override
    {
        if (_pos >= _len) return 0;
        uint32_t avail = _len - _pos;
        if (len > avail) len = avail;
        memcpy(data, _data + _pos, len);
        _pos += len;
        return len;
    }
    virtual bool seek(int32_t pos, int dir) override
    {
        if (dir == SEEK_SET) _pos = pos;
        else if (dir == SEEK_CUR) _pos += pos;
        return _pos <= _len;
    }
    virtual bool close() override { return true; }
    virtual bool isOpen() override { return _pos < _len; }
    virtual uint32_t getSize() override { return _len; }
    virtual uint32_t getPos() override { return _pos; }
private:
    const uint8_t *_data;
    uint32_t _len, _pos;
};

static AudioGeneratorMP3 *s_mp3 = NULL;
static AudioFileSourceMEM *s_file = NULL;
static AudioOutputI2S *s_out = NULL;
static uint8_t *s_mp3_buf = NULL;
static size_t s_mp3_size = 0;
static bool s_inited = false;

static AudioFileSourceHTTPStream *s_stream_http = NULL;
static AudioFileSourceBuffer *s_stream_buf = NULL;
static uint8_t *s_stream_buf_mem = NULL;
static player_source_mode_t s_source_mode = PLAYER_SOURCE_MEMORY;

static volatile player_state_t s_state = PLAYER_IDLE;
static unsigned long s_start_ms = 0;      /* 播放开始时刻 */
static unsigned long s_pause_accum = 0;   /* 累计暂停时长 */
static unsigned long s_pause_begin = 0;   /* 本次暂停开始时刻 */
static int s_duration_ms = 0;             /* 估算总时长 */

static SemaphoreHandle_t s_mutex = NULL;

static void player_stop_stream_internals(void);

bool player_init(void)
{
    if (s_inited) return true;
    if (!s_mutex) s_mutex = xSemaphoreCreateMutex();
    s_out = new AudioOutputI2S(1, 0, 8);
    s_out->SetPinout(I2S_PLAY_BCLK, I2S_PLAY_LRC, I2S_PLAY_DIN);
    s_out->SetGain(0.6);
    s_mp3 = new AudioGeneratorMP3();
    s_inited = true;
    Serial.println("[play] MAX98357A I2S1 init OK");
    return true;
}

/* 扫描 MP3 首帧头, 估算时长(ms)。失败返回 0 */
static int estimate_duration_ms(const uint8_t *buf, size_t size)
{
    if (size < 4) {
        Serial.println("[play] buffer too small for duration estimate");
        return 0;
    }
    static const int br_v1l3[16] = {0,32,40,48,56,64,80,96,112,128,160,192,224,256,320,0};
    static const int br_v2l3[16] = {0,8,16,24,32,40,48,56,64,80,96,112,128,144,160,0};

    size_t scan = size < 8192 ? size - 4 : 8192;
    for (size_t i = 0; i < scan; i++) {
        if (buf[i] != 0xFF || (buf[i + 1] & 0xE0) != 0xE0) continue;
        int ver = (buf[i + 1] >> 3) & 0x3;    /* 3=MPEG1, 2=MPEG2, 0=MPEG2.5 */
        int layer = (buf[i + 1] >> 1) & 0x3;  /* 1=LayerIII */
        int br_idx = (buf[i + 2] >> 4) & 0xF;
        if (layer != 1 || br_idx == 0 || br_idx == 15) continue;
        int kbps = (ver == 3) ? br_v1l3[br_idx] : br_v2l3[br_idx];
        if (kbps <= 0) continue;
        int ms = (int)((int64_t)size * 8 * 1000 / ((int64_t)kbps * 1000));
        Serial.printf("[play] mp3 bitrate %d kbps -> duration ~%d ms\n", kbps, ms);
        return ms;
    }
    Serial.println("[play] no mp3 frame header found, duration unknown");
    return 0;
}

bool player_play(const char *url, player_download_cb_t dl_cb)
{
    if (!s_inited) player_init();
    player_stop();
    if (!url || !url[0]) return false;

    s_state = PLAYER_DOWNLOADING;
    Serial.printf("[play] downloading %s\n", url);
    WiFiClient client;
    HTTPClient http;
    http.setTimeout(30000);
    if (!http.begin(client, url)) {
        Serial.println("[play] http.begin failed");
        s_state = PLAYER_ERROR;
        return false;
    }
    int code = http.GET();
    if (code != 200) {
        Serial.printf("[play] HTTP %d\n", code);
        http.end();
        s_state = PLAYER_ERROR;
        return false;
    }

    int len = http.getSize();
    if (len <= 0) len = 5 * 1024 * 1024;
    Serial.printf("[play] allocating %d bytes in PSRAM\n", len);

    s_mp3_buf = (uint8_t *)heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
    if (!s_mp3_buf) {
        Serial.println("[play] PSRAM alloc failed");
        http.end();
        s_state = PLAYER_ERROR;
        return false;
    }

    WiFiClient *stream = http.getStreamPtr();
    s_mp3_size = 0;
    int last_pct = -1;
    unsigned long deadline = millis() + 60000;

    /*
     * 关键：不要让 lwIP/NetworkClient 直接把 TCP 数据写入 PSRAM。
     * ESP32-S3 OPI PSRAM 与 cache/flash 共用 MSPI；在 WiFi 收包或 flash
     * cache 临时关闭窗口中，直接 readBytes(psram_ptr, ...) 会触发
     * "Cache disabled but cached memory region accessed"。
     * 先读到内部 DMA-capable SRAM，再在普通 CPU 上下文复制到 PSRAM。
     */
    const size_t net_chunk = 4096;
    uint8_t *net_buf = (uint8_t *)heap_caps_malloc(
        net_chunk, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT | MALLOC_CAP_DMA);
    if (!net_buf) {
        Serial.println("[play] internal network buffer alloc failed");
        http.end();
        free(s_mp3_buf);
        s_mp3_buf = NULL;
        s_state = PLAYER_ERROR;
        return false;
    }

    while (http.connected() || stream->available()) {
        if (millis() > deadline || s_mp3_size >= (size_t)len) break;
        size_t avail = stream->available();
        if (avail) {
            if (avail > net_chunk) avail = net_chunk;
            if (s_mp3_size + avail > (size_t)len) avail = len - s_mp3_size;
            int rd = stream->readBytes(net_buf, avail);
            if (rd > 0) {
                memcpy(s_mp3_buf + s_mp3_size, net_buf, (size_t)rd);
                s_mp3_size += (size_t)rd;
                int pct = (int)(s_mp3_size * 100 / len);
                if (pct != last_pct) {
                    last_pct = pct;
                    if (dl_cb) dl_cb(pct);
                }
            }
        } else {
            delay(10);
        }
    }
    free(net_buf);
    http.end();
    Serial.printf("[play] downloaded %d bytes\n", (int)s_mp3_size);

    if (s_mp3_size == 0) {
        free(s_mp3_buf);
        s_mp3_buf = NULL;
        s_state = PLAYER_ERROR;
        return false;
    }

    s_duration_ms = estimate_duration_ms(s_mp3_buf, s_mp3_size);

    s_file = new AudioFileSourceMEM(s_mp3_buf, s_mp3_size);
    if (!s_mp3->begin(s_file, s_out)) {
        Serial.println("[play] mp3 begin failed");
        delete s_file;
        s_file = NULL;
        free(s_mp3_buf);
        s_mp3_buf = NULL;
        s_state = PLAYER_ERROR;
        return false;
    }
    s_start_ms = millis();
    s_pause_accum = 0;
    s_state = PLAYER_PLAYING;
    Serial.println("[play] playback started");
    return true;
}

bool player_play_stream(const char *url)
{
    if (!s_inited) player_init();
    player_stop();
    if (!url || !url[0]) return false;

    s_source_mode = PLAYER_SOURCE_STREAM;
    s_state = PLAYER_CONNECTING;
    Serial.printf("[play] stream: connecting %s\n", url);

    s_stream_http = new AudioFileSourceHTTPStream(url);
    if (!s_stream_http->isOpen()) {
        Serial.println("[play] stream: HTTP open failed");
        delete s_stream_http;
        s_stream_http = NULL;
        s_state = PLAYER_ERROR;
        return false;
    }
    s_stream_http->SetReconnect(STREAM_RECONNECT_TRIES, STREAM_RECONNECT_DELAY_MS);
    s_stream_http->useHTTP10();

    s_stream_buf_mem = (uint8_t *)heap_caps_malloc(
        STREAM_BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_stream_buf_mem) {
        Serial.println("[play] stream: PSRAM buffer alloc failed");
        delete s_stream_http;
        s_stream_http = NULL;
        s_state = PLAYER_ERROR;
        return false;
    }

    s_stream_buf = new AudioFileSourceBuffer(s_stream_http, s_stream_buf_mem, STREAM_BUFFER_SIZE);

    int http_size = (int)s_stream_http->getSize();
    if (http_size > 0) {
        s_duration_ms = (int)((int64_t)http_size * 8 * 1000 / ((int64_t)128 * 1000));
        Serial.printf("[play] stream: size=%d bytes, est duration ~%d ms (128kbps)\n",
                      http_size, s_duration_ms);
    } else {
        s_duration_ms = 0;
        Serial.println("[play] stream: unknown content length");
    }

    s_state = PLAYER_BUFFERING;
    Serial.printf("[play] stream: pre-buffering (%dKB)...\n", STREAM_BUFFER_SIZE / 1024);
    for (int i = 0; i < 50; i++) {
        s_stream_buf->loop();
        uint32_t fill = s_stream_buf->getFillLevel();
        if (fill >= STREAM_BUFFER_SIZE / 4) {
            Serial.printf("[play] stream: buffer reached %u bytes, starting decoder\n", fill);
            break;
        }
        delay(10);
    }

    if (!s_mp3->begin(s_stream_buf, s_out)) {
        Serial.println("[play] stream: mp3 begin failed");
        player_stop_stream_internals();
        s_state = PLAYER_ERROR;
        return false;
    }

    s_start_ms = millis();
    s_pause_accum = 0;
    s_state = PLAYER_PLAYING;
    Serial.println("[play] stream: playback started");
    return true;
}

static void player_stop_stream_internals(void)
{
    if (s_stream_buf) { delete s_stream_buf; s_stream_buf = NULL; }
    if (s_stream_http) { delete s_stream_http; s_stream_http = NULL; }
    if (s_stream_buf_mem) { free(s_stream_buf_mem); s_stream_buf_mem = NULL; }
}

void player_stop(void)
{
    if (s_mutex) xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (s_mp3 && s_mp3->isRunning()) s_mp3->stop();
    if (s_file) { delete s_file; s_file = NULL; }
    if (s_mp3_buf) { free(s_mp3_buf); s_mp3_buf = NULL; s_mp3_size = 0; }
    player_stop_stream_internals();
    s_state = PLAYER_IDLE;
    if (s_mutex) xSemaphoreGive(s_mutex);
}

void player_pause(void)
{
    if (s_state == PLAYER_PLAYING) {
        s_pause_begin = millis();
        s_state = PLAYER_PAUSED;
        Serial.println("[play] paused");
    }
}

void player_resume(void)
{
    if (s_state == PLAYER_PAUSED) {
        s_pause_accum += millis() - s_pause_begin;
        s_state = PLAYER_PLAYING;
        Serial.println("[play] resumed");
    }
}

player_state_t player_state(void)
{
    return s_state;
}

int player_elapsed_ms(void)
{
    if (s_state == PLAYER_FINISHED) return s_duration_ms;
    if (s_state != PLAYER_PLAYING && s_state != PLAYER_PAUSED) return 0;
    unsigned long now = (s_state == PLAYER_PAUSED) ? s_pause_begin : millis();
    return (int)(now - s_start_ms - s_pause_accum);
}

int player_duration_ms(void)
{
    return s_duration_ms;
}

player_source_mode_t player_source_mode(void)
{
    return s_source_mode;
}

int player_buffer_fill_pct(void)
{
    if (s_source_mode != PLAYER_SOURCE_STREAM || !s_stream_buf) return 0;
    uint32_t fill = s_stream_buf->getFillLevel();
    return (int)(fill * 100 / STREAM_BUFFER_SIZE);
}

void player_loop(void)
{
    if (!s_mutex || !xSemaphoreTake(s_mutex, 0)) return;
    if (s_source_mode == PLAYER_SOURCE_STREAM) {
        if (s_state == PLAYER_PLAYING && s_mp3 && s_mp3->isRunning()) {
            if (s_stream_buf) s_stream_buf->loop();
            if (!s_mp3->loop()) {
                s_mp3->stop();
                s_state = PLAYER_FINISHED;
                Serial.println("[play] stream: finished");
            }
        }
    } else {
        if (s_state == PLAYER_PLAYING && s_mp3 && s_mp3->isRunning()) {
            if (!s_mp3->loop()) {
                s_mp3->stop();
                s_state = PLAYER_FINISHED;
                Serial.println("[play] finished");
            }
        }
    }
    xSemaphoreGive(s_mutex);
}
