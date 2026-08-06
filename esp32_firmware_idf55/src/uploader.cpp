#include "uploader.h"
#include "net_helper.h"
#include <WiFi.h>
#include <Client.h>
#include <ArduinoJson.h>
#include <stdio.h>

static const char *TAG = "upload";

/* 等生成完成：Fun-Music 可达 300s + 公网余量 */
static const unsigned long UPLOAD_RESPONSE_TIMEOUT_MS = 360000UL;

/* 计算 multipart body 总大小 */
static size_t calc_multipart_size(const char *boundary,
                                  size_t env_size, size_t speech_size, const char *user_text, int duration_sec)
{
    size_t bl = strlen(boundary);
    size_t total = 0;

    char hdr[256];

    /* env_audio part */
    int n = snprintf(hdr, sizeof(hdr),
        "--%s\r\nContent-Disposition: form-data; name=\"env_audio\"; filename=\"env.wav\"\r\nContent-Type: audio/wav\r\n\r\n",
        boundary);
    total += n + env_size + 2;

    /* speech_audio part (if present) */
    if (speech_size > 0) {
        n = snprintf(hdr, sizeof(hdr),
            "--%s\r\nContent-Disposition: form-data; name=\"speech_audio\"; filename=\"speech.wav\"\r\nContent-Type: audio/wav\r\n\r\n",
            boundary);
        total += n + speech_size + 2;
    }

    /* user_text part (if present) */
    if (user_text && user_text[0]) {
        n = snprintf(hdr, sizeof(hdr),
            "--%s\r\nContent-Disposition: form-data; name=\"user_text\"\r\n\r\n",
            boundary);
        total += n + strlen(user_text) + 2;
    }

    /* duration_sec part */
    char dbuf[8];
    snprintf(dbuf, sizeof(dbuf), "%d", duration_sec);
    n = snprintf(hdr, sizeof(hdr),
        "--%s\r\nContent-Disposition: form-data; name=\"duration_sec\"\r\n\r\n",
        boundary);
    total += n + strlen(dbuf) + 2;

    /* source part */
    n = snprintf(hdr, sizeof(hdr),
        "--%s\r\nContent-Disposition: form-data; name=\"source\"\r\n\r\n",
        boundary);
    total += n + 5 + 2;

    /* closing */
    total += bl + 6;
    return total;
}

bool uploader_upload(const uint8_t *env_wav, size_t env_size,
                     const uint8_t *speech_wav, size_t speech_size,
                     const char *user_text, int duration_sec,
                     char *out_url, size_t out_url_size,
                     char *out_title, size_t out_title_size)
{
    const char *host = network_server_host();
    if (!host || !host[0]) {
        Serial.printf("[%s] no server host\n", TAG);
        return false;
    }
    if (!env_wav || env_size == 0) {
        Serial.printf("[%s] no env audio\n", TAG);
        return false;
    }

    const char *boundary = "----ESP32AiMusicBoundary";
    size_t body_size = calc_multipart_size(boundary, env_size, speech_size, user_text, duration_sec);

    char endpoint[96];
    network_endpoint_summary(endpoint, sizeof(endpoint));
    Serial.printf("[%s] connecting %s, body=%d bytes, wait<=%lus\n",
                  TAG, endpoint, (int)body_size, UPLOAD_RESPONSE_TIMEOUT_MS / 1000UL);

    Client *client = network_connect_backend(60000);
    if (!client) {
        Serial.printf("[%s] connect failed\n", TAG);
        return false;
    }

    /* 发 HTTP 头 */
    client->print("POST /api/generate/pipeline HTTP/1.1\r\n");
    network_write_host_header(client);
    network_write_auth_header(client);
    client->printf("Content-Type: multipart/form-data; boundary=%s\r\n", boundary);
    client->printf("Content-Length: %d\r\n", (int)body_size);
    client->print("Connection: close\r\n\r\n");

    /* env_audio */
    client->printf("--%s\r\n", boundary);
    client->print("Content-Disposition: form-data; name=\"env_audio\"; filename=\"env.wav\"\r\n");
    client->print("Content-Type: audio/wav\r\n\r\n");
    client->write(env_wav, env_size);
    client->print("\r\n");

    /* speech_audio (optional) */
    if (speech_wav && speech_size > 0) {
        client->printf("--%s\r\n", boundary);
        client->print("Content-Disposition: form-data; name=\"speech_audio\"; filename=\"speech.wav\"\r\n");
        client->print("Content-Type: audio/wav\r\n\r\n");
        client->write(speech_wav, speech_size);
        client->print("\r\n");
    }

    /* user_text (optional) */
    if (user_text && user_text[0]) {
        client->printf("--%s\r\n", boundary);
        client->print("Content-Disposition: form-data; name=\"user_text\"\r\n\r\n");
        client->print(user_text);
        client->print("\r\n");
    }

    /* duration_sec */
    client->printf("--%s\r\n", boundary);
    client->print("Content-Disposition: form-data; name=\"duration_sec\"\r\n\r\n");
    client->printf("%d\r\n", duration_sec);

    /* source */
    client->printf("--%s\r\n", boundary);
    client->print("Content-Disposition: form-data; name=\"source\"\r\n\r\n");
    client->print("esp32\r\n");

    /* closing */
    client->printf("--%s--\r\n", boundary);

    Serial.printf("[%s] request sent, waiting response...\n", TAG);

    /* 读响应 */
    String response;
    const unsigned long started_at = millis();
    while (client->connected() || client->available()) {
        if ((unsigned long)(millis() - started_at) >= UPLOAD_RESPONSE_TIMEOUT_MS) {
            Serial.printf("[%s] response timeout\n", TAG);
            client->stop();
            delete client;
            return false;
        }
        while (client->available()) {
            response += (char)client->read();
        }
        if (!client->available() && client->connected()) {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
    client->stop();
    delete client;

    /* 分离 header / body */
    int sep = response.indexOf("\r\n\r\n");
    if (sep < 0) {
        Serial.printf("[%s] no header/body separator\n", TAG);
        return false;
    }
    String body = response.substring(sep + 4);

    /* 解析 JSON 取 output_url */
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
        Serial.printf("[%s] JSON parse failed: %s\n", TAG, err.c_str());
        Serial.printf("[%s] body: %.200s\n", TAG, body.c_str());
        return false;
    }

    const char *url = doc["output_url"];
    if (!url || !url[0]) {
        const char *error = doc["error"];
        Serial.printf("[%s] no output_url, error: %s\n", TAG, error ? error : "(null)");
        return false;
    }

    if (!network_resolve_url(out_url, out_url_size, url)) {
        strncpy(out_url, url, out_url_size - 1);
        out_url[out_url_size - 1] = 0;
    }

    if (out_title && out_title_size > 0) {
        out_title[0] = 0;
        const char *title = doc["song_title"];
        if (title && title[0]) {
            size_t j = 0;
            for (size_t i = 0; title[i] && j + 1 < out_title_size; i++) {
                char c = title[i];
                if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == ' ') {
                    out_title[j++] = c;
                }
            }
            while (j > 0 && out_title[j - 1] == ' ') j--;
            out_title[j] = 0;
        }
        if (!out_title[0]) {
            strncpy(out_title, "New Song", out_title_size - 1);
            out_title[out_title_size - 1] = 0;
        }
    }

    Serial.printf("[%s] success, output_url=%s title=%s\n", TAG, out_url,
                  (out_title && out_title[0]) ? out_title : "(none)");
    return true;
}
