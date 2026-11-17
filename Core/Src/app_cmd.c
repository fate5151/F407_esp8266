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
  *    th <ug/m3>            dust alarm threshold (10-1000)
 *    dust [poll]           show raw ADC / voltage / PM
  *    ui <0|1>              1 Hz status line on/off
  ******************************************************************************
  */

#include "app_cmd.h"
#include "app_cfg.h"
#include "app_dbg.h"
#include "app_dust.h"
#include "app_lcd.h"
#include "app_net.h"
#include "app_relay.h"
#include "app_touch.h"
#include "app_ui.h"
#include "app_util.h"
#include "usart.h"

#include <string.h>

#define CMD_RX_SIZE   128u   /* 必须是 2 的幂 */
#define CMD_LINE_MAX  96u    /* 一行最长 95 字符（wifi 命令要带引号 SSID + 密码） */
#define CMD_ARG_MAX   4u

static volatile uint8_t  s_rx[CMD_RX_SIZE];
static volatile uint16_t s_head;
static volatile uint16_t s_tail;
static uint8_t  s_rx_byte;

static char    s_line[CMD_LINE_MAX];
static uint8_t s_line_len;

/* blink <ch>：让某一路以 1 Hz 点动，用来听声音/量通断，blink 0 停止 */
static uint8_t  s_blink_ch;
static uint32_t s_blink_tick;

static void cmd_help(void)
{
    dbg_write("commands:\r\n"
              "  help                  show this text\r\n"
              "  status                print one status line\r\n"
              "  mode <0-63|0x00-0x3F> set all 6 relays by bitmask\r\n"
              "  relay <ch> <0|1>      switch one relay (ch 1-6)\r\n"
              "  toggle <ch>           flip one relay (ch 1-6)\r\n"
              "  th <ug/m3>            dust alarm threshold (10-1000)\r\n"
              "  dust [poll]           show raw ADC / voltage / PM (poll = 10 s)\r\n"
              "  ui <0|1>              1 Hz status line on/off\r\n"
              "  blink <ch>            toggle one relay at 1 Hz (0 = stop)\r\n"
              "  relaypol <0|1>        0=high-trigger, 1=low-trigger module\r\n"
              "  wifi <ssid> <pwd>     join AP, start HTTP server (saved to flash)\r\n"
              "                        (put SSID in \"quotes\" if it has spaces)\r\n"
              "  net <stop|status>     stop network / show state and IP\r\n"
              "  forget                erase saved wifi from flash\r\n"
              "  touch <cal|raw|scan|info>  calibrate / dump raw / scan I2C / state\r\n"
              "  at <AT+...>           send raw AT to ESP8266 (debug)\r\n");
}

