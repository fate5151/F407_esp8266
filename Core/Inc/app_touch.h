/**
  ******************************************************************************
  * @file    app_touch.h
  * @brief   3.5 寸屏 XPT2046 电阻触摸：读取、4 点校准（存 Flash）、点击检测
  ******************************************************************************
  */

#ifndef __APP_TOUCH_H
#define __APP_TOUCH_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* 触摸事件回调：返回 1 表示这次点击已被处理 */
typedef void (*app_touch_cb_t)(uint16_t x, uint16_t y);

/* 初始化引脚并载入 Flash 里的校准值（没有则用内置默认值） */
uint8_t app_touch_init(void);

/* 主循环 10 ms 调用；按下→松开时回调一次 */
void app_touch_task(app_touch_cb_t on_click);

/* 屏幕坐标；未校准时返回 0xFFFF */
uint8_t app_touch_read(uint16_t *x, uint16_t *y);

/* 交互式 4 点校准（会画十字，需要用户依次点四个角） */
void app_touch_calibrate(void);

/* 打印最近一次采样的原始值（调试用） */
void app_touch_report(void);

/* 扫描电容屏触摸芯片（I²C，PB0/PF11） */
void app_touch_scan(void);

/* 连续打印原始值，用于确认触摸数据是否变化 */
void app_touch_poll(uint16_t seconds);

uint8_t app_touch_calibrated(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_TOUCH_H */
