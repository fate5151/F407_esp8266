/**
  ******************************************************************************
  * @file    delay.c
  * @brief   us 级延时用 DWT 周期计数器实现，不占 SysTick（SysTick 留给 HAL 时基），
  *          所以中断可以正常响应；ms 级直接用 HAL_Delay
  ******************************************************************************
  */

#include "delay.h"
#include "main.h"

#define DWT_CTRL   (*(volatile uint32_t *)0xE0001000UL)
#define DWT_CYCCNT (*(volatile uint32_t *)0xE0001004UL)
#define DEMCR      (*(volatile uint32_t *)0xE000EDFCUL)
#define DEMCR_TRCENA (1UL << 24)
#define DWT_CTRL_CYCCNTENA (1UL << 0)

static uint32_t s_cycles_per_us;

void delay_init(void)
{
    DEMCR |= DEMCR_TRCENA;
    DWT_CYCCNT = 0u;
    DWT_CTRL |= DWT_CTRL_CYCCNTENA;
    s_cycles_per_us = SystemCoreClock / 1000000u;
}

void delay_us(uint32_t us)
{
    uint32_t start = DWT_CYCCNT;
    uint32_t ticks;

    if (s_cycles_per_us == 0u)
    {
        delay_init();
    }

    ticks = us * s_cycles_per_us;
    while ((DWT_CYCCNT - start) < ticks)
    {
    }
}

void delay_ms(uint32_t ms)
{
    HAL_Delay(ms);
}
