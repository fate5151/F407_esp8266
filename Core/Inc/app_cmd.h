/**
  ******************************************************************************
  * @file    app_cmd.h
  * @brief   USART1 行命令解析（中断接收 + 环形缓冲）
  ******************************************************************************
  */

#ifndef __APP_CMD_H
#define __APP_CMD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* 启动 USART1 单字节中断接收 */
void app_cmd_init(void);

/* 主循环 10 ms 时基调用：把收到的字符拼成一行并执行 */
void app_cmd_task(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_CMD_H */
