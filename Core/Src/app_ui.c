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
#include "app_dust.h"
#include "app_net.h"
#include "app_relay.h"

/* 板载 LED：DS0 = PF9，DS1 = PF10，这块板是低电平亮 */
#define LED0_ON()  HAL_GPIO_WritePin(LED0_GPIO_Port, LED0_Pin, GPIO_PIN_RESET)
#define LED0_OFF() HAL_GPIO_WritePin(LED0_GPIO_Port, LED0_Pin, GPIO_PIN_SET)
#define LED1_ON()  HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_RESET)
#define LED1_OFF() HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_SET)

static uint8_t s_enabled = 1u;
static uint8_t s_led0_on;

void app_ui_init(void)
{
    s_enabled = 1u;
    s_led0_on = 0u;
    LED0_OFF();
    LED1_OFF();
}

/* DS0 每秒翻转（心跳，0.5 Hz），DS1 网络就绪常亮 */
static void leds_update(void)
{
    s_led0_on = (uint8_t)(s_led0_on ^ 1u);

    if (s_led0_on)
    {
        LED0_ON();
    }
    else
    {
        LED0_OFF();
    }

    if (app_net_running())
    {
        LED1_ON();
    }
    else
    {
        LED1_OFF();
    }
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

    dbg_printf("%5lus PM=%3u TH=%3u %-6s RELAY=0x%02X [%s] NET=%s %s\r\n",
               (unsigned long)(HAL_GetTick() / 1000u),
               (unsigned int)app_dust_pm(),
               (unsigned int)app_dust_threshold(),
               app_dust_level_name(app_dust_level()),
               (unsigned int)mode,
               bits,
               app_net_state_name(app_net_state()),
               app_net_ip());
}

void app_ui_task(void)
{
    leds_update();

    if (s_enabled)
    {
        app_ui_print_status();
    }
}
