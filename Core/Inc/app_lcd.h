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

/* 主循环 200 ms 调用，重画变化的部分；设置界面打开时内部直接跳过 */
void app_lcd_update(void);

uint8_t app_lcd_ok(void);

/* 统一处理一次屏幕点击：继电器方块 / ALL OFF / WiFi SET 按钮 / 软键盘按键。
   触摸回调直接调它即可，不用在外面判断坐标 */
void app_lcd_handle_tap(uint16_t x, uint16_t y);

/* 1 = 正处于 WiFi 设置界面（软键盘），此时主界面暂停刷新 */
uint8_t app_lcd_in_setup(void);

/* 打开 WiFi 设置界面（触摸点按钮走这里，串口 wifiset 命令也走这里） */
void app_lcd_open_wifi_setup(void);

/* 点击命中测试：1~3 = 对应继电器方块，APP_LCD_BTN_xxx = 按钮，0 = 空白 */
#define APP_LCD_BTN_ALL_OFF 0xFEu
#define APP_LCD_BTN_WIFI    0xFDu
uint8_t app_lcd_hit_test(uint16_t x, uint16_t y);

uint16_t app_lcd_width(void);
uint16_t app_lcd_height(void);
uint16_t app_lcd_id(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_LCD_H */
