/**
  ******************************************************************************
  * @file    app_lcd.c
  * @brief   T7 显示：3.5 寸 TFTLCD 竖屏 320x480（NT35310），显示
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

/* 竖屏 320 x 480：3 路继电器排成一行，方块做高一点把上半屏用起来 */
#define GRID_COLS   3u
#define BOX_W       84u
#define BOX_H       150u
#define BOX_GAP     14u
#define GRID_X      12u
#define GRID_Y      76u

#define ALL_BTN_X0  12u
#define ALL_BTN_Y0  412u
#define ALL_BTN_X1  308u
#define ALL_BTN_Y1  456u

#define COL_BG      0x0000u    /* 黑 */
#define COL_TITLE   0x001Fu    /* 蓝 */
#define COL_HINT    0x7BEFu    /* 灰 */
#define COL_ON      0x07E0u    /* 绿 */
#define COL_OFF     0x4208u    /* 深灰 */
#define COL_ALARM   0xF800u    /* 红 */
#define COL_WARN    0xFD20u    /* 橙 */

static uint8_t s_ok;
static uint8_t s_last_mode = 0xFFu;
static uint8_t s_last_level = 0xFFu;
static uint8_t s_last_lv_text = 0xFFu;   /* 已画到屏幕上的等级文字 */
static uint16_t s_last_pm = 0xFFFFu;
static uint16_t s_last_th = 0xFFFFu;
static char s_last_ip[20];

/* 按竖屏 3 列算出每个继电器方块的左上角 */
static void box_origin(uint8_t ch, uint16_t *x, uint16_t *y)
{
    uint8_t idx = (uint8_t)(ch - 1u);

    *x = (uint16_t)(GRID_X + (idx % GRID_COLS) * (BOX_W + BOX_GAP));
    *y = (uint16_t)(GRID_Y + (idx / GRID_COLS) * (BOX_H + BOX_GAP));
}

/* 84x150 方块：数字 1/2/3 + ON/OFF，吸合绿、断开深灰、报警联动红 */
static void draw_relay_box(uint8_t ch, uint8_t on, uint8_t lv_alarm)
{
    uint16_t x;
    uint16_t y;
    uint16_t color = on ? COL_ON : COL_OFF;
    char num[2];

    box_origin(ch, &x, &y);

    if (on && lv_alarm)
    {
        color = COL_ALARM;          /* 报警联动开的通道标红 */
    }

    LCD_Fill(x, y, (uint16_t)(x + BOX_W), (uint16_t)(y + BOX_H), color);

    num[0] = (char)('0' + ch);
    num[1] = '\0';

    POINT_COLOR = (on ? BLACK : WHITE);
    BACK_COLOR = color;
    LCD_ShowString((uint16_t)(x + 34u), (uint16_t)(y + 39u), BOX_W, 44u, 32u,
                   (uint8_t *)num);
    LCD_ShowString((uint16_t)(x + 30u), (uint16_t)(y + 91u), BOX_W, 24u, 16u,
                   (uint8_t *)(on ? "ON " : "OFF"));

    POINT_COLOR = WHITE;
    BACK_COLOR = COL_BG;
}

/* "ALL OFF" 按钮 */
static void draw_all_off_button(void)
{
    LCD_Fill(ALL_BTN_X0, ALL_BTN_Y0, ALL_BTN_X1, ALL_BTN_Y1, COL_HINT);
    POINT_COLOR = BLACK;
    BACK_COLOR = COL_HINT;
    LCD_ShowString((uint16_t)(ALL_BTN_X0 + 92u), (uint16_t)(ALL_BTN_Y0 + 8u),
                   140u, 24u, 24u, (uint8_t *)"ALL OFF");
    POINT_COLOR = WHITE;
    BACK_COLOR = COL_BG;
}

uint8_t app_lcd_hit_test(uint16_t x, uint16_t y)
{
    uint8_t ch;

    for (ch = 1u; ch <= APP_RELAY_COUNT; ch++)
    {
        uint16_t bx;
        uint16_t by;

        box_origin(ch, &bx, &by);
        if (x >= bx && x <= (uint16_t)(bx + BOX_W) &&
            y >= by && y <= (uint16_t)(by + BOX_H))
        {
            return ch;
        }
    }

    if (x >= ALL_BTN_X0 && x <= ALL_BTN_X1 && y >= ALL_BTN_Y0 && y <= ALL_BTN_Y1)
    {
        return APP_LCD_BTN_ALL_OFF;
    }

    return 0u;
}

/* 等级 → 颜色：正常绿、预警橙、报警红、无信号灰 */
static uint16_t level_color(app_dust_level_t lv)
{
    switch (lv)
    {
    case APP_DUST_LEVEL_NORMAL:
        return COL_ON;
    case APP_DUST_LEVEL_WARN:
        return COL_WARN;
    case APP_DUST_LEVEL_ALARM:
        return COL_ALARM;
    default:
        return COL_HINT;
    }
}

/* 数值右对齐成定宽字符串。LCD_ShowString 会连字符背景一起画（mode=0），
   只要宽度不变就能把旧值整格擦掉，不必先 LCD_Fill 整块 → 不会闪 */
