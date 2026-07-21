#include "network.h"
#include <WiFiManager.h>
#include <WiFi.h>
#include "esp_log.h"

static const char *TAG = "net";
static String g_server_ip;

bool network_init(void)
{
    WiFiManager wm;
    wm.setConfigPortalTimeout(180);

    WiFiManagerParameter p_server("server", "Backend IP", "192.168.1.100", 20);
    wm.addParameter(&p_server);

    bool res = wm.autoConnect("AI-Music-Setup");
    if (!res) {
        ESP_LOGE(TAG, "WiFi config portal timeout or failed");
        return false;
    }
    g_server_ip = p_server.getValue();
    g_server_ip.trim();
    ESP_LOGI(TAG, "WiFi connected, server=%s", g_server_ip.c_str());
    return true;
}

const char *network_server_ip(void)
{
    return g_server_ip.c_str();
}

char *network_fetch_history(void)
{
    if (g_server_ip.length() == 0) return NULL;
    WiFiClient client;
    if (!client.connect(g_server_ip.c_str(), 5000)) {
        ESP_LOGE(TAG, "connect to %s:5000 failed", g_server_ip.c_str());
        return NULL;
    }
    client.printf("GET /api/history HTTP/1.1\r\nHost: %s:5000\r\nConnection: close\r\n\r\n",
                  g_server_ip.c_str());

    String response;
    unsigned long deadline = millis() + 10000;
    while (client.connected() || client.available()) {
        if (millis() > deadline) { ESP_LOGE(TAG, "timeout"); break; }
        while (client.available()) {
            response += (char)client.read();
        }
    }
    client.stop();

    int sep = response.indexOf("\r\n\r\n");
    if (sep < 0) { ESP_LOGE(TAG, "no body separator"); return NULL; }
    String body = response.substring(sep + 4);
    if (body.length() == 0) { ESP_LOGE(TAG, "empty body"); return NULL; }

    char *buf = (char *)malloc(body.length() + 1);
    if (buf) strcpy(buf, body.c_str());
    ESP_LOGI(TAG, "GET /api/history ok, %d bytes", (int)body.length());
    return buf;
}
