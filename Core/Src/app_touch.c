/**
  ******************************************************************************
  * @file    app_touch.c
  * @brief   3.5 寸屏 XPT2046 电阻触摸（软件 SPI）
  *
  *   引脚：T_CLK=PB0  T_PEN=PB1  T_MISO=PB2  T_MOSI=PF11  T_CS=PC13
  *   校准：4 点法，按屏幕坐标反推原始值线性映射，结果存 Flash 最后一个扇区
  *         （F407ZGTx 1MB → sector 11 @ 0x080E0000，程序只占 0x08000000~0x08013000，
  *          重新烧录也只会擦 0~4 扇区，不会碰它）
  ******************************************************************************
  */

#include "app_touch.h"
#include "app_dbg.h"
#include "delay.h"
#include "lcd.h"
#include "main.h"

#include <stdint.h>
#include <string.h>

/* ---------------------------------------------------------------- 引脚操作 */

#define T_CLK_H()   (GPIOB->BSRR = GPIO_PIN_0)
#define T_CLK_L()   (GPIOB->BSRR = (uint32_t)GPIO_PIN_0 << 16)
#define T_MOSI_H()  (GPIOF->BSRR = GPIO_PIN_11)
#define T_MOSI_L()  (GPIOF->BSRR = (uint32_t)GPIO_PIN_11 << 16)
#define T_CS_H()    (GPIOC->BSRR = GPIO_PIN_13)
#define T_CS_L()    (GPIOC->BSRR = (uint32_t)GPIO_PIN_13 << 16)
#define T_PEN_DOWN() (((GPIOB->IDR) & GPIO_PIN_1) == 0u)
#define T_MISO()     (((GPIOB->IDR) & GPIO_PIN_2) != 0u)

#define TP_CMD_X    0xD0u      /* 读 X+，12 bit */
#define TP_CMD_Y    0x90u      /* 读 Y+，12 bit */

#define TOUCH_SAMPLES   5u
#define TOUCH_DEBOUNCE  2u     /* 连续 2 次(20 ms)才算按下 */

/* ------------------------------------------------------------ 校准数据 */

#define TOUCH_CAL_MAGIC  0x54504331u    /* "TPC1" */
#define TOUCH_CAL_ADDR   0x080E0000u    /* sector 11 */
#define TOUCH_CAL_SECTOR FLASH_SECTOR_11

typedef struct
{
    uint32_t magic;
    int32_t  x_axis;      /* 屏幕 X 对应的原始轴：0=rawx 1=rawy */
    int32_t  y_axis;
    int32_t  xr0;         /* 屏幕 x=20 处的原始值 */
    int32_t  xr1;         /* 屏幕 x=W-20 处的原始值 */
    int32_t  yr0;         /* 屏幕 y=20 处的原始值 */
    int32_t  yr1;         /* 屏幕 y=H-20 处的原始值 */
    uint32_t sum;         /* 以上字段累加和，做校验 */
} touch_cal_t;

static touch_cal_t s_cal;
static uint8_t s_cal_valid;

static uint16_t s_rawx;
static uint16_t s_rawy;

static uint8_t s_press_cnt;
static uint8_t s_down;
static uint16_t s_down_x;
static uint16_t s_down_y;

/* ------------------------------------------------------------ 软件 SPI */

static void tp_write_byte(uint8_t num)
{
    uint8_t i;

    for (i = 0u; i < 8u; i++)
    {
        if ((num & 0x80u) != 0u)
        {
            T_MOSI_H();
        }
        else
        {
            T_MOSI_L();
        }
        num <<= 1;
        T_CLK_L();
        delay_us(1u);
        T_CLK_H();
    }
}

/* 读数：只有高 12 位有效，范围 0~4095 */
static uint16_t tp_read_ad(uint8_t cmd)
{
    uint16_t num = 0u;
    uint8_t i;

    T_CLK_L();
    T_MOSI_L();
    T_CS_L();

    tp_write_byte(cmd);
    delay_us(6u);                 /* XPT2046 转换时间 */

    T_CLK_L();
    delay_us(1u);
    T_CLK_H();                    /* 清 BUSY */
    delay_us(1u);
    T_CLK_L();

    for (i = 0u; i < 16u; i++)
    {
        num <<= 1;
        T_CLK_L();
        delay_us(1u);
        T_CLK_H();
        if (T_MISO())
        {
            num++;
        }
    }

    T_CS_H();
    return (uint16_t)(num >> 4);
}

