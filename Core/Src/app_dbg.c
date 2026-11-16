/**
  ******************************************************************************
  * @file    app_dbg.c
  * @brief   调试串口输出：阻塞发送，主循环与命令回显共用
  ******************************************************************************
  */

#include "app_dbg.h"
#include "usart.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define DBG_TX_TIMEOUT_MS 20u

void dbg_write(const char *s)
{
    size_t len;

    if (s == NULL)
    {
        return;
    }

    len = strlen(s);
    if (len > 0u)
    {
        (void)HAL_UART_Transmit(&huart1, (uint8_t *)s, (uint16_t)len, DBG_TX_TIMEOUT_MS);
    }
}

void dbg_printf(const char *fmt, ...)
{
    char buf[160];
    va_list ap;

    va_start(ap, fmt);
    (void)vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    dbg_write(buf);
}

/* syscalls.c 里 _write() 调用的弱符号，重定向后 printf 也走 USART1 */
int __io_putchar(int ch)
{
    uint8_t c = (uint8_t)ch;

    (void)HAL_UART_Transmit(&huart1, &c, 1u, DBG_TX_TIMEOUT_MS);
    return ch;
}
