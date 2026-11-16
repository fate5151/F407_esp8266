/**
  ******************************************************************************
  * @file    app_dust.c
  * @brief   粉尘传感器：ADC1_IN0 由 TIM3_TRGO 每 10 ms 触发一次，
  *          采样窗口 = 320 us（PA6 高电平），采样点在 280 us 处
  ******************************************************************************
  */

#include "app_dust.h"
#include "adc.h"
#include "app_dbg.h"
#include "main.h"

#define DUST_WIN_LEN       9u        /* 中位数窗口，抑制气流毛刺 */
#define DUST_ALARM_COUNT   3u        /* 连续 3 次超阈值才进 ALARM */
#define DUST_RELEASE_MS    30000u    /* 低于解除线持续 30 s 才解除 */
#define DUST_RELEASE_PCT   80u       /* 解除线 = 阈值的 80% */

/* 本工程的裁剪版 CMSIS 头里没有 ADC_CR2_CAL，按 RM0090 补上（bit17） */
#define DUST_ADC_CR2_CAL (0x1UL << 17)

static uint16_t s_win[DUST_WIN_LEN];
static uint8_t  s_win_n;
static uint8_t  s_win_head;

static uint16_t s_raw;
static uint16_t s_pm;
static uint16_t s_th = APP_DUST_TH_DEFAULT;

static app_dust_level_t s_level;
static uint8_t  s_over_cnt;
static uint32_t s_under_ms;
static uint32_t s_last_tick;

/* 9 点中位数：窗口很小，插入排序足够 */
static uint16_t median_push(uint16_t v)
{
    uint16_t t[DUST_WIN_LEN];
    uint8_t i;

    s_win[s_win_head] = v;
    s_win_head = (uint8_t)((s_win_head + 1u) % DUST_WIN_LEN);
    if (s_win_n < DUST_WIN_LEN)
    {
        s_win_n++;
    }

    for (i = 0u; i < s_win_n; i++)
    {
        t[i] = s_win[i];
    }

    for (i = 1u; i < s_win_n; i++)
    {
        uint16_t key = t[i];
        uint8_t j = i;

        while (j > 0u && t[j - 1u] > key)
        {
            t[j] = t[j - 1u];
            j--;
        }
        t[j] = key;
    }

    return t[s_win_n / 2u];
}

/* PM(ug/m3) = (Vo_mV − 900) × 0.2，Vo 由 0.5 分压还原 */
static uint16_t raw_to_pm(uint16_t raw)
{
    uint32_t mv = (uint32_t)raw * 3300u / 4095u;   /* 分压后 mV */
    uint32_t vo = mv * 2u;                         /* 传感器输出 mV */

    if (vo <= 900u)
    {
        return 0u;
    }

    return (uint16_t)((vo - 900u) * 2u / 10u);
}

static void level_update(uint16_t pm, uint32_t dt_ms)
{
    uint16_t release = (uint16_t)((uint32_t)s_th * DUST_RELEASE_PCT / 100u);

    if (pm >= s_th)
    {
        if (s_over_cnt < DUST_ALARM_COUNT)
        {
            s_over_cnt++;
        }
    }
    else
    {
        s_over_cnt = 0u;
    }

    switch (s_level)
    {
    case APP_DUST_LEVEL_ALARM:
        if (pm < release)
        {
            s_under_ms += dt_ms;
            if (s_under_ms >= DUST_RELEASE_MS)
            {
                s_level = APP_DUST_LEVEL_NORMAL;
                s_under_ms = 0u;
            }
        }
        else
        {
            s_under_ms = 0u;
        }
        break;

    case APP_DUST_LEVEL_WARN:
        if (s_over_cnt >= DUST_ALARM_COUNT)
        {
            s_level = APP_DUST_LEVEL_ALARM;
        }
        else if (pm < (uint16_t)(s_th / 2u))
        {
            s_level = APP_DUST_LEVEL_NORMAL;
        }
        break;

    default:
        if (s_over_cnt >= DUST_ALARM_COUNT)
        {
            s_level = APP_DUST_LEVEL_ALARM;
        }
        else if (pm >= (uint16_t)(s_th / 2u))
        {
            s_level = APP_DUST_LEVEL_WARN;
        }
        break;
    }
}

void app_dust_init(void)
{
    uint8_t i;

    for (i = 0u; i < DUST_WIN_LEN; i++)
    {
        s_win[i] = 0u;
    }

    s_win_n = 0u;
    s_win_head = 0u;
    s_raw = 0u;
    s_pm = 0u;
    s_level = APP_DUST_LEVEL_NORMAL;
    s_over_cnt = 0u;
    s_under_ms = 0u;
    s_last_tick = HAL_GetTick();
}

/* ADC 自校准。本工程 HAL 版本没有 HAL_ADCEx_Calibration_Start()，
   这里按 RM0090 手动置位 CAL；超时就放弃（用出厂校准），绝不阻塞启动 */
void app_dust_calibrate(void)
{
    uint32_t start;

    /* 校准要求 ADON = 1 且已稳定：HAL_ADC_Init() 不打开 ADC，这里先使能 */
    if ((hadc1.Instance->CR2 & ADC_CR2_ADON) == 0u)
    {
        __HAL_ADC_ENABLE(&hadc1);
    }
    HAL_Delay(1u);                     /* tSTAB */

    hadc1.Instance->CR2 |= DUST_ADC_CR2_CAL;
    start = HAL_GetTick();

    while ((hadc1.Instance->CR2 & DUST_ADC_CR2_CAL) != 0u)
    {
        if ((HAL_GetTick() - start) > 20u)
        {
            hadc1.Instance->CR2 &= ~DUST_ADC_CR2_CAL;
            dbg_write("adc: calibration timeout, using factory trim\r\n");
            break;
        }
    }
}

void app_dust_task(void)
{
    uint32_t now = HAL_GetTick();
    uint32_t dt = now - s_last_tick;

    s_last_tick = now;
    if (dt > 100u)                 /* 主循环被长时间阻塞时不让计时器飞掉 */
    {
        dt = 100u;
    }

    if (__HAL_ADC_GET_FLAG(&hadc1, ADC_FLAG_EOC))
    {
        s_raw = (uint16_t)HAL_ADC_GetValue(&hadc1);   /* 读 DR 会清 EOC */
        __HAL_ADC_CLEAR_FLAG(&hadc1, ADC_FLAG_EOC);
        s_pm = raw_to_pm(median_push(s_raw));
    }

    /* 上一次转换没被及时读走会置 OVR，读 DR 已清；这里兜底再把标志清干净 */
    if (__HAL_ADC_GET_FLAG(&hadc1, ADC_FLAG_OVR))
    {
        __HAL_ADC_CLEAR_FLAG(&hadc1, ADC_FLAG_OVR);
    }

    level_update(s_pm, dt);
}

uint16_t app_dust_pm(void)
{
    return s_pm;
}

uint16_t app_dust_raw(void)
{
    return s_raw;
}

app_dust_level_t app_dust_level(void)
{
    return s_level;
}

uint16_t app_dust_threshold(void)
{
    return s_th;
}

void app_dust_set_threshold(uint16_t th)
{
    if (th < 10u)
    {
        th = 10u;
    }
    if (th > 1000u)
    {
        th = 1000u;
    }

    s_th = th;
}

const char *app_dust_level_name(app_dust_level_t lv)
{
    switch (lv)
    {
    case APP_DUST_LEVEL_ALARM:
        return "ALARM";
    case APP_DUST_LEVEL_WARN:
        return "WARN";
    default:
        return "NORMAL";
    }
}
