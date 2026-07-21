#pragma once
#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Stage2 中文字体(simhei 子集,lv_font_conv 生成) */
extern lv_font_t lv_font_zh_16;   /* 16px 普通/小字 */
extern lv_font_t lv_font_zh_22;   /* 22px 标题/按钮 */

/* 创建五页触摸 UI 并显示主菜单。必须在 lvgl_port_lock() 之后调用。 */
void ui_create(void);

#ifdef __cplusplus
}
#endif
