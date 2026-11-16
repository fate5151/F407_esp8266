/**
  ******************************************************************************
  * @file    app_alarm.h
  * @brief   报警输出：蜂鸣器 PC8 + 状态灯 PC9，非阻塞闪烁
  ******************************************************************************
  */

#ifndef __APP_ALARM_H
#define __APP_ALARM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "app_dust.h"

/* 上电静音 */
void app_alarm_init(void);

/* 主循环 10 ms 时基调用，按报警等级驱动蜂鸣器和状态灯 */
void app_alarm_task(app_dust_level_t lv);

#ifdef __cplusplus
}
#endif

#endif /* __APP_ALARM_H */
