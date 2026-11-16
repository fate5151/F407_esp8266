/**
  ******************************************************************************
  * @file    app_dim.c
  * @brief   两路调光：TIM8 PSC 83 / ARR 999 → 1 kHz，占空比 0~999
  ******************************************************************************
  */

#include "app_dim.h"
#include "tim.h"

/* 与 MX_TIM8_Init() 中的 Period 保持一致 */
#define DIM_ARR 999u

static const uint32_t k_dim_channel[APP_DIM_COUNT] = {
    TIM_CHANNEL_1, TIM_CHANNEL_2
};

static uint8_t s_percent[APP_DIM_COUNT];

void app_dim_init(void)
{
    app_dim_set(1u, 0u);
    app_dim_set(2u, 0u);
}

void app_dim_set(uint8_t ch, uint8_t percent)
{
    if (ch < 1u || ch > APP_DIM_COUNT)
    {
        return;
    }

    if (percent > 100u)
    {
        percent = 100u;
    }

    s_percent[ch - 1u] = percent;
    __HAL_TIM_SET_COMPARE(&htim8, k_dim_channel[ch - 1u],
                          (uint32_t)percent * DIM_ARR / 100u);
}

uint8_t app_dim_get(uint8_t ch)
{
    if (ch < 1u || ch > APP_DIM_COUNT)
    {
        return 0u;
    }

    return s_percent[ch - 1u];
}
