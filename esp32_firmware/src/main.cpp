/*
 * ai_music ESP32 固件入口 (VIEWE UEDX24320028E-WB-A, ESP32-S3-N16R8)
 * Stage2: 五页触摸 UI (主菜单/录音/生成中/播放/历史) + 中文字库
 */

#include <Arduino.h>
#include <esp_display_panel.hpp>
#include <lvgl.h>
#include "lvgl_v8_port.h"
#include "ui.h"
#include "network.h"

using namespace esp_panel::drivers;
using namespace esp_panel::board;

void setup()
{
    Serial.begin(115200);

    Serial.println("Connecting WiFi");
    network_init();

    Serial.println("Initializing board");
    Board *board = new Board();
    board->init();
    assert(board->begin());

    Serial.println("Initializing LVGL");
    lvgl_port_init(board->getLCD(), board->getTouch());

    Serial.println("Creating UI");
    lvgl_port_lock(-1);
    ui_create();
    lvgl_port_unlock();
}

void loop()
{
    delay(1000);
}
