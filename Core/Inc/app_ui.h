/**
  ******************************************************************************
  * @file    app_ui.h
  * @brief   状态显示（T7）：6 路继电器 + PM + 阈值 + 报警等级
  ******************************************************************************
  */

#ifndef __APP_UI_H
#define __APP_UI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* 默认打开每秒一行的状态输出 */
void app_ui_init(void);

/* 主循环 1 s 时基调用 */
void app_ui_task(void);

/* 立刻打印一行完整状态（status 命令也用它） */
void app_ui_print_status(void);

void app_ui_enable(uint8_t on);
uint8_t app_ui_enabled(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_UI_H */
