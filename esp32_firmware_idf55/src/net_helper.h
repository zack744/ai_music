#pragma once

#include <stddef.h>
#include <stdint.h>

/* WiFi 连接状态回调（用于屏幕显示进度） */
typedef void (*network_status_cb_t)(const char *msg);

#ifdef __cplusplus
extern "C" {
#endif

/* WiFiManager 配网(阻塞): AP "AI-Music-Setup"
 * 可配置 WiFi + 后端 Host / Port / HTTPS。
 * 返回 true=已连上 WiFi。 */
bool network_init(network_status_cb_t status_cb);

/* 后端 host（域名或 IP）。兼容旧名 network_server_ip。 */
const char *network_server_host(void);
const char *network_server_ip(void);

uint16_t network_server_port(void);
bool network_server_tls(void);

/* 公网鉴权：与后端 API_ACCESS_KEY / SITE_PASSWORD 一致；空=不发头 */
const char *network_api_key(void);
void network_set_api_key(const char *key);

/* 运行时改配置并写 NVS */
void network_set_server_host(const char *host);
void network_set_server_port(uint16_t port);
void network_set_server_tls(bool tls);
void network_set_server(const char *host, uint16_t port, bool tls);

/* 仅 IP 兼容入口：改 host，保留 port/tls */
void network_set_server_ip(const char *ip);

/* 拼绝对 URL。path 以 / 开头。返回写入长度（不含 \\0），失败 0 */
size_t network_make_url(char *out, size_t out_size, const char *path);

/* 相对路径或绝对 URL → 可下载的绝对 URL */
size_t network_resolve_url(char *out, size_t out_size, const char *url_or_path);

/* 启动/设置页摘要，如 "https://api.ex.com:443" */
size_t network_endpoint_summary(char *out, size_t out_size);

/* HTTP(S) GET /api/history，JSON 字符串 malloc，调用者 free；失败 NULL */
char *network_fetch_history(void);

/* 清除已存 WiFi 账号（不改后端 Host/Port/TLS），然后重启。
 * 重启后 autoConnect 会开 AP "AI-Music-Setup" 进入配网。 */
void network_reset_wifi_and_reboot(void);

#ifdef __cplusplus
}

#include <Client.h>

/* C++：按当前 TLS 配置连接后端。调用方负责 stop。
 * 返回堆上 Client*（WiFiClient 或 WiFiClientSecure），失败 NULL。 */
Client *network_connect_backend(uint32_t timeout_ms = 15000);

/* 写 Host 头到 client（已 connect） */
void network_write_host_header(Client *client);

/* 写 X-API-Key（api_key 非空时） */
void network_write_auth_header(Client *client);

#endif
