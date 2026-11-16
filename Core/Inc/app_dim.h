/**
  ******************************************************************************
  * @file    app_dim.h
  * @brief   TIM8_CH1/CH2 两路调光（PC6 / PC7，1 kHz）
  ******************************************************************************
  */

#ifndef __APP_DIM_H
#define __APP_DIM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define APP_DIM_COUNT 2u

/* 两路都置 0%，等待 PWM 已由 main 启动 */
void app_dim_init(void);

/* ch: 1~2，percent: 0~100 */
void app_dim_set(uint8_t ch, uint8_t percent);

/* 返回 ch 上一次设置的百分比，非法通道返回 0 */
uint8_t app_dim_get(uint8_t ch);

#ifdef __cplusplus
}
#endif

#endif /* __APP_DIM_H */
