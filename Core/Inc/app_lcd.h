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

/* 点击命中测试：1~6 = 对应继电器方块，APP_LCD_BTN_ALL_OFF = 全断按钮，0 = 空白 */
#define APP_LCD_BTN_ALL_OFF 0xFEu
uint8_t app_lcd_hit_test(uint16_t x, uint16_t y);

uint16_t app_lcd_width(void);
uint16_t app_lcd_height(void);
uint16_t app_lcd_id(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_LCD_H */
