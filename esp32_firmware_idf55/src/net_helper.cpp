#include "net_helper.h"
#include <WiFiManager.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static String g_server_host;
static uint16_t g_server_port = 5000;
static bool g_server_tls = false;
static bool g_portal_used = false;

static void load_prefs(void)
{
    Preferences prefs;
    prefs.begin("ai_music", true);
    g_server_host = prefs.getString("server_host", "");
    if (g_server_host.length() == 0) {
        /* 兼容旧固件只存 server_ip */
        g_server_host = prefs.getString("server_ip", "");
    }
    g_server_port = (uint16_t)prefs.getUShort("server_port", 0);
    if (g_server_port == 0) {
        g_server_port = 5000;
    }
    g_server_tls = prefs.getBool("server_tls", false);
    prefs.end();
}

static void save_prefs(void)
{
    Preferences prefs;
    prefs.begin("ai_music", false);
    prefs.putString("server_host", g_server_host);
    prefs.putString("server_ip", g_server_host); /* 兼容旧字段 */
    prefs.putUShort("server_port", g_server_port);
    prefs.putBool("server_tls", g_server_tls);
    prefs.end();
}

static void normalize_host(String &h)
{
    h.trim();
    /* 用户若粘贴了完整 URL，剥掉协议与路径 */
    if (h.startsWith("https://")) {
        h = h.substring(8);
        g_server_tls = true;
        if (g_server_port == 5000) g_server_port = 443;
    } else if (h.startsWith("http://")) {
        h = h.substring(7);
        g_server_tls = false;
    }
    int slash = h.indexOf('/');
    if (slash >= 0) h = h.substring(0, slash);
    int colon = h.indexOf(':');
    if (colon > 0) {
        int p = h.substring(colon + 1).toInt();
        if (p > 0 && p <= 65535) {
            g_server_port = (uint16_t)p;
        }
        h = h.substring(0, colon);
    }
    h.trim();
}

bool network_init(network_status_cb_t status_cb)
{
    load_prefs();

    WiFiManager wm;
    wm.setConfigPortalTimeout(300);

    String host_def = (g_server_host.length() > 0) ? g_server_host : "192.168.1.100";
    char port_def[8];
    snprintf(port_def, sizeof(port_def), "%u", (unsigned)g_server_port);
    const char *tls_def = g_server_tls ? "1" : "0";

    WiFiManagerParameter p_host("server_host", "Backend Host (IP or domain)", host_def.c_str(), 64);
    WiFiManagerParameter p_port("server_port", "Backend Port (5000 or 443)", port_def, 6);
    WiFiManagerParameter p_tls("server_tls", "HTTPS 1=on 0=off", tls_def, 2);
    wm.addParameter(&p_host);
    wm.addParameter(&p_port);
    wm.addParameter(&p_tls);

    g_portal_used = false;
    if (status_cb) {
        wm.setAPCallback([status_cb](WiFiManager *) {
            g_portal_used = true;
            status_cb("WiFi not configured\nConnect phone to AP:\nAI-Music-Setup");
        });
    }

    Serial.printf("[net] autoConnect... (saved %s://%s:%u)\n",
                  g_server_tls ? "https" : "http",
                  g_server_host.length() > 0 ? g_server_host.c_str() : "(not set)",
                  (unsigned)g_server_port);

    bool res = wm.autoConnect("AI-Music-Setup");
    if (!res) {
        Serial.printf("[net] WiFi config portal timeout or failed\n");
        return false;
    }

    if (g_portal_used) {
        String new_host = p_host.getValue();
        String new_port = p_port.getValue();
        String new_tls = p_tls.getValue();
        new_host.trim();
        new_port.trim();
        new_tls.trim();
        bool changed = false;
        if (new_host.length() > 0) {
            normalize_host(new_host);
            if (new_host != g_server_host) {
                g_server_host = new_host;
                changed = true;
            } else {
                g_server_host = new_host;
            }
        }
        if (new_port.length() > 0) {
            int p = new_port.toInt();
            if (p > 0 && p <= 65535 && (uint16_t)p != g_server_port) {
                g_server_port = (uint16_t)p;
                changed = true;
            }
        }
        if (new_tls.length() > 0) {
            bool tls = (new_tls[0] == '1' || new_tls[0] == 'y' || new_tls[0] == 'Y' ||
                        new_tls[0] == 't' || new_tls[0] == 'T');
            if (tls != g_server_tls) {
                g_server_tls = tls;
                changed = true;
            }
        }
        if (changed || g_server_host.length() > 0) {
            save_prefs();
            Serial.printf("[net] server saved from portal: %s://%s:%u\n",
                          g_server_tls ? "https" : "http",
                          g_server_host.c_str(), (unsigned)g_server_port);
        }
    }

    Serial.printf("[net] WiFi connected, IP=%s, server=%s://%s:%u\n",
                  WiFi.localIP().toString().c_str(),
                  g_server_tls ? "https" : "http",
                  g_server_host.length() > 0 ? g_server_host.c_str() : "(not set)",
                  (unsigned)g_server_port);
    return true;
}

