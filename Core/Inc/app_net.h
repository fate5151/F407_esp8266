/**
  ******************************************************************************
  * @file    app_net.h
  * @brief   ESP8266（USART3）AT 联网 + 简易 HTTP 服务器，手机浏览器远程控制继电器
  ******************************************************************************
  */

#ifndef __APP_NET_H
#define __APP_NET_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "main.h"

/* 编译期默认 WiFi。留空表示不自动连接，用串口命令现场配置：
     wifi <ssid> <密码>
   注意：别把真实密码写进这里再提交到仓库 */
#define APP_NET_SSID_DEFAULT ""
#define APP_NET_PWD_DEFAULT  ""

#define APP_NET_PORT 80u

typedef enum
{
    APP_NET_IDLE = 0,        /* 未启动 */
    APP_NET_CLEAN,           /* AT+CIPCLOSE=5：关掉上次遗留的 TCP 链路 */
    APP_NET_WAIT_MODULE,     /* 等模块回应 AT */
    APP_NET_SET_MODE,        /* AT+CWMODE=1 */
    APP_NET_JOIN_AP,         /* AT+CWJAP */
    APP_NET_SET_MUX,         /* AT+CIPMUX=1 */
    APP_NET_START_SERVER,    /* AT+CIPSERVER=1,80 */
    APP_NET_GET_IP,          /* AT+CIFSR 取本机 IP */
    APP_NET_RUN,             /* 正常服务中 */
    APP_NET_SEND_PROMPT,     /* 等 '>' 提示符 */
    APP_NET_SEND_DATA,       /* 数据已发出，等 SEND OK */
    APP_NET_FAILED           /* 出错，10 s 后重来 */
} app_net_state_t;

/* 启动 USART3 中断接收；若头文件里配了默认 SSID 就自动开始连接 */
void app_net_init(void);

/* 主循环 10 ms 时基调用 */
void app_net_task(void);

/* 用给定账号连接并开服务器，返回 1 表示已开始 */
uint8_t app_net_start(const char *ssid, const char *pwd);

/* 关服务器并停下状态机 */
void app_net_stop(void);

app_net_state_t app_net_state(void);
const char *app_net_state_name(app_net_state_t st);

/* 拿到的 IP，未拿到时返回 "-" */
const char *app_net_ip(void);

/* 服务器已就绪（可以访问网页） */
uint8_t app_net_running(void);

/* 调试用：把一条原始 AT 命令发给模块，并把 1.5 s 内的回显打到 USART1 */
void app_net_raw(const char *cmd);

/* USART3 中断转发，由 HAL_UART_RxCplt/ErrorCallback 调用 */
void app_net_rx_complete(void);
void app_net_rx_error(UART_HandleTypeDef *huart);

#ifdef __cplusplus
}
#endif

#endif /* __APP_NET_H */
