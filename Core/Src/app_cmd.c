/**
  ******************************************************************************
  * @file    app_cmd.c
  * @brief   USART1 命令行：继电器 / 调光 / 阈值 / 状态显示开关
  *
  *    help                  show command list
  *    status                print one status line
  *    mode <0-63|0x00-0x3F> set all 6 relays by bitmask
  *    relay <ch> <0|1>      switch one relay (ch 1-6)
  *    toggle <ch>           flip one relay (ch 1-6)
  *    dim <ch> <0-100>      dimmer duty in percent (ch 1-2)
  *    th <ug/m3>            dust alarm threshold (10-1000)
  *    ui <0|1>              1 Hz status line on/off
  ******************************************************************************
  */

#include "app_cmd.h"
#include "app_dbg.h"
#include "app_dim.h"
#include "app_dust.h"
#include "app_relay.h"
#include "app_ui.h"
#include "usart.h"

#include <string.h>

#define CMD_RX_SIZE   64u    /* 必须是 2 的幂 */
#define CMD_LINE_MAX  32u
#define CMD_ARG_MAX   4u

static volatile uint8_t  s_rx[CMD_RX_SIZE];
static volatile uint16_t s_head;
static volatile uint16_t s_tail;
static uint8_t  s_rx_byte;

static char    s_line[CMD_LINE_MAX];
static uint8_t s_line_len;

/* 支持十进制与 0x 前缀十六进制，越界返回 0 */
static uint8_t parse_u16(const char *s, uint16_t *out)
{
    uint32_t v = 0u;
    uint32_t base = 10u;

    if (s == NULL || *s == '\0')
    {
        return 0u;
    }

    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
    {
        base = 16u;
        s += 2;
    }

    while (*s != '\0')
    {
        uint32_t d;

        if (*s >= '0' && *s <= '9')
        {
            d = (uint32_t)(*s - '0');
        }
        else if (base == 16u && *s >= 'a' && *s <= 'f')
        {
            d = (uint32_t)(*s - 'a') + 10u;
        }
        else if (base == 16u && *s >= 'A' && *s <= 'F')
        {
            d = (uint32_t)(*s - 'A') + 10u;
        }
        else
        {
            return 0u;
        }

        v = v * base + d;
        if (v > 0xFFFFu)
        {
            return 0u;
        }
        s++;
    }

    *out = (uint16_t)v;
    return 1u;
}

static void cmd_help(void)
{
    dbg_write("commands:\r\n"
              "  help                  show this text\r\n"
              "  status                print one status line\r\n"
              "  mode <0-63|0x00-0x3F> set all 6 relays by bitmask\r\n"
              "  relay <ch> <0|1>      switch one relay (ch 1-6)\r\n"
              "  toggle <ch>           flip one relay (ch 1-6)\r\n"
              "  dim <ch> <0-100>      dimmer duty in percent (ch 1-2)\r\n"
              "  th <ug/m3>            dust alarm threshold (10-1000)\r\n"
              "  ui <0|1>              1 Hz status line on/off\r\n");
}

