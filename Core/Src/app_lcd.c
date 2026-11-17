/**
  ******************************************************************************
  * @file    app_lcd.c
  * @brief   T7 显示：3.5 寸 TFTLCD 竖屏 320x480（NT35310），显示
  *          6 路继电器状态 / PM2.5 / 阈值 / 报警等级 / 网络 IP
  ******************************************************************************
  */

#include "app_lcd.h"
#include "app_cfg.h"
#include "app_dbg.h"
#include "app_dust.h"
#include "app_net.h"
#include "app_relay.h"
#include "delay.h"
#include "lcd.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* 主界面全量重画（设置界面退出后要恢复） */
static void app_lcd_redraw_all(void);

/* 竖屏 320 x 480：3 路继电器排成一行，方块做高一点把上半屏用起来 */
#define GRID_COLS   3u
#define BOX_W       84u
#define BOX_H       150u
#define BOX_GAP     14u
#define GRID_X      12u
#define GRID_Y      76u

#define ALL_BTN_X0  12u
#define ALL_BTN_Y0  412u
#define ALL_BTN_X1  150u
#define ALL_BTN_Y1  456u

#define WIFI_BTN_X0 166u
#define WIFI_BTN_Y0 412u
#define WIFI_BTN_X1 308u
#define WIFI_BTN_Y1 456u

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

/* ---------------------------------------------------------------- 软键盘 */

/* 5 行 x 10 键：前 4 行放字符，最后一行功能键（用更宽的 5 键，见 kb_key_rect） */
#define KB_X0       5u
#define KB_Y0       196u
#define KB_KEY_W    29u
#define KB_KEY_H    48u
#define KB_GAP_X    2u
#define KB_GAP_Y    2u
#define KB_COLS     10u
#define KB_ROWS     5u

#define KB_FN_W     60u      /* 最后一行功能键宽度 */

/* 键盘上的字符（最后一行 NULL 表示功能键，单独画） */
static const char k_kb_rows[4][11] = {
    "1234567890",
    "qwertyuiop",
    "asdfghjkl-",
    "zxcvbnm._@"
};
static const char *k_kb_fn[5] = { "SHIFT", "SPC", "DEL", "CLR", "OK" };

static uint8_t s_setup;                 /* 1 = 正在 WiFi 设置界面 */
static uint8_t s_shift;                 /* 1 = 大写模式 */
static uint8_t s_field;                 /* 0 = 编辑 SSID，1 = 编辑密码 */
static char    s_ssid[33];
static char    s_pwd[65];
static uint8_t s_ssid_len;
static uint8_t s_pwd_len;
static char    s_note[40];              /* 底部提示（保存结果等） */

/* 键盘第 row/col 键的矩形 */
static void kb_key_rect(uint8_t row, uint8_t col, uint16_t *x0, uint16_t *y0,
                        uint16_t *w, uint16_t *h)
{
    if (row >= 4u)
    {
        *x0 = (uint16_t)(KB_X0 + col * (KB_FN_W + KB_GAP_X));
        *w = KB_FN_W;
    }
    else
    {
        *x0 = (uint16_t)(KB_X0 + col * (KB_KEY_W + KB_GAP_X));
        *w = KB_KEY_W;
    }
    *y0 = (uint16_t)(KB_Y0 + row * (KB_KEY_H + KB_GAP_Y));
    *h = KB_KEY_H;
}

/* 画一个键：底色深灰、文字白；按下时由调用方换色重画 */
static void kb_draw_key(uint8_t row, uint8_t col, uint16_t bg)
{
    uint16_t x, y, w, h;
    char label[8];
    uint8_t i = 0u;
    uint8_t label_len;

    kb_key_rect(row, col, &x, &y, &w, &h);
    LCD_Fill(x, y, (uint16_t)(x + w), (uint16_t)(y + h), bg);

    if (row < 4u)
    {
        char c = k_kb_rows[row][col];

        label[0] = (char)((s_shift && c >= 'a' && c <= 'z') ? (c - 32) : c);
        label[1] = '\0';
        label_len = 1u;
    }
    else
    {
        const char *s = k_kb_fn[col];

        while (s[i] != '\0' && i < 6u)
        {
            label[i] = s[i];
            i++;
        }
        label[i] = '\0';
        label_len = i;
    }

    POINT_COLOR = WHITE;
    BACK_COLOR = bg;
    LCD_ShowString((uint16_t)(x + (w - label_len * 8u) / 2u),
                   (uint16_t)(y + (h - 16u) / 2u),
                   w, 16u, 16u, (uint8_t *)label);
    POINT_COLOR = WHITE;
    BACK_COLOR = COL_BG;
}

