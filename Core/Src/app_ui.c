/**
  ******************************************************************************
  * @file    app_ui.c
  * @brief   状态显示。TFTLCD（FSMC）还没移植，先用 USART1 打一行状态顶替：
  *          等把正点原子 lcd.c 加进来后，把 app_ui_print_status() 里的
  *          dbg_printf() 换成 LCD_ShowString() 即可，上层调用不用改。
  ******************************************************************************
  */

#include "app_ui.h"
#include "app_dbg.h"
#include "app_dim.h"
#include "app_dust.h"
#include "app_relay.h"
#include "main.h"

static uint8_t s_enabled = 1u;

void app_ui_init(void)
{
    s_enabled = 1u;
}

void app_ui_enable(uint8_t on)
{
    s_enabled = (on != 0u) ? 1u : 0u;
}

uint8_t app_ui_enabled(void)
{
    return s_enabled;
}

void app_ui_print_status(void)
{
    char bits[APP_RELAY_COUNT + 1u];
    uint8_t mode = app_relay_mode();
    uint8_t i;

    for (i = 0u; i < APP_RELAY_COUNT; i++)
    {
        bits[i] = ((mode >> i) & 1u) ? '1' : '0';
    }
    bits[APP_RELAY_COUNT] = '\0';

    dbg_printf("%6lus  PM=%3u ug/m3  TH=%3u  LV=%-6s  RELAY=0x%02X [%s]  DIM=%3u%%/%3u%%\r\n",
               (unsigned long)(HAL_GetTick() / 1000u),
               (unsigned int)app_dust_pm(),
               (unsigned int)app_dust_threshold(),
               app_dust_level_name(app_dust_level()),
               (unsigned int)mode,
               bits,
               (unsigned int)app_dim_get(1u),
               (unsigned int)app_dim_get(2u));
}

void app_ui_task(void)
{
    if (s_enabled)
    {
        app_ui_print_status();
    }
}
