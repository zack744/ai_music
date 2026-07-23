#include "net_helper.h"
#include <WiFiManager.h>
#include <WiFi.h>
#include <Preferences.h>

static String g_server_ip;
static bool g_portal_used = false;

bool network_init(network_status_cb_t status_cb)
{
    /* 从 NVS 读取上次保存的后端 IP（空=未配置过，用户可稍后在屏幕设置页填） */
    Preferences prefs;
    prefs.begin("ai_music", true);
    g_server_ip = prefs.getString("server_ip", "");
    prefs.end();

    WiFiManager wm;
    wm.setConfigPortalTimeout(300);

    /* 配网页面里后端 IP 的默认值（NVS 有就用 NVS 的，否则给个占位） */
    String param_default = (g_server_ip.length() > 0) ? g_server_ip : "192.168.1.100";
    WiFiManagerParameter p_server("server", "Backend IP", param_default.c_str(), 24);
    wm.addParameter(&p_server);

    g_portal_used = false;
    if (status_cb) {
        wm.setAPCallback([status_cb](WiFiManager *) {
            g_portal_used = true;
            status_cb("WiFi not configured\nConnect phone to AP:\nAI-Music-Setup");
        });
    }

    Serial.printf("[net] autoConnect... (saved server=%s)\n",
                  g_server_ip.length() > 0 ? g_server_ip.c_str() : "(not set)");
    bool res = wm.autoConnect("AI-Music-Setup");
    if (!res) {
        Serial.printf("[net] WiFi config portal timeout or failed\n");
        return false;
    }

    /* 如果用户走了配网页面，读取并保存后端 IP */
    if (g_portal_used) {
        String new_ip = p_server.getValue();
        new_ip.trim();
        if (new_ip.length() > 0 && new_ip != g_server_ip) {
            g_server_ip = new_ip;
            prefs.begin("ai_music", false);
            prefs.putString("server_ip", g_server_ip);
            prefs.end();
            Serial.printf("[net] server IP saved from portal: %s\n", g_server_ip.c_str());
        }
    }

    Serial.printf("[net] WiFi connected, IP=%s, server=%s\n",
                  WiFi.localIP().toString().c_str(),
                  g_server_ip.length() > 0 ? g_server_ip.c_str() : "(not set)");
    return true;
}

const char *network_server_ip(void)
{
    return g_server_ip.c_str();
}

void network_set_server_ip(const char *ip)
{
    if (!ip || strlen(ip) == 0) return;
    g_server_ip = ip;
    g_server_ip.trim();
    Preferences prefs;
    prefs.begin("ai_music", false);
    prefs.putString("server_ip", g_server_ip);
    prefs.end();
    Serial.printf("[net] server IP updated via UI: %s\n", g_server_ip.c_str());
}

char *network_fetch_history(void)
{
    if (g_server_ip.length() == 0) {
        Serial.printf("[net] no server IP, skip history\n");
        return NULL;
    }
    WiFiClient client;
    Serial.printf("[net] connecting %s:5000...\n", g_server_ip.c_str());
    if (!client.connect(g_server_ip.c_str(), 5000)) {
        Serial.printf("[net] connect to %s:5000 failed\n", g_server_ip.c_str());
        return NULL;
    }
    client.printf("GET /api/history HTTP/1.1\r\nHost: %s:5000\r\nConnection: close\r\n\r\n",
                  g_server_ip.c_str());

    String response;
    unsigned long deadline = millis() + 10000;
    while (client.connected() || client.available()) {
        if (millis() > deadline) { Serial.printf("[net] timeout\n"); break; }
        while (client.available()) {
            response += (char)client.read();
        }
    }
    client.stop();

    int sep = response.indexOf("\r\n\r\n");
    if (sep < 0) { Serial.printf("[net] no body separator\n"); return NULL; }
    String body = response.substring(sep + 4);
    if (body.length() == 0) { Serial.printf("[net] empty body\n"); return NULL; }

    char *buf = (char *)malloc(body.length() + 1);
    if (buf) strcpy(buf, body.c_str());
    Serial.printf("[net] GET /api/history ok, %d bytes\n", (int)body.length());
    return buf;
}
