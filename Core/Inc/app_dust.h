/**
  ******************************************************************************
  * @file    app_dust.h
  * @brief   GP2Y1010 粉尘采集：ADC 读取 → 9 点中位数 → PM 换算 → 回差报警状态机
  ******************************************************************************
  */

#ifndef __APP_DUST_H
#define __APP_DUST_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define APP_DUST_TH_DEFAULT 150u   /* 默认报警阈值 ug/m3 */

typedef enum
{
    APP_DUST_LEVEL_NORMAL = 0,
    APP_DUST_LEVEL_WARN,
    APP_DUST_LEVEL_ALARM
} app_dust_level_t;

/* 清空滤波窗口，状态复位到 NORMAL */
void app_dust_init(void);

/* ADC 自校准。本工程 HAL 版本没有 HAL_ADCEx_Calibration_Start()，
   这里按 RM0090 手动置位 CAL；必须在 HAL_ADC_Start() 之前调用 */
void app_dust_calibrate(void);

/* 主循环 10 ms 时基调用：有新数据就采样一次，然后推进状态机 */
void app_dust_task(void);

uint16_t app_dust_pm(void);          /* 中位数换算后的浓度 ug/m3 */
uint16_t app_dust_raw(void);         /* 最近一次 ADC 原始值 */
app_dust_level_t app_dust_level(void);

uint16_t app_dust_threshold(void);
void app_dust_set_threshold(uint16_t th);

const char *app_dust_level_name(app_dust_level_t lv);

#ifdef __cplusplus
}
#endif

#endif /* __APP_DUST_H */
