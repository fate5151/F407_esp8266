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

#ifdef __cplusplus
}
#endif

#endif /* __APP_RELAY_H */
