/**
  ******************************************************************************
  * @file    app_alarm.c
  * @brief   蜂鸣器（PC8，经 S8050）与状态灯（PC9）的报警指示，
  *          并在 ALARM 时自动闭合排风扇（CH2，T9 联动）
  ******************************************************************************
  */

#include "app_alarm.h"
#include "app_dbg.h"
#include "app_relay.h"
#include "main.h"

/* 都是高电平有效：PC8 拉高导通三极管，PC9 拉高点亮。
   若实测状态灯是低电平亮，把 LED_ON 电平对调即可 */
#define BEEP_ON()  HAL_GPIO_WritePin(BEEP_GPIO_Port, BEEP_Pin, GPIO_PIN_SET)
#define BEEP_OFF() HAL_GPIO_WritePin(BEEP_GPIO_Port, BEEP_Pin, GPIO_PIN_RESET)
#define LED_ON()   HAL_GPIO_WritePin(LED2_GPIO_Port, LED2_Pin, GPIO_PIN_SET)
#define LED_OFF()  HAL_GPIO_WritePin(LED2_GPIO_Port, LED2_Pin, GPIO_PIN_RESET)

#define ALARM_BEEP_ON_MS    200u    /* ALARM：200 ms 响 / 400 ms 停 */
#define ALARM_BEEP_CYCLE_MS 600u
#define WARN_BLINK_MS       500u    /* WARN：状态灯 1 Hz 慢闪 */
#define FAN_CHANNEL         2u      /* 排风扇接 CH2 */

static uint8_t s_fan_latched;       /* 已强制打开过排风扇 */
static uint8_t s_fan_saved;         /* 强制前的 CH2 状态，解除时恢复 */

void app_alarm_init(void)
{
    BEEP_OFF();
    LED_OFF();
    s_fan_latched = 0u;
    s_fan_saved = 0u;
}

/* 报警时自动排风，解除后恢复用户原来的开关状态 */
static void fan_interlock(app_dust_level_t lv)
{
    if (lv == APP_DUST_LEVEL_ALARM && s_fan_latched == 0u)
    {
        s_fan_saved = (uint8_t)((app_relay_mode() >> (FAN_CHANNEL - 1u)) & 1u);
        app_relay_set_channel(FAN_CHANNEL, 1u);
        s_fan_latched = 1u;
        dbg_printf("alarm: fan CH%u forced ON\r\n", (unsigned)FAN_CHANNEL);
    }
    else if (lv != APP_DUST_LEVEL_ALARM && s_fan_latched != 0u)
    {
        app_relay_set_channel(FAN_CHANNEL, s_fan_saved);
        s_fan_latched = 0u;
        dbg_printf("alarm: fan CH%u restored to %s\r\n", (unsigned)FAN_CHANNEL,
                   s_fan_saved ? "ON" : "OFF");
    }
}

void app_alarm_task(app_dust_level_t lv)
{
    uint32_t tick = HAL_GetTick();

    fan_interlock(lv);

    switch (lv)
    {
    case APP_DUST_LEVEL_ALARM:
        if ((tick % ALARM_BEEP_CYCLE_MS) < ALARM_BEEP_ON_MS)
        {
            BEEP_ON();
        }
        else
        {
            BEEP_OFF();
        }
        LED_ON();
        break;

    case APP_DUST_LEVEL_WARN:
        BEEP_OFF();
        if (((tick / WARN_BLINK_MS) & 1u) != 0u)
        {
            LED_ON();
        }
        else
        {
            LED_OFF();
        }
        break;

    default:
        BEEP_OFF();
        LED_OFF();
        break;
    }
}