/* 换通道后的第一次转换结果不可信，先读一次丢掉 */
static uint16_t tp_read_channel(uint8_t cmd)
{
    (void)tp_read_ad(cmd);
    delay_us(20u);
    return tp_read_ad(cmd);
}

/* 5 次采样，去掉最大/最小取平均 */
static uint16_t tp_read_avg(uint8_t cmd)
{
    uint16_t buf[TOUCH_SAMPLES];
    uint16_t tmp;
    uint32_t sum = 0u;
    uint8_t i;
    uint8_t j;

    for (i = 0u; i < TOUCH_SAMPLES; i++)
    {
        buf[i] = tp_read_channel(cmd);
    }

    for (i = 0u; i + 1u < TOUCH_SAMPLES; i++)
    {
        for (j = (uint8_t)(i + 1u); j < TOUCH_SAMPLES; j++)
        {
            if (buf[i] > buf[j])
            {
                tmp = buf[i];
                buf[i] = buf[j];
                buf[j] = tmp;
            }
        }
    }

    for (i = 1u; i + 1u < TOUCH_SAMPLES; i++)
    {
        sum += buf[i];
    }

    return (uint16_t)(sum / (TOUCH_SAMPLES - 2u));
}

/* ------------------------------------------------------------ GPIO 初始化 */

static void touch_gpio_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();

    /* PB1 (T_PEN) / PB2 (T_MISO) 上拉输入 */
    gpio.Pin = GPIO_PIN_1 | GPIO_PIN_2;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(GPIOB, &gpio);

    /* PB0 (T_CLK) 推挽输出 */
    gpio.Pin = GPIO_PIN_0;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOB, &gpio);

    /* PC13 (T_CS) */
    gpio.Pin = GPIO_PIN_13;
    HAL_GPIO_Init(GPIOC, &gpio);

    /* PF11 (T_MOSI) */
    gpio.Pin = GPIO_PIN_11;
    HAL_GPIO_Init(GPIOF, &gpio);

    T_CS_H();
    T_CLK_H();
}

/* ------------------------------------------------------------ Flash 存取 */

static uint32_t cal_sum(const touch_cal_t *c)
{
    return (uint32_t)c->x_axis + (uint32_t)c->y_axis +
           (uint32_t)c->xr0 + (uint32_t)c->xr1 +
           (uint32_t)c->yr0 + (uint32_t)c->yr1 + 0x5A5A5A5Au;
}

static void cal_load(void)
{
    const touch_cal_t *p = (const touch_cal_t *)TOUCH_CAL_ADDR;

    if (p->magic == TOUCH_CAL_MAGIC && p->sum == cal_sum(p))
    {
        memcpy(&s_cal, p, sizeof(s_cal));
        s_cal_valid = 1u;
        dbg_printf("touch: cal loaded x[%d..%d] y[%d..%d] axis=%d/%d\r\n",
                   (int)s_cal.xr0, (int)s_cal.xr1, (int)s_cal.yr0, (int)s_cal.yr1,
                   (int)s_cal.x_axis, (int)s_cal.y_axis);
    }
    else
    {
        s_cal_valid = 0u;
    }
}

static uint8_t cal_save(void)
{
    const uint32_t *src = (const uint32_t *)&s_cal;
    uint32_t words = sizeof(s_cal) / 4u;
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t sector_err = 0u;
    uint32_t i;
    uint8_t ok = 1u;

    s_cal.magic = TOUCH_CAL_MAGIC;
    s_cal.sum = cal_sum(&s_cal);

    HAL_FLASH_Unlock();

    erase.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase.Sector = TOUCH_CAL_SECTOR;
    erase.NbSectors = 1u;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    if (HAL_FLASHEx_Erase(&erase, &sector_err) != HAL_OK)
    {
        ok = 0u;
    }

    if (ok)
    {
        for (i = 0u; i < words; i++)
        {
            if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                                  TOUCH_CAL_ADDR + i * 4u, src[i]) != HAL_OK)
            {
                ok = 0u;
                break;
            }
        }
    }

    HAL_FLASH_Lock();
    return ok;
}

/* ------------------------------------------------------------ 原始值→屏幕 */

static int32_t map_axis(int32_t raw, int32_t r0, int32_t r1, uint16_t span,
                        uint16_t margin)
{
    int32_t d = r1 - r0;

    if (d == 0)
    {
        return (int32_t)margin;
    }

    return (int32_t)margin + (raw - r0) * (int32_t)span / d;
}