/* 把输入框内容重画一遍（密码用 * 掩码） */
static void setup_draw_field(uint8_t field)
{
    uint16_t y = (field == 0u) ? 64u : 122u;
    const char *text = (field == 0u) ? s_ssid : s_pwd;
    uint8_t len = (field == 0u) ? s_ssid_len : s_pwd_len;
    uint8_t active = (field == s_field) ? 1u : 0u;
    uint16_t box = active ? COL_TITLE : COL_OFF;
    char buf[34];
    uint8_t i;

    LCD_Fill(12u, y, 308u, (uint16_t)(y + 34u), box);

    for (i = 0u; i < len && i < 32u; i++)
    {
        buf[i] = (field == 1u) ? '*' : text[i];
    }
    buf[i] = '\0';

    POINT_COLOR = WHITE;
    BACK_COLOR = box;
    LCD_ShowString(18u, (uint16_t)(y + 9u), 288u, 20u, 16u, (uint8_t *)buf);

    /* 正在编辑时画一条光标 */
    if (active)
    {
        uint16_t cx = (uint16_t)(18u + (uint16_t)i * 8u);

        if (cx < 300u)
        {
            LCD_Fill(cx, (uint16_t)(y + 8u), (uint16_t)(cx + 2u), (uint16_t)(y + 28u), WHITE);
        }
    }

    POINT_COLOR = WHITE;
    BACK_COLOR = COL_BG;
}

/* 整个设置界面重画 */
static void setup_draw(void)
{
    uint8_t r;
    uint8_t c;

    LCD_Clear(COL_BG);

    POINT_COLOR = COL_TITLE;
    BACK_COLOR = COL_BG;
    LCD_ShowString(12u, 12u, 296u, 26u, 24u, (uint8_t *)"WiFi Setting");
    POINT_COLOR = COL_HINT;
    LCD_ShowString(232u, 18u, 80u, 20u, 16u, (uint8_t *)"[X] Exit");
    LCD_DrawLine(12u, 40u, 308u, 40u);

    POINT_COLOR = WHITE;
    LCD_ShowString(12u, 44u, 60u, 16u, 16u, (uint8_t *)"SSID");
    LCD_ShowString(12u, 102u, 60u, 16u, 16u, (uint8_t *)"PASS");

    setup_draw_field(0u);
    setup_draw_field(1u);

    POINT_COLOR = COL_HINT;
    LCD_ShowString(12u, 162u, 296u, 18u, 16u, (uint8_t *)"tap box to edit, OK saves");
    if (s_note[0] != '\0')
    {
        POINT_COLOR = COL_ON;
        LCD_ShowString(12u, 178u, 296u, 16u, 12u, (uint8_t *)s_note);
    }

    for (r = 0u; r < KB_ROWS; r++)
    {
        uint8_t cols = (r < 4u) ? KB_COLS : 5u;

        for (c = 0u; c < cols; c++)
        {
            kb_draw_key(r, c, COL_OFF);
        }
    }

    POINT_COLOR = WHITE;
    BACK_COLOR = COL_BG;
}

/* 把键盘某个键闪烁一下，给触摸反馈 */
static void kb_flash_key(uint8_t row, uint8_t col)
{
    kb_draw_key(row, col, COL_TITLE);
    delay_ms(40u);
    kb_draw_key(row, col, COL_OFF);
}

/* 往当前字段追加一个字符；返回 1 表示确实加进去了 */
static uint8_t setup_append(char ch)
{
    if (s_field == 0u)
    {
        if (s_ssid_len + 1u < (uint8_t)sizeof(s_ssid))
        {
            s_ssid[s_ssid_len++] = ch;
            s_ssid[s_ssid_len] = '\0';
            return 1u;
        }
    }
    else if (s_pwd_len + 1u < (uint8_t)sizeof(s_pwd))
    {
        s_pwd[s_pwd_len++] = ch;
        s_pwd[s_pwd_len] = '\0';
        return 1u;
    }
    return 0u;
}

static void setup_backspace(void)
{
    if (s_field == 0u)
    {
        if (s_ssid_len > 0u)
        {
            s_ssid[--s_ssid_len] = '\0';
        }
    }
    else if (s_pwd_len > 0u)
    {
        s_pwd[--s_pwd_len] = '\0';
    }
}

static void setup_clear(void)
{
    if (s_field == 0u)
    {
        s_ssid_len = 0u;
        memset(s_ssid, 0, sizeof(s_ssid));
    }
    else
    {
        s_pwd_len = 0u;
        memset(s_pwd, 0, sizeof(s_pwd));
    }
}

