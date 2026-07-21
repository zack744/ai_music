#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* WiFiManager 配网(阻塞):首次启动开 AP "AI-Music-Setup",手机连上后配置
 * WiFi 密码 + 后端服务器 IP。配网成功后保存,后续自动连接。
 * 返回 true=已连上 WiFi。 */
bool network_init(void);

/* 获取配网时输入的后端服务器 IP(如 "192.168.1.100") */
const char *network_server_ip(void);

/* HTTP GET /api/history,返回 JSON 字符串(malloc 分配,调用者 free)。失败返回 NULL */
char *network_fetch_history(void);

#ifdef __cplusplus
}
#endif