const char *network_server_host(void)
{
    return g_server_host.c_str();
}

const char *network_server_ip(void)
{
    return network_server_host();
}

uint16_t network_server_port(void)
{
    return g_server_port;
}

bool network_server_tls(void)
{
    return g_server_tls;
}

void network_set_server_host(const char *host)
{
    if (!host || !host[0]) return;
    String h = host;
    normalize_host(h);
    if (h.length() == 0) return;
    g_server_host = h;
    save_prefs();
    Serial.printf("[net] host updated: %s\n", g_server_host.c_str());
}

void network_set_server_port(uint16_t port)
{
    if (port == 0) return;
    g_server_port = port;
    save_prefs();
    Serial.printf("[net] port updated: %u\n", (unsigned)g_server_port);
}

void network_set_server_tls(bool tls)
{
    g_server_tls = tls;
    save_prefs();
    Serial.printf("[net] tls updated: %d\n", (int)g_server_tls);
}

void network_set_server(const char *host, uint16_t port, bool tls)
{
    if (host && host[0]) {
        String h = host;
        normalize_host(h);
        g_server_host = h;
    }
    if (port > 0) g_server_port = port;
    g_server_tls = tls;
    /* normalize_host 可能改 port/tls；以显式参数为准再写一次 */
    if (port > 0) g_server_port = port;
    g_server_tls = tls;
    save_prefs();
    Serial.printf("[net] server set: %s://%s:%u\n",
                  g_server_tls ? "https" : "http",
                  g_server_host.c_str(), (unsigned)g_server_port);
}

void network_set_server_ip(const char *ip)
{
    network_set_server_host(ip);
}

size_t network_make_url(char *out, size_t out_size, const char *path)
{
    if (!out || out_size == 0 || g_server_host.length() == 0) return 0;
    const char *p = (path && path[0]) ? path : "/";
    if (p[0] != '/') {
        /* 容错：补 / */
        int n = snprintf(out, out_size, "%s://%s:%u/%s",
                         g_server_tls ? "https" : "http",
                         g_server_host.c_str(), (unsigned)g_server_port, p);
        return (n > 0 && (size_t)n < out_size) ? (size_t)n : 0;
    }
    int n = snprintf(out, out_size, "%s://%s:%u%s",
                     g_server_tls ? "https" : "http",
                     g_server_host.c_str(), (unsigned)g_server_port, p);
    return (n > 0 && (size_t)n < out_size) ? (size_t)n : 0;
}

size_t network_resolve_url(char *out, size_t out_size, const char *url_or_path)
{
    if (!out || out_size == 0 || !url_or_path || !url_or_path[0]) return 0;
    if (strncmp(url_or_path, "http://", 7) == 0 ||
        strncmp(url_or_path, "https://", 8) == 0) {
        strncpy(out, url_or_path, out_size - 1);
        out[out_size - 1] = 0;
        return strlen(out);
    }
    if (url_or_path[0] == '/') {
        return network_make_url(out, out_size, url_or_path);
    }
    /* 裸文件名 */
    char path[160];
    snprintf(path, sizeof(path), "/outputs/%s", url_or_path);
    return network_make_url(out, out_size, path);
}

