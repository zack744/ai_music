#pragma once

/* WiFi 连接状态回调（用于屏幕显示进度） */
typedef void (*network_status_cb_t)(const char *msg);

#ifdef __cplusplus
extern "C" {
#endif

/* WiFiManager 配网(阻塞):首次启动开 AP "AI-Music-Setup",手机连上后配置
 * WiFi 密码 + 后端服务器 IP。配网成功后保存,后续自动连接。
 * status_cb: 配网过程中状态变化的回调(如"请连接热点配网"),可为 NULL。
 * 返回 true=已连上 WiFi。 */
bool network_init(network_status_cb_t status_cb);

/* 获取配网时输入的后端服务器 IP(如 "192.168.1.100") */
const char *network_server_ip(void);

/* 运行时修改后端 IP 并持久化到 NVS（供屏幕设置页调用） */
void network_set_server_ip(const char *ip);

/* HTTP GET /api/history,返回 JSON 字符串(malloc 分配,调用者 free)。失败返回 NULL */
char *network_fetch_history(void);

#ifdef __cplusplus
}
#endif