/* 保存并交给网络模块重连，然后退回主界面 */
static void setup_save(void)
{
    s_ssid[s_ssid_len] = '\0';
    s_pwd[s_pwd_len] = '\0';

    if (s_ssid_len == 0u)
    {
        (void)snprintf(s_note, sizeof(s_note), "SSID is empty");
        setup_draw();
        return;
    }

    if (app_net_start(s_ssid, s_pwd))
    {
        dbg_printf("lcd: wifi set to '%s', reconnecting\r\n", s_ssid);
        s_setup = 0u;
        LCD_Clear(COL_BG);
        app_lcd_redraw_all();
        return;
    }

    (void)snprintf(s_note, sizeof(s_note), "save failed, check SSID/PASS");
    setup_draw();
}

/* 设置界面的点击处理；返回 1 表示已消费 */
static uint8_t setup_tap(uint16_t x, uint16_t y)
{
    uint8_t r;
    uint8_t c;

    /* 右上角 X：直接退出，不保存 */
    if (x >= 224u && y <= 40u)
    {
        dbg_write("lcd: wifi setting cancelled\r\n");
        s_setup = 0u;
        LCD_Clear(COL_BG);
        app_lcd_redraw_all();
        return 1u;
    }

    /* 两个输入框：点一下切过去 */
    if (x >= 12u && x <= 308u)
    {
        if (y >= 64u && y <= 98u)
        {
            uint8_t old = s_field;

            s_field = 0u;
            if (old != s_field)
            {
                setup_draw_field(old);
            }
            setup_draw_field(s_field);
            return 1u;
        }
        if (y >= 122u && y <= 156u)
        {
            uint8_t old = s_field;

            s_field = 1u;
            if (old != s_field)
            {
                setup_draw_field(old);
            }
            setup_draw_field(s_field);
            return 1u;
        }
    }

    /* 键盘：逐行算行列 */
    for (r = 0u; r < KB_ROWS; r++)
    {
        uint8_t cols = (r < 4u) ? KB_COLS : 5u;

        for (c = 0u; c < cols; c++)
        {
            uint16_t kx, ky, kw, kh;

            kb_key_rect(r, c, &kx, &ky, &kw, &kh);
            if (x < kx || x > (uint16_t)(kx + kw) || y < ky || y > (uint16_t)(ky + kh))
            {
                continue;
            }

            kb_flash_key(r, c);

            if (r < 4u)
            {
                char ch = k_kb_rows[r][c];

                if (s_shift && ch >= 'a' && ch <= 'z')
                {
                    ch = (char)(ch - 32);
                }
                (void)setup_append(ch);
                setup_draw_field(s_field);
            }
            else
            {
                switch (c)
                {
                case 0u:                     /* SHIFT */
                    s_shift = (uint8_t)(s_shift ^ 1u);
                    setup_draw();
                    break;
                case 1u:                     /* SPC */
                    (void)setup_append(' ');
                    setup_draw_field(s_field);
                    break;
                case 2u:                     /* DEL */
                    setup_backspace();
                    setup_draw_field(s_field);
                    break;
                case 3u:                     /* CLR */
                    setup_clear();
                    setup_draw_field(s_field);
                    break;
                default:                     /* OK */
                    setup_save();
                    break;
                }
            }
            return 1u;
        }
    }

    return 1u;                           /* 设置界面里点空白也吞掉，别漏到主界面 */
}

/* 进入设置界面：先把已存的账号读出来填好，改一半也能撤回 */
static void setup_open(void)
{
    char ssid[33];
    char pwd[65];

    s_ssid[0] = '\0';
    s_pwd[0] = '\0';
    s_ssid_len = 0u;
    s_pwd_len = 0u;

    if (app_cfg_load_wifi(ssid, pwd))
    {
        size_t n = strlen(ssid);

        if (n >= sizeof(s_ssid))
        {
            n = sizeof(s_ssid) - 1u;
        }
        memcpy(s_ssid, ssid, n);
        s_ssid_len = (uint8_t)n;

        n = strlen(pwd);
        if (n >= sizeof(s_pwd))
        {
            n = sizeof(s_pwd) - 1u;
        }
        memcpy(s_pwd, pwd, n);
        s_pwd_len = (uint8_t)n;
    }

    s_field = 0u;
    s_shift = 0u;
    s_note[0] = '\0';
    s_setup = 1u;
    setup_draw();
    dbg_write("lcd: wifi setting opened\r\n");
}

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

