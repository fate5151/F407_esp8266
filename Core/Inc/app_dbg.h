/**
  ******************************************************************************
  * @file    app_dbg.h
  * @brief   调试串口 USART1（PA9/PA10，115200 8N1）输出封装
  ******************************************************************************
  */

#ifndef __APP_DBG_H
#define __APP_DBG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* 预留：调试口固定 USART1，由 MX_USART1_UART_Init() 配置 */
void dbg_write(const char *s);

/* 只支持 %d/%u/%x/%X/%c/%s 这类整数格式，不带浮点 */
void dbg_printf(const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* __APP_DBG_H */
