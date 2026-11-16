/**
  ******************************************************************************
  * @file    app_relay.c
  * @brief   6 路继电器驱动，PC0~PC5，推挽输出，高电平吸合
  ******************************************************************************
  */

#include "app_relay.h"
#include "main.h"

/* CH1..CH6 全部在 GPIOC 上，端口常量可以共用 */
static const uint16_t k_relay_pin[APP_RELAY_COUNT] = {
    RELAY_Pin, RELAYC1_Pin, RELAYC2_Pin, RELAYC3_Pin, RELAYC4_Pin, RELAYC5_Pin
};

static uint8_t s_mode;

void app_relay_init(void)
{
    app_relay_set_mode(0u);
}

void app_relay_set_mode(uint8_t mode)
{
    uint8_t i;

    s_mode = (uint8_t)(mode & 0x3Fu);
    for (i = 0u; i < APP_RELAY_COUNT; i++)
    {
        HAL_GPIO_WritePin(GPIOC, k_relay_pin[i],
                          (s_mode & (1u << i)) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    }
}

void app_relay_set_channel(uint8_t ch, uint8_t on)
{
    if (ch < 1u || ch > APP_RELAY_COUNT)
    {
        return;
    }

    if (on)
    {
        app_relay_set_mode((uint8_t)(s_mode | (1u << (ch - 1u))));
    }
    else
    {
        app_relay_set_mode((uint8_t)(s_mode & ~(1u << (ch - 1u))));
    }
}

void app_relay_toggle(uint8_t ch)
{
    if (ch < 1u || ch > APP_RELAY_COUNT)
    {
        return;
    }

    app_relay_set_mode((uint8_t)(s_mode ^ (1u << (ch - 1u))));
}

uint8_t app_relay_mode(void)
{
    return s_mode;
}