/* 原始值→屏幕坐标 */
static uint8_t raw_to_screen(uint16_t rawx, uint16_t rawy, uint16_t *x, uint16_t *y)
{
    int32_t rx = (int32_t)rawx;
    int32_t ry = (int32_t)rawy;
    int32_t ra;
    int32_t rb;
    int32_t sx;
    int32_t sy;

    if (!s_cal_valid || x == NULL || y == NULL)
    {
        return 0u;
    }

    ra = (s_cal.x_axis != 0) ? ry : rx;
    rb = (s_cal.y_axis != 0) ? ry : rx;

    sx = map_axis(ra, s_cal.xr0, s_cal.xr1, (uint16_t)(lcddev.width - 40u), 20u);
    sy = map_axis(rb, s_cal.yr0, s_cal.yr1, (uint16_t)(lcddev.height - 40u), 20u);

    if (sx < 0)
    {
        sx = 0;
    }
    if (sy < 0)
    {
        sy = 0;
    }
    if (sx >= (int32_t)lcddev.width)
    {
        sx = (int32_t)lcddev.width - 1;
    }
    if (sy >= (int32_t)lcddev.height)
    {
        sy = (int32_t)lcddev.height - 1;
    }

    *x = (uint16_t)sx;
    *y = (uint16_t)sy;
    return 1u;
}

uint8_t app_touch_read(uint16_t *x, uint16_t *y)
{
    s_rawx = tp_read_avg(TP_CMD_X);
    s_rawy = tp_read_avg(TP_CMD_Y);
    return raw_to_screen(s_rawx, s_rawy, x, y);
}

/* ------------------------------------------------------------ 4 点校准 */

static void draw_cross(uint16_t x, uint16_t y)
{
    LCD_Fill((uint16_t)(x - 12u), y, (uint16_t)(x + 12u), y, RED);
    LCD_Fill(x, (uint16_t)(y - 12u), x, (uint16_t)(y + 12u), RED);
    LCD_Fill((uint16_t)(x - 4u), (uint16_t)(y - 4u),
             (uint16_t)(x + 4u), (uint16_t)(y + 4u), RED);
}

/* 等一次"按下再松开"，返回滤波后的原始值 */
static uint8_t wait_touch_raw(uint16_t *rx, uint16_t *ry)
{
    uint32_t start = HAL_GetTick();
    uint32_t release;

    while (!T_PEN_DOWN())                     /* 等用户点下去 */
    {
        if ((HAL_GetTick() - start) > 20000u)
        {
            return 0u;
        }
    }

    delay_ms(20);
    *rx = tp_read_avg(TP_CMD_X);
    *ry = tp_read_avg(TP_CMD_Y);

    release = HAL_GetTick();
    while (T_PEN_DOWN())                      /* 等松开 */
    {
        if ((HAL_GetTick() - release) > 5000u)
        {
            break;
        }
    }
    return 1u;
}

