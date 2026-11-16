/**
  ******************************************************************************
  * @file    app_relay.h
  * @brief   6 路继电器驱动（位掩码模式，bit0..bit5 = CH1..CH6）
  ******************************************************************************
  */

#ifndef __APP_RELAY_H
#define __APP_RELAY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define APP_RELAY_COUNT 6u

/* 上电后立刻调用（MX_GPIO_Init 之后）：按保存的极性把引脚拉到"断开"电平，
   避免低电平触发模块在复位瞬间 6 路全吸合 */
void app_relay_preinit(void);

/* 按保存的极性返回"断开"对应的引脚电平（GPIO_PIN_SET / GPIO_PIN_RESET），
   给 gpio.c 作上电初值用，避免输出使能瞬间误吸合 */
uint32_t app_relay_off_level(void);

/* 上电全部断开（mode = 0） */
void app_relay_init(void);

/* 一次性设置 6 路状态，mode 只有低 6 位有效，高电平 = 吸合 */
void app_relay_set_mode(uint8_t mode);

/* ch: 1~6，on: 0 断开 / 非 0 吸合 */
void app_relay_set_channel(uint8_t ch, uint8_t on);

/* ch: 1~6，翻转单路 */
void app_relay_toggle(uint8_t ch);

/* 当前位掩码，bit0..bit5 = CH1..CH6 */
uint8_t app_relay_mode(void);

/* 触发极性：0 = 高电平吸合（默认），1 = 低电平吸合。
   低电平模块在 mc 复位时引脚为低，会自动全吸合，需要靠这个开关纠正 */
void app_relay_set_polarity(uint8_t active_low);
uint8_t app_relay_polarity(void);

/* 重新按当前极性刷新所有引脚（改完极性调一次） */
void app_relay_refresh(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_RELAY_H */