static void cmd_exec(char *line)
{
    char *argv[CMD_ARG_MAX];
    uint8_t argc = 0u;
    char *p = line;
    uint16_t v;
    uint16_t w;

    /* at <原始 AT 文本>：整行透传给 ESP8266，不再分词 */
    if (strncmp(line, "at ", 3u) == 0)
    {
        app_net_raw(line + 3u);
        dbg_write("OK raw sent (response shown for 1.5 s)\r\n");
        return;
    }

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

        if (*p == '"')                       /* "SSID 含空格" 这种写法 */
        {
            p++;
            argv[argc++] = p;
            while (*p != '\0' && *p != '"')
            {
                p++;
            }
        }
        else
        {
            argv[argc++] = p;
            while (*p != '\0' && *p != ' ')
            {
                p++;
            }
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

    if (strcmp(argv[0], "at") == 0)
    {
        dbg_write("ERR at <AT+...>\r\n");
    }
    else if (strcmp(argv[0], "help") == 0)
    {
        cmd_help();
    }
    else if (strcmp(argv[0], "status") == 0)
    {
        app_ui_print_status();
    }
    else if (strcmp(argv[0], "mode") == 0)
    {
        if (argc < 2u || !app_parse_u16(argv[1], &v) || v > 0x3Fu)
        {
            dbg_write("ERR mode 0-63\r\n");
            return;
        }
        app_relay_set_mode((uint8_t)v);
        dbg_printf("OK mode=0x%02X\r\n", (unsigned int)app_relay_mode());
    }
    else if (strcmp(argv[0], "relay") == 0)
    {
        if (argc < 3u || !app_parse_u16(argv[1], &v) || !app_parse_u16(argv[2], &w) ||
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
        if (argc < 2u || !app_parse_u16(argv[1], &v) || v < 1u || v > APP_RELAY_COUNT)
        {
            dbg_write("ERR toggle <ch 1-6>\r\n");
            return;
        }
        app_relay_toggle((uint8_t)v);
        dbg_printf("OK mode=0x%02X\r\n", (unsigned int)app_relay_mode());
    }
        else if (strcmp(argv[0], "th") == 0)
    {
        if (argc < 2u || !app_parse_u16(argv[1], &v))
        {
            dbg_write("ERR th <10-1000>\r\n");
            return;
        }
        app_dust_set_threshold(v);
        dbg_printf("OK th=%u ug/m3\r\n", (unsigned int)app_dust_threshold());
    }
    else if (strcmp(argv[0], "ui") == 0)
    {
        if (argc < 2u || !app_parse_u16(argv[1], &v) || v > 1u)
        {
            dbg_write("ERR ui <0|1>\r\n");
            return;
        }
        app_ui_enable((uint8_t)v);
        dbg_printf("OK ui=%u\r\n", (unsigned int)v);
    }
    else if (strcmp(argv[0], "dust") == 0)
    {
        if (argc >= 2u && strcmp(argv[1], "poll") == 0)
        {
            uint32_t end = HAL_GetTick() + 10000u;

            dbg_write("dust: polling 10 s (blow on the sensor now)\r\n");
            while ((int32_t)(HAL_GetTick() - end) < 0)
            {
                uint16_t raw = app_dust_raw();
                uint32_t mv = (uint32_t)raw * 3300u / 4095u;

                dbg_printf("dust: raw=%4u adc=%4lumV Vo=%4lumV PM=%u\r\n",
                           (unsigned)raw, (unsigned long)mv,
                           (unsigned long)(mv * 2u), (unsigned)app_dust_pm());
                HAL_Delay(400u);
            }
        }
        else
        {
            uint16_t raw = app_dust_raw();
            uint32_t mv = (uint32_t)raw * 3300u / 4095u;

            dbg_printf("DUST raw=%u adc=%lumV Vo=%lumV PM=%u TH=%u LV=%s\r\n",
                       (unsigned)raw, (unsigned long)mv, (unsigned long)(mv * 2u),
                       (unsigned)app_dust_pm(), (unsigned)app_dust_threshold(),
                       app_dust_level_name(app_dust_level()));
        }
    }
    else if (strcmp(argv[0], "blink") == 0)
    {
        if (argc < 2u || !app_parse_u16(argv[1], &v) || v > APP_RELAY_COUNT)
        {
            dbg_write("ERR blink <ch 0-6>\r\n");
            return;
        }
        s_blink_ch = (uint8_t)v;
        s_blink_tick = HAL_GetTick();
        if (s_blink_ch == 0u)
        {
            dbg_write("OK blink off\r\n");
        }
        else
        {
            dbg_printf("OK blink CH%u at 1 Hz (blink 0 to stop)\r\n", (unsigned)s_blink_ch);
        }
    }
    else if (strcmp(argv[0], "relaypol") == 0)
    {
        if (argc < 2u || !app_parse_u16(argv[1], &v) || v > 1u)
        {
            dbg_write("ERR relaypol <0=high-trigger|1=low-trigger>\r\n");
            return;
        }
        app_relay_set_polarity((uint8_t)v);
        dbg_printf("OK relay polarity=%s\r\n", v ? "LOW" : "HIGH");
    }
    else if (strcmp(argv[0], "forget") == 0)
    {
        app_cfg_erase_wifi();
        dbg_write("OK saved wifi erased\r\n");
    }
    else if (strcmp(argv[0], "touch") == 0)
    {
        if (argc < 2u)
        {
            dbg_write("ERR touch <cal|raw|info>\r\n");
            return;
        }
        if (strcmp(argv[1], "cal") == 0)
        {
            dbg_write("OK tap the 4 crosses...\r\n");
            app_touch_calibrate();
        }
        else if (strcmp(argv[1], "raw") == 0)
        {
            app_touch_report();
        }
        else if (strcmp(argv[1], "scan") == 0)
        {
            app_touch_scan();
        }
        else if (strcmp(argv[1], "poll") == 0)
        {
            app_touch_poll(10u);
        }
        else if (strcmp(argv[1], "info") == 0)
        {
            dbg_printf("TOUCH cal=%u lcd=%ux%u id=0x%04X\r\n",
                       (unsigned)app_touch_calibrated(),
                       (unsigned)app_lcd_width(), (unsigned)app_lcd_height(),
                       (unsigned)app_lcd_id());
        }
        else
        {
            dbg_write("ERR touch <cal|raw|info>\r\n");
        }
    }
    else if (strcmp(argv[0], "wifi") == 0)
    {
        if (argc < 3u)
        {
            dbg_write("ERR wifi <ssid> <pwd>\r\n");
            return;
        }
        if (!app_net_start(argv[1], argv[2]))
        {
            dbg_write("ERR empty or too long ssid/pwd\r\n");
            return;
        }
        dbg_write("OK connecting...\r\n");
    }
    else if (strcmp(argv[0], "net") == 0)
    {
        if (argc < 2u)
        {
            dbg_write("ERR net <stop|status> (start with: wifi <ssid> <pwd>)\r\n");
            return;
        }
        if (strcmp(argv[1], "stop") == 0)
        {
            app_net_stop();
            dbg_write("OK net stopped\r\n");
        }
        else if (strcmp(argv[1], "status") == 0)
        {
            dbg_printf("NET=%s IP=%s\r\n", app_net_state_name(app_net_state()),
                       app_net_ip());
        }
        else
        {
            dbg_write("ERR net <stop|status> (start with: wifi <ssid> <pwd>)\r\n");
        }
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
    s_blink_ch = 0u;
    s_blink_tick = HAL_GetTick();
    (void)HAL_UART_Receive_IT(&huart1, &s_rx_byte, 1u);
}

void app_cmd_task(void)
{
    if (s_blink_ch != 0u)
    {
        uint32_t now = HAL_GetTick();

        if ((now - s_blink_tick) >= 500u)
        {
            s_blink_tick = now;
            app_relay_toggle(s_blink_ch);
        }
    }

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

/* USART1 单字节接收完成：入环形缓冲并重新武装；USART3 交给 app_net */
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
    else
    {
        app_net_rx_complete();
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
    else
    {
        app_net_rx_error(huart);
    }
}
