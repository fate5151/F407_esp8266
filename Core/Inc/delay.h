/**
  ******************************************************************************
  * @file    delay.h
  * @brief   us 级延时（DWT 周期计数器）与 ms 级延时（HAL_Delay）
  ******************************************************************************
  */

#ifndef __DELAY_H
#define __DELAY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

void delay_init(void);
void delay_us(uint32_t us);
void delay_ms(uint32_t ms);

#ifdef __cplusplus
}
#endif

#endif /* __DELAY_H */