size_t network_endpoint_summary(char *out, size_t out_size)
{
    if (!out || out_size == 0) return 0;
    if (g_server_host.length() == 0) {
        strncpy(out, "(not set)", out_size - 1);
        out[out_size - 1] = 0;
        return strlen(out);
    }
    int n = snprintf(out, out_size, "%s://%s:%u",
                     g_server_tls ? "https" : "http",
                     g_server_host.c_str(), (unsigned)g_server_port);
    return (n > 0 && (size_t)n < out_size) ? (size_t)n : 0;
}

Client *network_connect_backend(uint32_t timeout_ms)
{
    if (g_server_host.length() == 0) {
        Serial.printf("[net] no server host\n");
        return NULL;
    }

    Client *client = NULL;
    if (g_server_tls) {
        WiFiClientSecure *sec = new WiFiClientSecure();
        if (!sec) return NULL;
        sec->setInsecure(); /* 演示优先：跳过证书校验 */
        sec->setTimeout(timeout_ms); /* Stream timeout: ms */
        Serial.printf("[net] TLS connect %s:%u ...\n",
                      g_server_host.c_str(), (unsigned)g_server_port);
        if (!sec->connect(g_server_host.c_str(), g_server_port)) {
            Serial.printf("[net] TLS connect failed\n");
            delete sec;
            return NULL;
        }
        client = sec;
    } else {
        WiFiClient *plain = new WiFiClient();
        if (!plain) return NULL;
        plain->setTimeout(timeout_ms);
        Serial.printf("[net] connect %s:%u ...\n",
                      g_server_host.c_str(), (unsigned)g_server_port);
        if (!plain->connect(g_server_host.c_str(), g_server_port)) {
            Serial.printf("[net] connect failed\n");
            delete plain;
            return NULL;
        }
        client = plain;
    }
    return client;
}

void network_write_host_header(Client *client)
{
    if (!client) return;
    /* 标准端口可省略，这里始终带端口，兼容反代与非标准端口 */
    client->printf("Host: %s:%u\r\n", g_server_host.c_str(), (unsigned)g_server_port);
}

void network_reset_wifi_and_reboot(void)
{
    Serial.println("[net] clearing WiFi credentials, reboot to portal...");
    WiFi.disconnect(true /* wifioff */, true /* eraseAP */);
    delay(200);
    {
        WiFiManager wm;
        wm.resetSettings();
    }
    delay(300);
    ESP.restart();
}

char *network_fetch_history(void)
{
    if (g_server_host.length() == 0) {
        Serial.printf("[net] no server host, skip history\n");
        return NULL;
    }

    Client *client = network_connect_backend(15000);
    if (!client) return NULL;

    client->print("GET /api/history HTTP/1.1\r\n");
    network_write_host_header(client);
    client->print("Connection: close\r\n\r\n");

    String response;
    unsigned long deadline = millis() + 20000UL;
    while (client->connected() || client->available()) {
        if (millis() > deadline) {
            Serial.printf("[net] history timeout\n");
            break;
        }
        while (client->available()) {
            response += (char)client->read();
        }
        if (!client->available() && client->connected()) {
            delay(10);
        }
    }
    client->stop();
    delete client;

    int sep = response.indexOf("\r\n\r\n");
    if (sep < 0) {
        Serial.printf("[net] no body separator\n");
        return NULL;
    }
    String body = response.substring(sep + 4);
    if (body.length() == 0) {
        Serial.printf("[net] empty body\n");
        return NULL;
    }

    char *buf = (char *)malloc(body.length() + 1);
    if (buf) strcpy(buf, body.c_str());
    Serial.printf("[net] GET /api/history ok, %d bytes\n", (int)body.length());
    return buf;
}