/* "ALL OFF" 与 "WiFi SET" 按钮 */
static void draw_all_off_button(void)
{
    LCD_Fill(ALL_BTN_X0, ALL_BTN_Y0, ALL_BTN_X1, ALL_BTN_Y1, COL_HINT);
    POINT_COLOR = BLACK;
    BACK_COLOR = COL_HINT;
    LCD_ShowString((uint16_t)(ALL_BTN_X0 + 16u), (uint16_t)(ALL_BTN_Y0 + 8u),
                   120u, 24u, 24u, (uint8_t *)"ALL OFF");
    POINT_COLOR = WHITE;
    BACK_COLOR = COL_BG;
}

static void draw_wifi_button(void)
{
    LCD_Fill(WIFI_BTN_X0, WIFI_BTN_Y0, WIFI_BTN_X1, WIFI_BTN_Y1, COL_TITLE);
    POINT_COLOR = WHITE;
    BACK_COLOR = COL_TITLE;
    LCD_ShowString((uint16_t)(WIFI_BTN_X0 + 6u), (uint16_t)(WIFI_BTN_Y0 + 8u),
                   136u, 24u, 24u, (uint8_t *)"WiFi SET");
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

    if (x >= WIFI_BTN_X0 && x <= WIFI_BTN_X1 && y >= WIFI_BTN_Y0 && y <= WIFI_BTN_Y1)
    {
        return APP_LCD_BTN_WIFI;
    }

    return 0u;
}

uint8_t app_lcd_in_setup(void)
{
    return s_setup;
}

void app_lcd_open_wifi_setup(void)
{
    if (!s_ok || s_setup)
    {
        return;
    }
    setup_open();
}

/* 统一入口：设置界面里的点击全部交给 setup_tap，否则按主界面按钮分发 */
void app_lcd_handle_tap(uint16_t x, uint16_t y)
{
    uint8_t hit;

    if (!s_ok)
    {
        return;
    }

    if (s_setup)
    {
        (void)setup_tap(x, y);
        return;
    }

    hit = app_lcd_hit_test(x, y);

    if (hit >= 1u && hit <= APP_RELAY_COUNT)
    {
        app_relay_toggle(hit);
        app_lcd_update();
    }
    else if (hit == APP_LCD_BTN_ALL_OFF)
    {
        app_relay_set_mode(0u);
        app_lcd_update();
    }
    else if (hit == APP_LCD_BTN_WIFI)
    {
        setup_open();
    }
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

    if (!s_ok || s_setup)
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

    s_setup = 0u;
    s_ok = 1u;
    app_lcd_redraw_all();

    dbg_printf("lcd: panel ok, id=0x%04X, %ux%u portrait\r\n",
               (unsigned)lcddev.id, (unsigned)lcddev.width, (unsigned)lcddev.height);
    return 1u;
}

/* 主界面全量重画：从设置界面退回来、或者刚初始化时用 */
static void app_lcd_redraw_all(void)
{
    uint8_t i;

    LCD_Clear(COL_BG);

    POINT_COLOR = COL_TITLE;
    BACK_COLOR = COL_BG;
    LCD_ShowString(12u, 12u, 296u, 30u, 24u, (uint8_t *)"F407 Remote Control");
    LCD_DrawLine(12u, 44u, 308u, 44u);

    POINT_COLOR = WHITE;
    LCD_ShowString(12u, 50u, 296u, 20u, 16u, (uint8_t *)"RELAY CH1 - CH3");

    draw_all_off_button();
    draw_wifi_button();

    /* PM / TH / 单位的文字标签是固定的，只在整屏重画时画一次；
       主循环里只重画变动的数值和等级文字，不用整块填充，所以不会闪 */
    POINT_COLOR = WHITE;
    BACK_COLOR = COL_BG;
    LCD_ShowString(12u, 282u, 40u, 30u, 24u, (uint8_t *)"PM ");
    LCD_ShowString(84u, 282u, 90u, 30u, 24u, (uint8_t *)" ug/m3");
    LCD_ShowString(12u, 314u, 40u, 30u, 24u, (uint8_t *)"TH ");

    /* 继电器方块按当前真实状态画，别等下一次 update */
    for (i = 1u; i <= APP_RELAY_COUNT; i++)
    {
        draw_relay_box(i, (uint8_t)((app_relay_mode() >> (i - 1u)) & 1u),
                       (app_dust_level() == APP_DUST_LEVEL_ALARM) ? 1u : 0u);
    }

    s_last_mode = app_relay_mode();          /* 上面已经画好，不用再重画 */
    s_last_level = (uint8_t)app_dust_level();
    s_last_lv_text = 0xFFu;                  /* 等级文字还没画，交给 update */
    s_last_pm = 0xFFFFu;
    s_last_th = 0xFFFFu;
    s_last_ip[0] = '\0';
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