static void cmd_exec(char *line)
{
    char *argv[CMD_ARG_MAX];
    uint8_t argc = 0u;
    char *p = line;
    uint16_t v;
    uint16_t w;

    while (*p != '\0' && argc < CMD_ARG_MAX)
    {
        while (*p == ' ')
        {
            p++;
        }
        if (*p == '\0')
        {
            break;
        }

        argv[argc++] = p;
        while (*p != '\0' && *p != ' ')
        {
            p++;
        }
        if (*p != '\0')
        {
            *p++ = '\0';
        }
    }

    if (argc == 0u)
    {
        return;
    }

    if (strcmp(argv[0], "help") == 0)
    {
        cmd_help();
    }
    else if (strcmp(argv[0], "status") == 0)
    {
        app_ui_print_status();
    }
    else if (strcmp(argv[0], "mode") == 0)
    {
        if (argc < 2u || !parse_u16(argv[1], &v) || v > 0x3Fu)
        {
            dbg_write("ERR mode 0-63\r\n");
            return;
        }
        app_relay_set_mode((uint8_t)v);
        dbg_printf("OK mode=0x%02X\r\n", (unsigned int)app_relay_mode());
    }
    else if (strcmp(argv[0], "relay") == 0)
    {
        if (argc < 3u || !parse_u16(argv[1], &v) || !parse_u16(argv[2], &w) ||
            v < 1u || v > APP_RELAY_COUNT || w > 1u)
        {
            dbg_write("ERR relay <ch 1-6> <0|1>\r\n");
            return;
        }
        app_relay_set_channel((uint8_t)v, (uint8_t)w);
        dbg_printf("OK mode=0x%02X\r\n", (unsigned int)app_relay_mode());
    }
    else if (strcmp(argv[0], "toggle") == 0)
    {
        if (argc < 2u || !parse_u16(argv[1], &v) || v < 1u || v > APP_RELAY_COUNT)
        {
            dbg_write("ERR toggle <ch 1-6>\r\n");
            return;
        }
        app_relay_toggle((uint8_t)v);
        dbg_printf("OK mode=0x%02X\r\n", (unsigned int)app_relay_mode());
    }
    else if (strcmp(argv[0], "dim") == 0)
    {
        if (argc < 3u || !parse_u16(argv[1], &v) || !parse_u16(argv[2], &w) ||
            v < 1u || v > APP_DIM_COUNT || w > 100u)
        {
            dbg_write("ERR dim <ch 1-2> <0-100>\r\n");
            return;
        }
        app_dim_set((uint8_t)v, (uint8_t)w);
        dbg_printf("OK dim%d=%u%%\r\n", (unsigned int)v, (unsigned int)app_dim_get((uint8_t)v));
    }
    else if (strcmp(argv[0], "th") == 0)
    {
        if (argc < 2u || !parse_u16(argv[1], &v))
        {
            dbg_write("ERR th <10-1000>\r\n");
            return;
        }
        app_dust_set_threshold(v);
        dbg_printf("OK th=%u ug/m3\r\n", (unsigned int)app_dust_threshold());
    }
    else if (strcmp(argv[0], "ui") == 0)
    {
        if (argc < 2u || !parse_u16(argv[1], &v) || v > 1u)
        {
            dbg_write("ERR ui <0|1>\r\n");
            return;
        }
        app_ui_enable((uint8_t)v);
        dbg_printf("OK ui=%u\r\n", (unsigned int)v);
    }
    else
    {
        dbg_printf("ERR unknown '%s' (try help)\r\n", argv[0]);
    }
}

void app_cmd_init(void)
{
    s_head = 0u;
    s_tail = 0u;
    s_line_len = 0u;
    (void)HAL_UART_Receive_IT(&huart1, &s_rx_byte, 1u);
}

void app_cmd_task(void)
{
    while (s_tail != s_head)
    {
        char c = (char)s_rx[s_tail];
        s_tail = (uint16_t)((s_tail + 1u) & (CMD_RX_SIZE - 1u));

        if (c == '\r' || c == '\n')
        {
            char echo[3] = { '\r', '\n', '\0' };

            dbg_write(echo);
            if (s_line_len > 0u)
            {
                s_line[s_line_len] = '\0';
                s_line_len = 0u;
                cmd_exec(s_line);
            }
            continue;
        }

        if (c == '\b' || c == 0x7Fu)     /* 退格 */
        {
            if (s_line_len > 0u)
            {
                s_line_len--;
                dbg_write("\b \b");
            }
            continue;
        }

        if (c < 0x20 || c > 0x7E)        /* 丢掉不可打印字符 */
        {
            continue;
        }

        if (s_line_len < (CMD_LINE_MAX - 1u))
        {
            char echo[2];

            s_line[s_line_len++] = c;
            echo[0] = c;
            echo[1] = '\0';
            dbg_write(echo);
        }
    }
}

/* USART1 单字节接收完成：入环形缓冲并重新武装 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        uint16_t next = (uint16_t)((s_head + 1u) & (CMD_RX_SIZE - 1u));

        if (next != s_tail)
        {
            s_rx[s_head] = s_rx_byte;
            s_head = next;
        }

        (void)HAL_UART_Receive_IT(&huart1, &s_rx_byte, 1u);
    }
}

/* 溢出/噪声错误后重新武装接收，避免串口再也收不到数据 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        __HAL_UART_CLEAR_OREFLAG(huart);
        (void)HAL_UART_Receive_IT(&huart1, &s_rx_byte, 1u);
    }
}
