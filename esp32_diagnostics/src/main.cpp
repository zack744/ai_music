#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "esp_heap_caps.h"
#include "esp_psram.h"
#include "sdkconfig.h"

static constexpr const char *TEST_URL =
    "http://192.168.50.246:5000/outputs/compare_funmusic_vocals.mp3";
static constexpr size_t PSRAM_TEST_BYTES = 2 * 1024 * 1024;
static constexpr size_t NET_CHUNK = 4096;

static void banner(const char *name) {
    Serial.printf("\n\n========== %s ==========\n", name);
}

static bool psram_pattern_test(size_t bytes, int rounds, bool yield_for_wifi) {
    uint32_t *buf = static_cast<uint32_t *>(
        heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!buf) {
        Serial.printf("[PSRAM] alloc %u failed\n", static_cast<unsigned>(bytes));
        return false;
    }

    Serial.printf("[PSRAM] buffer=%p bytes=%u rounds=%d\n", buf,
                  static_cast<unsigned>(bytes), rounds);
    const size_t words = bytes / sizeof(uint32_t);

    for (int round = 0; round < rounds; ++round) {
        const uint32_t salt = 0x9E3779B9u * static_cast<uint32_t>(round + 1);
        for (size_t i = 0; i < words; ++i) {
            buf[i] = static_cast<uint32_t>(i) ^ salt ^ 0xA5A55A5Au;
            if (yield_for_wifi && (i & 0x3fff) == 0) delay(1);
        }

        size_t errors = 0;
        for (size_t i = 0; i < words; ++i) {
            const uint32_t expected = static_cast<uint32_t>(i) ^ salt ^ 0xA5A55A5Au;
            if (buf[i] != expected) {
                if (errors < 4) {
                    Serial.printf("[PSRAM] mismatch round=%d word=%u got=%08lx exp=%08lx\n",
                                  round, static_cast<unsigned>(i),
                                  static_cast<unsigned long>(buf[i]),
                                  static_cast<unsigned long>(expected));
                }
                ++errors;
            }
            if (yield_for_wifi && (i & 0x3fff) == 0) delay(1);
        }
        Serial.printf("[PSRAM] round %d/%d errors=%u free=%u\n", round + 1, rounds,
                      static_cast<unsigned>(errors),
                      static_cast<unsigned>(ESP.getFreePsram()));
        if (errors) {
            free(buf);
            return false;
        }
    }
    free(buf);
    return true;
}

static bool connect_saved_wifi() {
    banner("STAGE B0: CONNECT SAVED WIFI");
    WiFi.mode(WIFI_STA);
    WiFi.begin();
    const uint32_t started = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - started < 25000) {
        Serial.print('.');
        delay(250);
    }
    Serial.println();
    if (WiFi.status() != WL_CONNECTED) {
        Serial.printf("[WIFI] connect failed, status=%d\n", static_cast<int>(WiFi.status()));
        return false;
    }
    Serial.printf("[WIFI] connected ssid=%s ip=%s rssi=%d\n",
                  WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI());
    return true;
}

