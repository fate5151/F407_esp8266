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

/* 竖屏 320 x 480：继电器做成 3 列 x 2 行 */
#define GRID_COLS   3u
#define BOX_W       84u
#define BOX_GAP     14u
#define GRID_X      12u
#define GRID_Y      76u
#define ROW_PITCH   104u

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

/* 按竖屏 3 列 x 2 行算出每个继电器方块的左上角 */
static void box_origin(uint8_t ch, uint16_t *x, uint16_t *y)
{
    uint8_t idx = (uint8_t)(ch - 1u);

    *x = (uint16_t)(GRID_X + (idx % GRID_COLS) * (BOX_W + BOX_GAP));
    *y = (uint16_t)(GRID_Y + (idx / GRID_COLS) * ROW_PITCH);
}

/* 84x84 方块：数字 + ON/OFF，吸合绿、断开深灰、报警联动红 */
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

    LCD_Fill(x, y, (uint16_t)(x + BOX_W), (uint16_t)(y + BOX_W), color);

    num[0] = (char)('0' + ch);
    num[1] = '\0';

    POINT_COLOR = (on ? BLACK : WHITE);
    BACK_COLOR = color;
    LCD_ShowString((uint16_t)(x + 30u), (uint16_t)(y + 12u), BOX_W, 44u, 24u,
                   (uint8_t *)num);
    LCD_ShowString((uint16_t)(x + 30u), (uint16_t)(y + 50u), BOX_W, 30u, 16u,
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
            y >= by && y <= (uint16_t)(by + BOX_W))
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
        LCD_Fill(12u, 280u, 308u, 348u, COL_BG);

        POINT_COLOR = WHITE;
        BACK_COLOR = COL_BG;
        (void)snprintf(buf, sizeof(buf), "PM %u ug/m3", (unsigned)pm);
        LCD_ShowString(12u, 282u, 296u, 30u, 24u, (uint8_t *)buf);

        (void)snprintf(buf, sizeof(buf), "TH %u", (unsigned)th);
        LCD_ShowString(12u, 314u, 150u, 30u, 24u, (uint8_t *)buf);

        s_last_pm = pm;
        s_last_th = th;
        s_last_lv_text = 0xFFu;     /* 上面那次填充把等级区擦掉了，强制重画 */
    }

    /* 等级可能单独变化（PM 不变，比如回差计时到点解除报警），不能只看 pm/th */
    if (lv != s_last_lv_text)
    {
        LCD_Fill(170u, 314u, 308u, 344u, COL_BG);

        BACK_COLOR = COL_BG;
        POINT_COLOR = (lv == APP_DUST_LEVEL_NORMAL) ? COL_ON
                      : ((lv == APP_DUST_LEVEL_WARN) ? COL_WARN : COL_ALARM);
        LCD_ShowString(170u, 314u, 138u, 30u, 24u, (uint8_t *)app_dust_level_name(lv));
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
    LCD_ShowString(12u, 50u, 296u, 20u, 16u, (uint8_t *)"RELAY CH1 - CH6");

    draw_all_off_button();

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
