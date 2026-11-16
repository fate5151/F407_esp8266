/**
  ******************************************************************************
  * @file    app_lcd.c
  * @brief   T7 显示：3.5 寸 TFTLCD（480x320，NT35310）显示
  *          6 路继电器状态 / PM2.5 / 阈值 / 报警等级 / 网络 IP
  ******************************************************************************
  */

#include "app_lcd.h"
#include "app_dbg.h"
#include "app_dust.h"
#include "app_net.h"
#include "app_relay.h"
#include "delay.h"
#include "lcd.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define LCD_W       480u
#define LCD_H       320u
#define ROW_H       30u

#define COL_BG      0x0000u    /* 黑 */
#define COL_TITLE   0x001Fu    /* 蓝 */
#define COL_HINT    0x7BEFu    /* 灰 */
#define COL_ON      0x07E0u    /* 绿 */
#define COL_OFF     0x4208u    /* 深灰 */
#define COL_ALARM   0xF800u    /* 红 */
#define COL_WARN    0xFD20u    /* 橙 */
#define COL_ON_BLUE 0xFFFFu

static uint8_t s_ok;
static uint8_t s_last_mode = 0xFFu;
static uint8_t s_last_level = 0xFFu;
static uint16_t s_last_pm = 0xFFFFu;
static uint16_t s_last_th = 0xFFFFu;
static char s_last_ip[20];

/* 60x60 的继电器方块，亮/暗一目了然 */
static void draw_relay_box(uint8_t ch, uint8_t on, uint8_t lv_alarm)
{
    uint16_t x = (uint16_t)((ch - 1u) * 78u + 14u);
    uint16_t y = 96u;
    uint16_t color = on ? COL_ON : COL_OFF;
    char num[2];

    if (on && lv_alarm)
    {
        color = COL_ALARM;          /* 报警联动开的通道标红 */
    }

    LCD_Fill(x, y, (uint16_t)(x + 60u), (uint16_t)(y + 60u), color);

    num[0] = (char)('1' + (ch - 1u));
    num[1] = '\0';
    POINT_COLOR = (on ? BLACK : WHITE);
    BACK_COLOR = color;
    LCD_ShowString((uint16_t)(x + 20u), (uint16_t)(y + 18u), 24u, 24u, 24u, (uint8_t *)num);

    POINT_COLOR = WHITE;
    BACK_COLOR = BLACK;
    LCD_ShowString(x, (uint16_t)(y + 66u), 16u, 16u, 16u, (uint8_t *)(on ? "ON " : "OFF"));
}

void app_lcd_update(void)
{
    char buf[32];
    uint8_t mode;
    app_dust_level_t lv;
    uint16_t pm;
    uint16_t th;
    const char *ip;
    uint8_t i;

    if (!s_ok)
    {
        return;
    }

    mode = app_relay_mode();
    lv = app_dust_level();
    pm = app_dust_pm();
    th = app_dust_threshold();
    ip = app_net_ip();

    /* 只有变化的部分才重画，避免整屏闪烁 */
    if (mode != s_last_mode || lv != s_last_level)
    {
        for (i = 1u; i <= APP_RELAY_COUNT; i++)
        {
            draw_relay_box(i, (uint8_t)((mode >> (i - 1u)) & 1u),
                           (lv == APP_DUST_LEVEL_ALARM) ? 1u : 0u);
        }
        s_last_mode = mode;
        s_last_level = (uint8_t)lv;
    }

    if (pm != s_last_pm || th != s_last_th)
    {
        LCD_Fill(20u, 200u, 460u, 246u, COL_BG);

        POINT_COLOR = WHITE;
        BACK_COLOR = COL_BG;
        (void)snprintf(buf, sizeof(buf), "PM %u ug/m3   TH %u",
                       (unsigned)pm, (unsigned)th);
        LCD_ShowString(20u, 204u, 24u, 24u, 24u, (uint8_t *)buf);

        POINT_COLOR = (lv == APP_DUST_LEVEL_NORMAL) ? COL_ON
                      : ((lv == APP_DUST_LEVEL_WARN) ? COL_WARN : COL_ALARM);
        LCD_ShowString(340u, 204u, 24u, 24u, 24u, (uint8_t *)app_dust_level_name(lv));
        POINT_COLOR = WHITE;

        s_last_pm = pm;
        s_last_th = th;
    }

    if (strcmp(ip, s_last_ip) != 0)
    {
        LCD_Fill(20u, 254u, 460u, 290u, COL_BG);

        POINT_COLOR = COL_HINT;
        BACK_COLOR = COL_BG;
        if (app_net_running())
        {
            (void)snprintf(buf, sizeof(buf), "WiFi %s  http://%s/",
                           app_net_state_name(app_net_state()), ip);
        }
        else
        {
            (void)snprintf(buf, sizeof(buf), "WiFi %s (send: wifi SSID PWD)",
                           app_net_state_name(app_net_state()));
        }
        LCD_ShowString(20u, 258u, 16u, 16u, 16u, (uint8_t *)buf);
        POINT_COLOR = WHITE;

        (void)strncpy(s_last_ip, ip, sizeof(s_last_ip) - 1u);
        s_last_ip[sizeof(s_last_ip) - 1u] = '\0';
    }
}

uint8_t app_lcd_init(void)
{
    delay_init();
    LCD_Init();                     /* 内部会打印 LCD ID */

    if (lcddev.id == 0u || lcddev.id == 0xFFFFu)
    {
        dbg_write("lcd: no panel detected\r\n");
        s_ok = 0u;
        return 0u;
    }

    LCD_Display_Dir(1);             /* 横屏 480x320 */
    LCD_Clear(BLACK);

    POINT_COLOR = COL_TITLE;
    BACK_COLOR = BLACK;
    LCD_ShowString(20u, 14u, 32u, 32u, 32u, (uint8_t *)"F407 Remote Control");
    LCD_DrawLine(20u, 56u, 460u, 56u);

    POINT_COLOR = WHITE;
    LCD_ShowString(20u, 66u, 16u, 16u, 16u, (uint8_t *)"RELAY CH1 - CH6");

    s_ok = 1u;
    s_last_mode = 0xFFu;            /* 强制首帧全画 */
    s_last_level = 0xFFu;
    s_last_pm = 0xFFFFu;
    s_last_th = 0xFFFFu;
    s_last_ip[0] = '\0';

    dbg_printf("lcd: panel ok, id=0x%04X, %ux%u\r\n",
               (unsigned)lcddev.id, (unsigned)lcddev.width, (unsigned)lcddev.height);
    return 1u;
}

uint8_t app_lcd_ok(void)
{
    return s_ok;
}