void app_touch_calibrate(void)
{
    uint16_t px[4];
    uint16_t py[4];
    uint16_t cx[4];
    uint16_t cy[4];
    uint16_t w = lcddev.width;
    uint16_t h = lcddev.height;
    uint8_t i;
    int32_t dx_rawx;
    int32_t dx_rawy;

    if (w == 0u || h == 0u)
    {
        dbg_write("touch: no lcd, skip calibration\r\n");
        return;
    }

    cx[0] = 20u;
    cy[0] = 20u;
    cx[1] = (uint16_t)(w - 20u);
    cy[1] = 20u;
    cx[2] = (uint16_t)(w - 20u);
    cy[2] = (uint16_t)(h - 20u);
    cx[3] = 20u;
    cy[3] = (uint16_t)(h - 20u);

    LCD_Clear(BLACK);
    POINT_COLOR = WHITE;
    BACK_COLOR = BLACK;
    LCD_ShowString(10u, (uint16_t)(h / 2u - 16u), 300u, 20u, 16u,
                   (uint8_t *)"Tap the 4 crosses: TL TR BR BL");

    for (i = 0u; i < 4u; i++)
    {
        draw_cross(cx[i], cy[i]);
        dbg_printf("touch: waiting point %u at %u,%u\r\n",
                   (unsigned)(i + 1u), (unsigned)cx[i], (unsigned)cy[i]);

        if (!wait_touch_raw(&px[i], &py[i]))
        {
            dbg_write("touch: calibration timeout\r\n");
            LCD_Clear(BLACK);
            return;
        }
        dbg_printf("touch: point %u raw=%u,%u\r\n",
                   (unsigned)(i + 1u), (unsigned)px[i], (unsigned)py[i]);
        LCD_Fill((uint16_t)(cx[i] - 12u), (uint16_t)(cy[i] - 12u),
                 (uint16_t)(cx[i] + 12u), (uint16_t)(cy[i] + 12u), BLACK);
    }

    /* 用左上→右上判断屏幕 X 走的是哪个原始轴 */
    dx_rawx = (int32_t)px[1] - (int32_t)px[0];
    dx_rawy = (int32_t)py[1] - (int32_t)py[0];
    if (dx_rawx < 0)
    {
        dx_rawx = -dx_rawx;
    }
    if (dx_rawy < 0)
    {
        dx_rawy = -dx_rawy;
    }

    if (dx_rawx >= dx_rawy)
    {
        s_cal.x_axis = 0;                       /* 屏幕 X ← rawX */
        s_cal.xr0 = px[0];
        s_cal.xr1 = px[1];
        s_cal.y_axis = 1;                       /* 屏幕 Y ← rawY */
        s_cal.yr0 = py[0];
        s_cal.yr1 = py[3];
    }
    else
    {
        s_cal.x_axis = 1;                       /* 屏幕 X ← rawY */
        s_cal.xr0 = py[0];
        s_cal.xr1 = py[1];
        s_cal.y_axis = 0;                       /* 屏幕 Y ← rawX */
        s_cal.yr0 = px[0];
        s_cal.yr1 = px[3];
    }

    s_cal_valid = 1u;

    if (cal_save())
    {
        dbg_printf("touch: cal saved x[%d..%d] y[%d..%d] axis=%d/%d\r\n",
                   (int)s_cal.xr0, (int)s_cal.xr1,
                   (int)s_cal.yr0, (int)s_cal.yr1,
                   (int)s_cal.x_axis, (int)s_cal.y_axis);
    }
    else
    {
        dbg_write("touch: cal save failed (RAM only)\r\n");
    }

    LCD_Clear(BLACK);
    POINT_COLOR = WHITE;
    BACK_COLOR = BLACK;
    LCD_ShowString(10u, (uint16_t)(h / 2u - 16u), 300u, 20u, 16u,
                   (uint8_t *)"touch calibrated");
}

/* ------------------------------------------------------------ 对外接口 */

uint8_t app_touch_init(void)
{
    uint16_t rx;
    uint16_t ry;

    touch_gpio_init();
    cal_load();

    /* 随便读一次，确认 SPI 通路（不做校准判断） */
    rx = tp_read_ad(TP_CMD_X);
    ry = tp_read_ad(TP_CMD_Y);
    s_rawx = rx;
    s_rawy = ry;

    if (!s_cal_valid)
    {
        dbg_write("touch: no calibration, run 'touch cal'\r\n");
    }

    s_press_cnt = 0u;
    s_down = 0u;
    return s_cal_valid;
}

void app_touch_task(app_touch_cb_t on_click)
{
    uint16_t x = 0u;
    uint16_t y = 0u;
    uint8_t got;

    if (T_PEN_DOWN())
    {
        if (s_press_cnt < TOUCH_DEBOUNCE)
        {
            s_press_cnt++;
        }
        else if (!s_down)
        {
            s_down = 1u;
            (void)app_touch_read(&s_down_x, &s_down_y);   /* 按下时采样才算数 */
        }
    }
    else
    {
        if (s_down)
        {
            x = s_down_x;
            y = s_down_y;
            got = (uint8_t)((x != 0u || y != 0u) ? 1u : 0u);

            if (got && on_click != NULL && s_cal_valid)
            {
                on_click(x, y);
            }
        }

        s_down = 0u;
        s_press_cnt = 0u;
    }
}

void app_touch_report(void)
{
    uint16_t x;
    uint16_t y;
    uint8_t ok = app_touch_read(&x, &y);

    dbg_printf("touch: raw=%u,%u pen=%d cal=%d screen=%u,%u\r\n",
               (unsigned)s_rawx, (unsigned)s_rawy,
               T_PEN_DOWN() ? 1 : 0, s_cal_valid ? 1 : 0,
               ok ? (unsigned)x : 0u, ok ? (unsigned)y : 0u);
}

uint8_t app_touch_calibrated(void)
{
    return s_cal_valid;
}

/* ---------------------------------------------------- 电容屏 I²C 探测 */
/* 3.5 寸 NT35310 有电阻版和电容版（GT911/GT9147 等）。电容版用 I²C：
   SCL=PB0, SDA=PF11（与电阻版的 T_CLK/T_MOSI 复用同一对引脚） */

