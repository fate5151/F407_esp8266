/**
  ******************************************************************************
  * @file    app_lcd.h
  * @brief   3.5 寸 TFTLCD（NT35310 / FSMC NE4）显示 6 路继电器 + 粉尘 + 网络状态
  ******************************************************************************
  */

#ifndef __APP_LCD_H
#define __APP_LCD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* 返回 1 表示屏幕初始化成功（识别到驱动 IC） */
uint8_t app_lcd_init(void);

/* 主循环 1 s / 事件触发时调用，重画整屏 */
void app_lcd_update(void);

uint8_t app_lcd_ok(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_LCD_H */