static bool http_download(bool copy_to_psram) {
    banner(copy_to_psram ? "STAGE C2: HTTP -> SRAM -> PSRAM" :
                           "STAGE C1: HTTP -> INTERNAL SRAM SINK");
    WiFiClient client;
    HTTPClient http;
    http.setConnectTimeout(10000);
    http.setTimeout(30000);
    if (!http.begin(client, TEST_URL)) {
        Serial.println("[HTTP] begin failed");
        return false;
    }
    const int code = http.GET();
    if (code != HTTP_CODE_OK) {
        Serial.printf("[HTTP] GET code=%d\n", code);
        http.end();
        return false;
    }

    int expected = http.getSize();
    if (expected <= 0) expected = 1024 * 1024;
    Serial.printf("[HTTP] content-length=%d\n", expected);

    uint8_t *net = static_cast<uint8_t *>(
        heap_caps_malloc(NET_CHUNK, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT | MALLOC_CAP_DMA));
    uint8_t *dst = nullptr;
    if (copy_to_psram) {
        dst = static_cast<uint8_t *>(
            heap_caps_malloc(expected, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    }
    if (!net || (copy_to_psram && !dst)) {
        Serial.printf("[HTTP] allocation failed net=%p dst=%p\n", net, dst);
        free(net); free(dst); http.end();
        return false;
    }

    WiFiClient *stream = http.getStreamPtr();
    size_t total = 0;
    uint32_t last_report = 0;
    const uint32_t deadline = millis() + 60000;
    while ((http.connected() || stream->available()) && millis() < deadline && total < static_cast<size_t>(expected)) {
        size_t avail = stream->available();
        if (!avail) { delay(1); continue; }
        if (avail > NET_CHUNK) avail = NET_CHUNK;
        if (total + avail > static_cast<size_t>(expected)) avail = expected - total;
        const int got = stream->readBytes(net, avail);
        if (got <= 0) continue;
        if (dst) memcpy(dst + total, net, static_cast<size_t>(got));
        total += static_cast<size_t>(got);
        if (total - last_report >= 64 * 1024 || total == static_cast<size_t>(expected)) {
            last_report = total;
            Serial.printf("[HTTP] received=%u/%d free-int=%u free-psram=%u\n",
                          static_cast<unsigned>(total), expected,
                          static_cast<unsigned>(ESP.getFreeHeap()),
                          static_cast<unsigned>(ESP.getFreePsram()));
        }
    }
    http.end();

    bool ok = total == static_cast<size_t>(expected);
    if (dst && ok) {
        uint32_t checksum = 2166136261u;
        for (size_t i = 0; i < total; ++i) checksum = (checksum ^ dst[i]) * 16777619u;
        Serial.printf("[HTTP] PSRAM checksum=%08lx\n", static_cast<unsigned long>(checksum));
    }
    Serial.printf("[HTTP] result=%s bytes=%u\n", ok ? "PASS" : "FAIL",
                  static_cast<unsigned>(total));
    free(net); free(dst);
    return ok;
}

void setup() {
    Serial.begin(115200);
    delay(1500);
    banner("ESP32-S3 N16R8 MINIMAL DIAGNOSTICS");
    Serial.printf("[CFG] IDF=%s CPU=%uMHz PSRAM-found=%d size=%u free=%u\n",
                  ESP.getSdkVersion(), getCpuFrequencyMhz(), psramFound(),
                  static_cast<unsigned>(ESP.getPsramSize()),
                  static_cast<unsigned>(ESP.getFreePsram()));
    Serial.printf("[CFG] dcache-size=%d line=%d\n",
                  CONFIG_ESP32S3_DATA_CACHE_SIZE,
                  CONFIG_ESP32S3_DATA_CACHE_LINE_SIZE);
#if CONFIG_SPIRAM_MODE_OCT
    Serial.println("[CFG] SPIRAM mode=OCTAL");
#else
    Serial.println("[CFG] SPIRAM mode=NOT OCTAL");
#endif
#ifdef CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP
    Serial.println("[CFG] WiFi/LWIP prefer PSRAM=YES");
#else
    Serial.println("[CFG] WiFi/LWIP prefer PSRAM=NO");
#endif

    banner("STAGE A: PSRAM ONLY");
    bool a = psram_pattern_test(PSRAM_TEST_BYTES, 8, false);
    Serial.printf("[RESULT] A PSRAM-only=%s\n", a ? "PASS" : "FAIL");
    if (!a) return;

    bool wifi = connect_saved_wifi();
    Serial.printf("[RESULT] B0 WiFi=%s\n", wifi ? "PASS" : "FAIL");
    if (!wifi) return;

    banner("STAGE B1: PSRAM WHILE WIFI CONNECTED");
    bool b = psram_pattern_test(PSRAM_TEST_BYTES, 12, true);
    Serial.printf("[RESULT] B1 PSRAM+WiFi=%s\n", b ? "PASS" : "FAIL");
    if (!b) return;

    bool c1 = http_download(false);
    Serial.printf("[RESULT] C1 HTTP-sink=%s\n", c1 ? "PASS" : "FAIL");
    if (!c1) return;

    bool c2 = http_download(true);
    Serial.printf("[RESULT] C2 HTTP-to-PSRAM=%s\n", c2 ? "PASS" : "FAIL");

    banner("DIAGNOSTICS COMPLETE");
    Serial.printf("[FINAL] A=%d B0=%d B1=%d C1=%d C2=%d\n", a, wifi, b, c1, c2);
}

void loop() {
    static uint32_t last = 0;
    if (millis() - last >= 5000) {
        last = millis();
        Serial.printf("[ALIVE] ms=%lu wifi=%d free-int=%u free-psram=%u\n",
                      static_cast<unsigned long>(millis()),
                      static_cast<int>(WiFi.status()),
                      static_cast<unsigned>(ESP.getFreeHeap()),
                      static_cast<unsigned>(ESP.getFreePsram()));
    }
    delay(10);
}