#define CT_SCL_H()   (GPIOB->BSRR = GPIO_PIN_0)
#define CT_SCL_L()   (GPIOB->BSRR = (uint32_t)GPIO_PIN_0 << 16)
#define CT_SDA_H()   (GPIOF->BSRR = GPIO_PIN_11)
#define CT_SDA_L()   (GPIOF->BSRR = (uint32_t)GPIO_PIN_11 << 16)
#define CT_SDA_IN()  (GPIOF->MODER = (GPIOF->MODER & ~(3UL << 22)))
#define CT_SDA_OUT() (GPIOF->MODER = (GPIOF->MODER & ~(3UL << 22)) | (1UL << 22))
#define CT_SDA_READ() (((GPIOF->IDR) & GPIO_PIN_11) != 0u)

static void ct_delay(void)
{
    delay_us(2u);
}

static void ct_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();

    gpio.Pin = GPIO_PIN_0;
    gpio.Mode = GPIO_MODE_OUTPUT_OD;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(GPIOB, &gpio);

    gpio.Pin = GPIO_PIN_11;
    HAL_GPIO_Init(GPIOF, &gpio);

    CT_SCL_H();
    CT_SDA_H();
}

static void ct_start(void)
{
    CT_SDA_H();
    CT_SCL_H();
    ct_delay();
    CT_SDA_L();
    ct_delay();
    CT_SCL_L();
    ct_delay();
}

static void ct_stop(void)
{
    CT_SDA_L();
    ct_delay();
    CT_SCL_H();
    ct_delay();
    CT_SDA_H();
    ct_delay();
}

static uint8_t ct_send_byte(uint8_t txd)
{
    uint8_t i;

    for (i = 0u; i < 8u; i++)
    {
        if ((txd & 0x80u) != 0u)
        {
            CT_SDA_H();
        }
        else
        {
            CT_SDA_L();
        }
        ct_delay();
        CT_SCL_H();
        ct_delay();
        CT_SCL_L();
        txd <<= 1;
    }

    CT_SDA_H();
    ct_delay();
    CT_SCL_H();
    ct_delay();

    {
        uint8_t nack = CT_SDA_READ() ? 1u : 0u;   /* 高 = 无应答 */

        CT_SCL_L();
        ct_delay();
        return nack;
    }
}

static uint8_t ct_probe(uint8_t addr7)
{
    uint8_t nack;

    ct_start();
    nack = ct_send_byte((uint8_t)(addr7 << 1));
    ct_stop();
    return (nack == 0u) ? 1u : 0u;
}

/* 轮询原始值若干秒，不依赖 PEN，用来确认触摸是否真的有数据变化 */
void app_touch_poll(uint16_t seconds)
{
    uint32_t end = HAL_GetTick() + (uint32_t)seconds * 1000u;

    dbg_write("touch: polling raw (press the screen now)\r\n");
    while ((int32_t)(HAL_GetTick() - end) < 0)
    {
        uint16_t rx = tp_read_avg(TP_CMD_X);
        uint16_t ry = tp_read_avg(TP_CMD_Y);

        dbg_printf("touch: raw=%4u,%4u pen=%d\r\n", (unsigned)rx, (unsigned)ry,
                   T_PEN_DOWN() ? 1 : 0);
        delay_ms(300u);
    }
}

void app_touch_scan(void)
{
    static const struct
    {
        uint8_t addr;
        const char *name;
    } known[] = {
        { 0x5Du, "GT911/GT9147" },
        { 0x14u, "GT9147-alt" },
        { 0x38u, "FT5206/FT5x06" },
        { 0x48u, "OTT2001A" },
        { 0x70u, "FT6236" },
    };
    uint8_t i;
    uint8_t found = 0u;

    ct_init();
    dbg_write("touch: I2C scan on PB0(SCL)/PF11(SDA)...\r\n");

    for (i = 0u; i < (uint8_t)(sizeof(known) / sizeof(known[0])); i++)
    {
        if (ct_probe(known[i].addr))
        {
            dbg_printf("touch: found 0x%02X (%s)\r\n",
                       (unsigned)known[i].addr, known[i].name);
            found++;
        }
    }

    /* 顺手看一下 PEN 引脚：电阻屏应有上拉且空闲为高 */
    dbg_printf("touch: scan done, %u device(s); PB1(PEN)=%d PB2(MISO)=%d\r\n",
               (unsigned)found,
               (((GPIOB->IDR) & GPIO_PIN_1) != 0u) ? 1 : 0,
               (((GPIOB->IDR) & GPIO_PIN_2) != 0u) ? 1 : 0);

    /* 还原成电阻触摸的引脚方向，避免影响后续 */
    touch_gpio_init();
}
