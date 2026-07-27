/*
 * ai_music ESP32 固件入口 (VIEWE UEDX24320028E-WB-A, ESP32-S3-N16R8)
 * Stage2: 五页触摸 UI (主菜单/录音/生成中/播放/历史) + 中文字库
 * Stage2.5: WiFi 配网 + 屏幕显示连接状态 + USB CDC 日志
 */

#include <Arduino.h>
#include <WiFi.h>
#include <esp_display_panel.hpp>
#include <lvgl.h>
#include <esp_log.h>
#include <stdarg.h>
#include "lvgl_v8_port.h"
#include "ui.h"
#include "net_helper.h"
#include "player.h"
#include "recorder.h"
#include "config.h"

using namespace esp_panel::drivers;
using namespace esp_panel::board;

/* ---- ESP_LOG 重定向到 USB Serial ---- */
static bool s_log_ready = false;
static bool s_lvgl_ready = false;
static int log_to_serial(const char *fmt, va_list args)
{
    if (!s_log_ready) return 0;
    char buf[512];
    int ret = vsnprintf(buf, sizeof(buf), fmt, args);
    buf[sizeof(buf) - 1] = 0;
    Serial.print(buf);
    return ret;
}

/* ---- WiFi 状态回调:更新屏幕 ---- */
static void wifi_status_cb(const char *msg)
{
    // WiFiManager 运行期间不触碰 LVGL/SPI DMA；此阶段只输出日志。
    Serial.printf("[net] %s\n", msg ? msg : "(null)");
    if (s_lvgl_ready) {
        lvgl_port_lock(-1);
        ui_show_boot_msg(msg ? msg : "WiFi...");
        lvgl_port_unlock();
    }
}

void setup()
{
    Serial.begin(115200);
    delay(300);
    s_log_ready = true;
    esp_log_set_vprintf(log_to_serial);

    Serial.println("\n\n======== ai_music boot ========");

    pinMode(0, OUTPUT);
    digitalWrite(0, LOW);

    // 1. 先完成 WiFi/NVS/认证，期间不启动 LVGL/SPI DMA。
    //    这样可避免 WiFiManager 的 Flash/PSRAM 操作与 LCD 刷新并发。
    Serial.println("[boot] init WiFi (before LVGL)");
    bool wifi_ok = network_init(wifi_status_cb);

    // 2. WiFi 完成后再初始化屏幕和 LVGL。
    Serial.println("[boot] init board");
    Board *board = new Board();
    board->init();
    assert(board->begin());

    Serial.println("[boot] init LVGL");
    lvgl_port_init(board->getLCD(), board->getTouch());
    s_lvgl_ready = true;

    lvgl_port_lock(-1);
    if (wifi_ok && WiFi.status() == WL_CONNECTED) {
        char ep[96];
        network_endpoint_summary(ep, sizeof(ep));
        char msg[200];
        if (network_server_host()[0]) {
            snprintf(msg, sizeof(msg), "WiFi OK\nIP:%s\n%s",
                     WiFi.localIP().toString().c_str(), ep);
        } else {
            snprintf(msg, sizeof(msg), "WiFi OK\nIP:%s\nServer: not set\nTap Settings(gear)",
                     WiFi.localIP().toString().c_str());
        }
        Serial.printf("[boot] %s\n", msg);
        ui_show_boot_msg(msg);
    } else {
        Serial.printf("[boot] WiFi NOT connected\n");
        ui_show_boot_msg("WiFi NOT connected\nReboot to retry");
    }
    lvgl_port_unlock();
    delay(1000);

    // 3. 创建 UI；此时不再有 WiFiManager 阻塞操作。
    Serial.println("[boot] create UI");
    lvgl_port_lock(-1);
    ui_create();
    lvgl_port_unlock();

    // 4. 播放器使用旧 I2S API，与录音模块保持同一驱动族。
    Serial.println("[boot] init player");
    player_init();

#if PLAYER_BOOT_SELF_TEST
    ESP_LOGI("SelfTest", "starting player self-test: %s", PLAYER_BOOT_SELF_TEST_URL);
#if PLAYER_BOOT_SELF_TEST_STREAM
    bool self_test_ok = player_play_stream(PLAYER_BOOT_SELF_TEST_URL);
#else
    bool self_test_ok = player_play(PLAYER_BOOT_SELF_TEST_URL, NULL);
#endif
    ESP_LOGI("SelfTest", "player_play result=%s", self_test_ok ? "OK" : "FAIL");
#endif

#if RECORDER_BOOT_SELF_TEST
    {
        ESP_LOGI("SelfTest", "starting recorder self-test (3s)...");
        recorder_init();
        size_t wav_size = 0;
        uint8_t *wav = recorder_record(3, &wav_size, NULL);
        if (wav && wav_size > 44) {
            int16_t *pcm = (int16_t *)(wav + 44);
            size_t nsamp = (wav_size - 44) / 2;
            int16_t smin = 32767, smax = -32768;
            int32_t sum = 0;
            int zero_cnt = 0;
            for (size_t i = 0; i < nsamp; i++) {
                int16_t s = pcm[i];
                if (s < smin) smin = s;
                if (s > smax) smax = s;
                sum += s;
                if (s == 0) zero_cnt++;
            }
            int16_t avg = (int16_t)(sum / (int32_t)nsamp);
            ESP_LOGI("SelfTest", "recorder OK: %d samples, min=%d max=%d avg=%d zeros=%d/%d",
                     (int)nsamp, smin, smax, avg, zero_cnt, (int)nsamp);
            if (smin == 0 && smax == 0) {
                ESP_LOGE("SelfTest", "WARNING: all-zero PCM! Check wiring (BCLK/WS/SD) and L/R=GND");
            } else if (smin == -32768 || smax == 32767) {
                ESP_LOGW("SelfTest", "WARNING: clipping detected! Gain too high or wiring issue");
            } else {
                ESP_LOGI("SelfTest", "PCM looks healthy, microphone is working");
            }
            free(wav);
        } else {
            ESP_LOGE("SelfTest", "recorder FAILED: no data");
        }
    }
#endif

    Serial.println("[boot] boot done");
}

void loop()
{
    player_loop();
    delay(5);
}