static void fmt_fixed(char *dst, uint16_t dstlen, uint32_t v, uint8_t width)
{
    char tmp[12];
    uint16_t n = (uint16_t)snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)v);
    uint16_t i = 0u;

    if (n > width)                 /* 调用方已限幅，这里只是防越界覆盖后面的单位 */
    {
        n = width;
    }
    while ((uint16_t)(i + n) < width)
    {
        dst[i++] = ' ';
    }
    memcpy(&dst[i], tmp, n);
    dst[i + n] = '\0';
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

    if (pm != s_last_pm)
    {
        POINT_COLOR = WHITE;
        BACK_COLOR = COL_BG;

        fmt_fixed(buf, sizeof(buf), (uint32_t)((pm > 999u) ? 999u : pm), 3u);
        LCD_ShowString(48u, 282u, 40u, 30u, 24u, (uint8_t *)buf);

        s_last_pm = pm;
    }

    if (th != s_last_th)
    {
        POINT_COLOR = WHITE;
        BACK_COLOR = COL_BG;

        fmt_fixed(buf, sizeof(buf), (uint32_t)th, 4u);
        LCD_ShowString(48u, 314u, 50u, 30u, 24u, (uint8_t *)buf);

        s_last_th = th;
    }

    /* 等级可能单独变化（PM 不变，比如回差计时到点解除报警），不能只看 pm/th */
    if (lv != s_last_lv_text)
    {
        const char *name = app_dust_level_name(lv);
        uint16_t n = 0u;

        BACK_COLOR = COL_BG;
        POINT_COLOR = level_color(lv);

        /* 右补空格到 9 字符宽（"NO SIGNAL" 是最长的），换短文字也能擦干净 */
        while (name[n] != '\0' && n < 8u)
        {
            buf[n] = name[n];
            n++;
        }
        while (n < 9u)
        {
            buf[n++] = ' ';
        }
        buf[n] = '\0';

        LCD_ShowString(170u, 314u, 110u, 30u, 24u, (uint8_t *)buf);
        POINT_COLOR = WHITE;

        s_last_lv_text = (uint8_t)lv;
    }

    if (strcmp(ip, s_last_ip) != 0)
    {
        LCD_Fill(12u, 356u, 308u, 400u, COL_BG);

        POINT_COLOR = COL_HINT;
        BACK_COLOR = COL_BG;
        (void)snprintf(buf, sizeof(buf), "WiFi %s", app_net_state_name(app_net_state()));
        LCD_ShowString(12u, 358u, 296u, 20u, 16u, (uint8_t *)buf);

        if (app_net_running())
        {
            POINT_COLOR = COL_ON;
            (void)snprintf(buf, sizeof(buf), "http://%s/", ip);
            LCD_ShowString(12u, 380u, 296u, 20u, 16u, (uint8_t *)buf);
        }
        else
        {
            POINT_COLOR = COL_HINT;
            LCD_ShowString(12u, 380u, 296u, 20u, 16u, (uint8_t *)"send: wifi SSID PWD");
        }
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

    LCD_Display_Dir(0);             /* 竖屏 320x480 */
    LCD_Clear(BLACK);

    POINT_COLOR = COL_TITLE;
    BACK_COLOR = BLACK;
    LCD_ShowString(12u, 12u, 296u, 30u, 24u, (uint8_t *)"F407 Remote Control");
    LCD_DrawLine(12u, 44u, 308u, 44u);

    POINT_COLOR = WHITE;
    LCD_ShowString(12u, 50u, 296u, 20u, 16u, (uint8_t *)"RELAY CH1 - CH3");

    draw_all_off_button();

    /* PM / TH / 单位的文字标签是固定的，只在初始化画一次；
       主循环里只重画变动的数值和等级文字，不用整块填充，所以不会闪 */
    POINT_COLOR = WHITE;
    BACK_COLOR = COL_BG;
    LCD_ShowString(12u, 282u, 40u, 30u, 24u, (uint8_t *)"PM ");
    LCD_ShowString(84u, 282u, 90u, 30u, 24u, (uint8_t *)" ug/m3");
    LCD_ShowString(12u, 314u, 40u, 30u, 24u, (uint8_t *)"TH ");

    s_ok = 1u;
    s_last_mode = 0xFFu;            /* 强制首帧全画 */
    s_last_level = 0xFFu;
    s_last_lv_text = 0xFFu;
    s_last_pm = 0xFFFFu;
    s_last_th = 0xFFFFu;
    s_last_ip[0] = '\0';

    dbg_printf("lcd: panel ok, id=0x%04X, %ux%u portrait\r\n",
               (unsigned)lcddev.id, (unsigned)lcddev.width, (unsigned)lcddev.height);
    return 1u;
}

uint8_t app_lcd_ok(void)
{
    return s_ok;
}

uint16_t app_lcd_width(void)
{
    return lcddev.width;
}

uint16_t app_lcd_height(void)
{
    return lcddev.height;
}

uint16_t app_lcd_id(void)
{
    return lcddev.id;
}
