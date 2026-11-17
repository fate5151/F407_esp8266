/**
  ******************************************************************************
  * @file    app_net.c
  * @brief   ESP8266（ATK-MW8266D，USART3 115200）TCP 服务器 + 简易 HTTP 控制页
  *
  * 启动序列（10 ms 时间片轮询，全程非阻塞）：
  *   AT → AT+CWMODE=1 → AT+CWJAP="ssid","pwd" → AT+CIPMUX=1
  *   → AT+CIPSERVER=1,80 → AT+CIFSR → 运行
  *
  * 运行：解析 "+IPD,<link>,<len>:<HTTP 请求>"，回 302 重定向或状态页
  *   GET /                    状态页（3 s 自动刷新）
  *   GET /relay?ch=1&on=1     单路开/关
  *   GET /toggle?ch=1         单路翻转
  *   GET /all?m=63            按位掩码设置 6 路
  *   GET /th?v=150            报警阈值
  ******************************************************************************
  */

#include "app_net.h"
#include "app_cfg.h"
#include "app_dbg.h"
#include "app_dust.h"
#include "app_relay.h"
#include "app_util.h"
#include "usart.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define NET_RING_SIZE  256u        /* USART3 中断环形缓冲，必须是 2 的幂 */
#define NET_STREAM_MAX 512u        /* AT 解析流缓冲 */
#define NET_TX_MAX     2048u       /* HTTP 响应缓冲（CIPSEND 单包上限 2048） */
#define NET_CMD_MAX    128u
#define NET_RETRY_MAX  3u          /* 单条命令重试次数 */
#define NET_BOOT_RETRY 5u          /* 开机等模块就绪的次数 */
#define NET_RESET_MS   10000u      /* 彻底失败后重来的间隔 */
#define NET_REQ_MAX    192u        /* 一次只关心请求行，够用 */

static volatile uint8_t  s_ring[NET_RING_SIZE];
static volatile uint16_t s_ring_head;
static volatile uint16_t s_ring_tail;
static uint8_t s_rx_byte;

static char     s_stream[NET_STREAM_MAX];
static uint16_t s_stream_len;

static char     s_cmd[NET_CMD_MAX];
static char     s_tx[NET_TX_MAX];
static uint16_t s_tx_len;

static app_net_state_t s_state;
static uint8_t  s_started;
static uint8_t  s_waiting;            /* 命令已发出，正在等回应 */
static uint8_t  s_retry;
static uint32_t s_deadline;
static char     s_ssid[33];
static char     s_pwd[65];
static char     s_ip[20];
static uint8_t  s_link;               /* 当前处理的 TCP 连接号 */
static uint32_t s_ipd_skip;           /* 当前 +IPD 帧里还没丢弃的字节数 */
static uint16_t s_mark;               /* 发 CIPSEND 时流缓冲的长度，用来保护已收到的客户端数据 */
static uint32_t s_raw_until;          /* 调试透传的结束时刻，0 = 未启用 */

#define NET_NOT_FOUND 0xFFFFu

static void net_issue(const char *cmd, uint32_t timeout_ms);
static void net_enter(app_net_state_t st);
static void net_send_close(void);
static void net_parse_ipd(void);

/* ---------------------------------------------------------------- TCP 发送 */

static void net_send(const char *s)
{
    size_t len = strlen(s);

    if (len > 0u)
    {
        (void)HAL_UART_Transmit(&huart3, (uint8_t *)s, (uint16_t)len, 200u);
    }
}

static void tx_reset(void)
{
    s_tx_len = 0u;
    s_tx[0] = '\0';
}

static void tx_add(const char *fmt, ...)
{
    va_list ap;
    int w;

    if (s_tx_len >= (NET_TX_MAX - 1u))
    {
        return;
    }

    va_start(ap, fmt);
    w = vsnprintf(&s_tx[s_tx_len], (size_t)(NET_TX_MAX - s_tx_len), fmt, ap);
    va_end(ap);

    if (w > 0)
    {
        s_tx_len = (uint16_t)(s_tx_len + (uint16_t)w);
        if (s_tx_len >= NET_TX_MAX)
        {
            s_tx_len = NET_TX_MAX - 1u;
        }
    }
}

/* ------------------------------------------------------------ AT 流缓冲 */

static void stream_clear(void)
{
    s_stream_len = 0u;
    s_stream[0] = '\0';
}

static void stream_append(const char *buf, uint16_t n)
{
    uint16_t room = (uint16_t)(NET_STREAM_MAX - 1u - s_stream_len);

    if (n > room)
    {
        n = room;                       /* 缓冲满了就丢新数据 */
    }

    if (n > 0u)
    {
        memcpy(&s_stream[s_stream_len], buf, n);
        s_stream_len = (uint16_t)(s_stream_len + n);
        s_stream[s_stream_len] = '\0';
    }
}

static void stream_drop(uint16_t n)
{
    if (n >= s_stream_len)
    {
        stream_clear();
        return;
    }

    memmove(s_stream, &s_stream[n], (size_t)(s_stream_len - n));
    s_stream_len = (uint16_t)(s_stream_len - n);
    s_stream[s_stream_len] = '\0';
}

static uint8_t net_has(const char *tok)
{
    return (strstr(s_stream, tok) != NULL) ? 1u : 0u;
}

/* 删掉流缓冲里 [from, to) 这一段，后面的数据前移 */
static void stream_drop_range(uint16_t from, uint16_t to)
{
    if (to > s_stream_len)
    {
        to = s_stream_len;
    }
    if (from >= to)
    {
        return;
    }

    memmove(&s_stream[from], &s_stream[to], (size_t)(s_stream_len - to));
    s_stream_len = (uint16_t)(s_stream_len - (to - from));
    s_stream[s_stream_len] = '\0';
}

/* 在 mark 之后找 tok，返回绝对偏移；找不到返回 NET_NOT_FOUND */
static uint16_t net_find_after(uint16_t mark, const char *tok)
{
    const char *p;

    if (mark >= s_stream_len)
    {
        return NET_NOT_FOUND;
    }

    p = strstr(s_stream + mark, tok);
    return (p == NULL) ? NET_NOT_FOUND : (uint16_t)(p - s_stream);
}

/* 把 mark 之后到 tok 结尾（含 CRLF）的 AT 回显删掉，保留其他数据 */
static void net_drop_response(uint16_t mark, uint16_t tok_off, size_t tok_len)
{
    uint16_t to;

    if (tok_off == NET_NOT_FOUND)
    {
        stream_drop_range(mark, s_stream_len);
        return;
    }

    to = (uint16_t)(tok_off + tok_len);
    while (to < s_stream_len && (s_stream[to] == '\r' || s_stream[to] == '\n'))
    {
        to++;
    }
    stream_drop_range(mark, to);
}

/* 把 ISR 环形缓冲一次性搬完（每次 10 ms 最多进 115 字节，缓冲区够） */
static void net_drain(void)
{
    char chunk[64];

    while (s_ring_tail != s_ring_head)
    {
        uint16_t n = 0u;

        if (s_stream_len >= (NET_STREAM_MAX - 1u))
        {
            s_ring_tail = s_ring_head;      /* 流缓冲满：丢掉剩余，避免死循环 */
            break;
        }

        while (s_ring_tail != s_ring_head && n < (uint16_t)sizeof(chunk))
        {
            chunk[n++] = (char)s_ring[s_ring_tail];
            s_ring_tail = (uint16_t)((s_ring_tail + 1u) & (NET_RING_SIZE - 1u));
        }

        stream_append(chunk, n);
    }
}

/* ------------------------------------------------------------- AT 时序 */

static void net_enter(app_net_state_t st)
{
    s_state = st;
    s_waiting = 0u;
    s_retry = 0u;
}

static void net_issue(const char *cmd, uint32_t timeout_ms)
{
    stream_clear();
    net_send(cmd);
    s_deadline = HAL_GetTick() + timeout_ms;
    s_waiting = 1u;
}

/* 发 CIPSEND 专用：不清空流缓冲，只记下当前位置，
   这样已经收到的下一帧客户端数据不会丢 */
static void net_issue_keep(const char *cmd, uint32_t timeout_ms)
{
    s_mark = s_stream_len;
    net_send(cmd);
    s_deadline = HAL_GetTick() + timeout_ms;
    s_waiting = 1u;
}

static uint8_t net_timeout(void)
{
    return ((int32_t)(HAL_GetTick() - s_deadline) >= 0) ? 1u : 0u;
}

static void net_retry_or_fail(void)
{
    if (++s_retry >= NET_RETRY_MAX)
    {
        dbg_write("net: AT sequence failed, retry in 10 s\r\n");
        net_enter(APP_NET_FAILED);
    }
    else
    {
        s_waiting = 0u;
    }
}

static void net_send_close(void)
{
    (void)snprintf(s_cmd, sizeof(s_cmd), "AT+CIPCLOSE=%u\r\n", (unsigned)s_link);
    net_send(s_cmd);
}

/* 发 CIPCLOSE 并等它的 OK 再回 RUN，否则新连接会撞上未释放的 link */
static void net_begin_close(void)
{
    s_mark = s_stream_len;          /* 保护已到达的下一帧数据 */
    net_send_close();
    s_deadline = HAL_GetTick() + 600u;
    s_waiting = 1u;
    s_state = APP_NET_CLOSING;
}

/* ------------------------------------------------------------ HTTP 页面 */

static uint8_t query_get(const char *query, const char *key, uint16_t *out)
{
    size_t klen = strlen(key);
    const char *p = query;

    while ((p = strstr(p, key)) != NULL)
    {
        char before = (p == query) ? '?' : p[-1];

        if ((before == '?' || before == '&') && p[klen] == '=')
        {
            /* 只取到下一个 '&'，因为后面可能还有别的参数 */
            char tmp[8];
            const char *v = p + klen + 1u;
            uint8_t i = 0u;

            while (v[i] != '\0' && v[i] != '&' && i < (uint8_t)(sizeof(tmp) - 1u))
            {
                tmp[i] = v[i];
                i++;
            }
            tmp[i] = '\0';

            return app_parse_u16(tmp, out);
        }
        p += klen;
    }

    return 0u;
}

static void tx_http_redirect(void)
{
    tx_add("HTTP/1.1 302 Found\r\nLocation: /\r\nContent-Length: 0\r\n"
           "Cache-Control: no-store\r\nConnection: close\r\n\r\n");
}

/* ---- 页面 body 单独构建，之后按实际长度写 Content-Length ---- */
static char     s_body[NET_TX_MAX - 256u];
static uint16_t s_body_len;

static void body_reset(void)
{
    s_body_len = 0u;
    s_body[0] = '\0';
}

static void body_add(const char *fmt, ...)
{
    va_list ap;
    int w;

    if (s_body_len >= (uint16_t)(sizeof(s_body) - 1u))
    {
        return;
    }

    va_start(ap, fmt);
    w = vsnprintf(&s_body[s_body_len], sizeof(s_body) - s_body_len, fmt, ap);
    va_end(ap);

    if (w > 0)
    {
        s_body_len = (uint16_t)(s_body_len + (uint16_t)w);
        if (s_body_len >= (uint16_t)sizeof(s_body))
        {
            s_body_len = (uint16_t)(sizeof(s_body) - 1u);
        }
    }
}

static void page_build(void)
{
    uint8_t mode = app_relay_mode();
    app_dust_level_t lv = app_dust_level();
    uint8_t i;

    body_reset();
    body_add("<!DOCTYPE html><html><head><meta charset=utf-8>"
             "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
             "<meta http-equiv=refresh content=3><title>F407</title>"
             "<style>body{font-family:sans-serif;margin:16px}"
           "table{border-collapse:collapse}td,th{padding:6px 12px;border:1px solid #ccc}"
           "a{text-decoration:none}.on{background:#e33;color:#fff}.off{background:#eee}"
           ".hi{color:#e33;font-weight:bold}</style></head><body>");
    body_add("<h3>F407 Remote</h3><p>PM <b>%u</b> ug/m3 &nbsp; TH %u &nbsp; "
           "<span class=%s>%s</span></p>",
           (unsigned)app_dust_pm(), (unsigned)app_dust_threshold(),
           (lv == APP_DUST_LEVEL_NORMAL) ? "off" : "hi",
           app_dust_level_name(lv));
    body_add("<table><tr><th>CH</th><th>State</th><th>Action</th></tr>");

    for (i = 0u; i < APP_RELAY_COUNT; i++)
    {
        uint8_t on = (uint8_t)((mode >> i) & 1u);

        body_add("<tr><td>%u</td><td class=%s>%s</td><td><a href=\"/toggle?ch=%u\">%s</a></td></tr>",
               (unsigned)(i + 1u), on ? "on" : "off", on ? "ON" : "OFF",
               (unsigned)(i + 1u), on ? "OFF" : "ON");
    }

        body_add("</table>");
body_add("<p>TH: <a href=\"/th?v=100\">100</a> <a href=\"/th?v=150\">150</a> "
           "<a href=\"/th?v=200\">200</a>");
    body_add("<p><a href=\"/all?m=0\">ALL OFF</a> &nbsp; <a href=\"/all?m=63\">ALL ON</a>");
    body_add("</body></html>");

    /* 头部 + body 拼成完整响应；带 Content-Length 客户端读完即可结束，
       不必等连接关闭（之前没长度，超时就表现为 HTTP 000） */
    tx_reset();
    tx_add("HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
           "Content-Length: %u\r\nCache-Control: no-store\r\n"
           "Connection: close\r\n\r\n", (unsigned)s_body_len);

    if (((uint32_t)s_tx_len + (uint32_t)s_body_len) < NET_TX_MAX)
    {
        memcpy(&s_tx[s_tx_len], s_body, s_body_len);
        s_tx_len = (uint16_t)(s_tx_len + s_body_len);
        s_tx[s_tx_len] = '\0';
    }
}

/* 处理一条 HTTP 请求，结果写进 s_tx */
static void http_handle(const char *data, uint16_t len)
{
    char req[NET_REQ_MAX];
    uint16_t n = (len < (NET_REQ_MAX - 1u)) ? len : (uint16_t)(NET_REQ_MAX - 1u);
    char *path;
    char *end;
    uint16_t v;
    uint16_t w;

    memcpy(req, data, n);
    req[n] = '\0';

    tx_reset();

    if (strncmp(req, "GET ", 4u) != 0)
    {
        tx_http_redirect();
        return;
    }

    path = strchr(req, ' ');
    if (path == NULL)
    {
        tx_http_redirect();
        return;
    }

    path++;                                  /* 跳过 "GET " 后面的空格 */

    end = strchr(path, ' ');                 /* 砍掉 " HTTP/1.1" 和后续头部 */
    if (end == NULL)
    {
        end = strchr(path, '\r');
    }
    if (end != NULL)
    {
        *end = '\0';
    }

    dbg_printf("http: %s\r\n", path);        /* 调试：看手机到底请求了什么 */

    if (path[0] == '/' && path[1] == '\0')
    {
        page_build();
    }
    else if (strncmp(path, "/relay?", 7u) == 0 &&
             query_get(path + 7, "ch", &v) && query_get(path + 7, "on", &w) &&
             v >= 1u && v <= APP_RELAY_COUNT && w <= 1u)
    {
        app_relay_set_channel((uint8_t)v, (uint8_t)w);
        tx_http_redirect();
    }
    else if (strncmp(path, "/toggle?", 8u) == 0 &&
             query_get(path + 8, "ch", &v) && v >= 1u && v <= APP_RELAY_COUNT)
    {
        app_relay_toggle((uint8_t)v);
        tx_http_redirect();
    }
    else if (strncmp(path, "/all?", 5u) == 0 &&
             query_get(path + 5, "m", &v) && v <= 0x3Fu)
    {
        app_relay_set_mode((uint8_t)v);
        tx_http_redirect();
    }
        else if (strncmp(path, "/th?", 4u) == 0 && query_get(path + 4, "v", &v))
    {
        app_dust_set_threshold(v);
        tx_http_redirect();
    }
    else
    {
        tx_http_redirect();                  /* 不认识的路径也回首页 */
    }
}

/* 等模块给出 '>' 后再发数据 */
static void net_start_send(void)
{
    (void)snprintf(s_cmd, sizeof(s_cmd), "AT+CIPSEND=%u,%u\r\n",
                   (unsigned)s_link, (unsigned)s_tx_len);
    net_issue_keep(s_cmd, 2000u);
    s_state = APP_NET_SEND_PROMPT;
    s_waiting = 1u;
}

/* 解析 "+IPD,<link>,<len>:<data>"
 * 只取请求行（到第一个 CRLF）就够用，帧里剩下的请求头按 len 直接丢弃，
 * 这样浏览器几百字节的请求头不会把 512 B 的流缓冲撑爆。 */
static void net_parse_ipd(void)
{
    for (;;)
    {
        char *p;
        char *colon;
        char *eol;
        unsigned link = 0u;
        unsigned len = 0u;
        uint16_t data_off;
        uint16_t line_len;

        /* 1) 先把上一帧剩下的字节丢掉 */
        if (s_ipd_skip > 0u)
        {
            uint32_t drop = ((uint32_t)s_stream_len < s_ipd_skip)
                            ? (uint32_t)s_stream_len : s_ipd_skip;

            stream_drop((uint16_t)drop);
            s_ipd_skip -= drop;
            if (s_ipd_skip > 0u)
            {
                return;                      /* 这一帧还没走完 */
            }
        }

        p = strstr(s_stream, "+IPD,");
        if (p == NULL)
        {
            if (net_has(",CLOSED") || net_has(",CONNECT"))
            {
                if (net_has(",CONNECT"))
                {
                    dbg_write("net: client connect\r\n");
                }
                else
                {
                    dbg_write("net: client closed\r\n");
                }
                stream_clear();
                return;
            }
            if (s_stream_len > 128u)
            {
                stream_clear();              /* 没有 +IPD 的噪声，丢掉免得塞满 */
            }
            return;
        }

        colon = strchr(p, ':');
        if (colon == NULL)
        {
            return;                          /* 头部还没收全 */
        }

        if (sscanf(p + 5, "%u,%u", &link, &len) != 2 || len == 0u || len > 8192u)
        {
            stream_drop((uint16_t)(p - s_stream) + 5u);
            continue;
        }

        data_off = (uint16_t)(colon - s_stream) + 1u;
        eol = strstr(s_stream + data_off, "\r\n");
        if (eol == NULL)
        {
            if (s_stream_len >= (NET_STREAM_MAX - 1u))
            {
                /* 请求行太长，放弃这一帧，按声明长度跳过 */
                stream_drop(data_off);
                s_ipd_skip = len;
            }
            return;                          /* 请求行还没收全 */
        }

        line_len = (uint16_t)((eol - (s_stream + data_off)) + 2u);
        s_link = (uint8_t)link;
        http_handle(s_stream + data_off, line_len);

        /* 这一帧除了请求行还有多少字节（浏览器那几百字节请求头）：
           已经从流里收到的就地删掉，还没到的记到 skip 里后续丢弃，
           这样既不会把 512 B 缓冲撑爆，也不会误删下一帧 */
        {
            uint32_t frame_rest;
            uint32_t in_stream;
            uint32_t drop_now;

            stream_drop((uint16_t)((uint32_t)data_off + (uint32_t)line_len));
            frame_rest = (len > (uint32_t)line_len)
                         ? ((uint32_t)len - (uint32_t)line_len) : 0u;
            in_stream = (uint32_t)s_stream_len;
            drop_now = (frame_rest < in_stream) ? frame_rest : in_stream;
            stream_drop((uint16_t)drop_now);
            s_ipd_skip = frame_rest - drop_now;
        }

        net_start_send();
        return;                              /* 发送交给状态机，回 RUN 再继续 */
    }
}

static void net_parse_ip(void)
{
    const char *p = strstr(s_stream, "STAIP,\"");
    uint8_t i = 0u;

    if (p == NULL)
    {
        return;
    }

    p += 7;
    while (*p != '"' && *p != '\0' && i < (uint8_t)(sizeof(s_ip) - 1u))
    {
        s_ip[i++] = *p++;
    }
    s_ip[i] = '\0';
}

/* ------------------------------------------------------------- 状态机 */

void app_net_task(void)
{
    net_drain();

    /* 调试透传：1.5 s 内把模块回显原样打出来，期间暂停状态机 */
    if (s_raw_until != 0u)
    {
        if ((int32_t)(HAL_GetTick() - s_raw_until) < 0)
        {
            if (s_stream_len > 0u)
            {
                dbg_printf("raw: %s\r\n", s_stream);
                stream_clear();
            }
            return;
        }
        s_raw_until = 0u;
        stream_clear();
    }

    if (!s_started)
    {
        return;
    }

    switch (s_state)
    {
    case APP_NET_CLEAN:
        if (!s_waiting)
        {
            /* CIPMUX=1 时 CIPCLOSE=5 表示关掉全部链路；不支持就忽略回显 */
            net_issue("AT+CIPCLOSE=5\r\n", 800u);
        }
        else if (net_has("OK\r\n") || net_has("ERROR") || net_has("link is not valid") ||
                 net_timeout())
        {
            net_enter(APP_NET_WAIT_MODULE);
        }
        break;

    case APP_NET_WAIT_MODULE:
        if (!s_waiting)
        {
            net_issue("AT\r\n", 1000u);
        }
        else if (net_has("OK\r\n"))
        {
            net_enter(APP_NET_SET_MODE);
        }
        else if (net_has("ERROR") || net_timeout())
        {
            if (++s_retry < NET_BOOT_RETRY)
            {
                s_waiting = 0u;
            }
            else
            {
                dbg_write("net: no module answer on USART3\r\n");
                net_enter(APP_NET_FAILED);
            }
        }
        break;

    case APP_NET_SET_MODE:
        if (!s_waiting)
        {
            net_issue("AT+CWMODE=1\r\n", 2000u);
        }
        else if (net_has("OK\r\n") || net_has("no change"))
        {
            net_enter(APP_NET_JOIN_AP);
        }
        else if (net_has("ERROR") || net_timeout())
        {
            net_retry_or_fail();
        }
        break;

    case APP_NET_JOIN_AP:
        if (!s_waiting)
        {
            (void)snprintf(s_cmd, sizeof(s_cmd), "AT+CWJAP=\"%s\",\"%s\"\r\n",
                           s_ssid, s_pwd);
            net_issue(s_cmd, 25000u);
        }
        else if (net_has("WIFI GOT IP") || net_has("OK\r\n"))
        {
            dbg_printf("net: joined '%s'\r\n", s_ssid);
            net_enter(APP_NET_SET_MUX);
        }
        else if (net_has("FAIL") || net_has("ERROR") || net_timeout())
        {
            /* 已经连着同一个 AP 时再 CWJAP 会报错，这里不拦，继续往下走；
               真正没连上的话后面的 CIFSR 会拿到 0.0.0.0，再回到重试流程 */
            dbg_write("net: join failed, continue anyway\r\n");
            net_enter(APP_NET_SET_MUX);
        }
        break;

    case APP_NET_SET_MUX:
        if (!s_waiting)
        {
            net_issue("AT+CIPMUX=1\r\n", 2000u);
        }
        else if (net_has("OK\r\n"))
        {
            net_enter(APP_NET_START_SERVER);
        }
        else if (net_has("ERROR") || net_timeout())
        {
            net_retry_or_fail();
        }
        break;

    case APP_NET_START_SERVER:
        if (!s_waiting)
        {
            net_issue("AT+CIPSERVER=1,80\r\n", 3000u);
        }
        else if (net_has("OK\r\n") || net_has("no change"))
        {
            net_enter(APP_NET_GET_IP);
        }
        else if (net_has("ERROR") || net_timeout())
        {
            net_retry_or_fail();
        }
        break;

    case APP_NET_GET_IP:
        if (!s_waiting)
        {
            net_issue("AT+CIFSR\r\n", 3000u);
        }
        else if (net_has("STAIP"))
        {
            net_parse_ip();
            if (s_ip[0] == '\0' || strcmp(s_ip, "0.0.0.0") == 0)
            {
                dbg_write("net: no IP yet, retry whole sequence\r\n");
                net_enter(APP_NET_FAILED);
            }
            else
            {
                net_enter(APP_NET_RUN);
                dbg_printf("net: server ready at http://%s/\r\n", s_ip);
            }
        }
        else if (net_has("OK\r\n") || net_timeout())
        {
            net_enter(APP_NET_RUN);
            dbg_write("net: server ready (IP unknown)\r\n");
        }
        break;

    case APP_NET_RUN:
        if (net_has("WIFI DISCONNECT") || net_has("WIFI GOT IP"))
        {
            /* 热点断开（或重新拿到 IP）时重新走一遍连接流程 */
            if (net_has("WIFI DISCONNECT"))
            {
                dbg_write("net: wifi lost, reconnect in 10 s\r\n");
            }
            stream_clear();
            net_enter(APP_NET_FAILED);
        }
        else
        {
            net_parse_ipd();
        }
        break;

    case APP_NET_SEND_PROMPT:
    {
        uint16_t off = net_find_after(s_mark, ">");

        if (off != NET_NOT_FOUND)
        {
            stream_drop_range(s_mark, (uint16_t)(off + 1u));
            s_mark = s_stream_len;
            (void)HAL_UART_Transmit(&huart3, (uint8_t *)s_tx, s_tx_len, 1500u);
            s_deadline = HAL_GetTick() + 3000u;
            s_state = APP_NET_SEND_DATA;
        }
        else if (net_timeout())
        {
            dbg_printf("send: no prompt, module said [%s]\r\n", s_stream + s_mark);
            net_drop_response(s_mark, NET_NOT_FOUND, 0u);
            net_send_close();
            net_enter(APP_NET_RUN);
        }
        break;
    }

    case APP_NET_SEND_DATA:
    {
        uint16_t ok = net_find_after(s_mark, "SEND OK");
        uint16_t fail = net_find_after(s_mark, "SEND FAIL");

        if (ok != NET_NOT_FOUND)
        {
            net_drop_response(s_mark, ok, 7u);
            net_begin_close();       /* 关掉 link，等 OK 再收下一个连接 */
        }
        else if (fail != NET_NOT_FOUND)
        {
            net_drop_response(s_mark, fail, 9u);
            dbg_write("send: SEND FAIL\r\n");
            net_begin_close();
        }
        else if (net_timeout())
        {
            dbg_printf("send: no SEND OK, module said [%s]\r\n", s_stream + s_mark);
            net_drop_response(s_mark, NET_NOT_FOUND, 0u);
            net_begin_close();
        }
        break;
    }

    case APP_NET_CLOSING:
        if (net_has("OK\r\n") || net_has("ERROR") || net_has("link is not valid") ||
            net_timeout())
        {
            net_enter(APP_NET_RUN);
        }
        break;

    case APP_NET_FAILED:
        if (!s_waiting)
        {
            s_deadline = HAL_GetTick() + NET_RESET_MS;
            s_waiting = 1u;
        }
        else if (net_timeout())
        {
            dbg_write("net: retry wifi\r\n");
            net_enter(APP_NET_CLEAN);
            s_started = 1u;
        }
        break;

    case APP_NET_IDLE:
    default:
        break;
    }
}

/* -------------------------------------------------------------- 对外接口 */

void app_net_init(void)
{
    s_ring_head = 0u;
    s_ring_tail = 0u;
    s_ip[0] = '\0';
    s_link = 0u;
    s_ipd_skip = 0u;
    stream_clear();
    tx_reset();
    s_started = 0u;
    net_enter(APP_NET_IDLE);

    (void)HAL_UART_Receive_IT(&huart3, &s_rx_byte, 1u);

    /* 优先用 Flash 里保存的账号（掉电不丢），没有再用编译期默认值 */
    {
        char ssid[APP_CFG_SSID_LEN];
        char pwd[APP_CFG_PWD_LEN];

        if (app_cfg_load_wifi(ssid, pwd))
        {
            dbg_printf("net: stored wifi '%s', auto connect\r\n", ssid);
            (void)app_net_start(ssid, pwd);
        }
        else if (strlen(APP_NET_SSID_DEFAULT) > 0u)
        {
            (void)app_net_start(APP_NET_SSID_DEFAULT, APP_NET_PWD_DEFAULT);
        }
        else
        {
            dbg_write("net: no stored wifi, send 'wifi <ssid> <pwd>'\r\n");
        }
    }
}

uint8_t app_net_start(const char *ssid, const char *pwd)
{
    size_t sl;
    size_t pl;

    if (ssid == NULL || pwd == NULL)
    {
        return 0u;
    }

    sl = strlen(ssid);
    pl = strlen(pwd);
    if (sl == 0u || sl >= sizeof(s_ssid) || pl >= sizeof(s_pwd))
    {
        return 0u;
    }

    memcpy(s_ssid, ssid, sl + 1u);
    memcpy(s_pwd, pwd, pl + 1u);

    /* 存到 Flash，下次上电自动连（内容没变时不会重复擦写） */
    {
        uint8_t r = app_cfg_save_wifi(ssid, pwd);

        if (r == 1u)
        {
            dbg_write("net: wifi saved to flash\r\n");
        }
        else if (r == 0u)
        {
            dbg_write("net: wifi save failed (session only)\r\n");
        }
    }

    s_started = 1u;
    s_ip[0] = '\0';
    s_ipd_skip = 0u;
    stream_clear();
    net_enter(APP_NET_CLEAN);
    dbg_printf("net: connecting to '%s' ...\r\n", s_ssid);
    return 1u;
}

void app_net_stop(void)
{
    net_send("AT+CIPSERVER=0\r\n");
    s_started = 0u;
    s_ip[0] = '\0';
    s_ipd_skip = 0u;
    stream_clear();
    net_enter(APP_NET_IDLE);
}

app_net_state_t app_net_state(void)
{
    return s_state;
}

const char *app_net_state_name(app_net_state_t st)
{
    switch (st)
    {
    case APP_NET_CLEAN:
        return "CLEAN";
    case APP_NET_WAIT_MODULE:
        return "WAIT_AT";
    case APP_NET_SET_MODE:
        return "CWMODE";
    case APP_NET_JOIN_AP:
        return "JOIN_AP";
    case APP_NET_SET_MUX:
        return "CIPMUX";
    case APP_NET_START_SERVER:
        return "SERVER";
    case APP_NET_GET_IP:
        return "GET_IP";
    case APP_NET_RUN:
        return "READY";
    case APP_NET_SEND_PROMPT:
    case APP_NET_SEND_DATA:
    case APP_NET_CLOSING:
        return "SENDING";
    case APP_NET_FAILED:
        return "FAILED";
    default:
        return "IDLE";
    }
}

const char *app_net_ip(void)
{
    return (s_ip[0] != '\0') ? s_ip : "-";
}

uint8_t app_net_running(void)
{
    return (s_state == APP_NET_RUN) ? 1u : 0u;
}

void app_net_raw(const char *cmd)
{
    if (cmd == NULL || *cmd == '\0')
    {
        return;
    }

    stream_clear();
    net_send(cmd);
    net_send("\r\n");
    s_raw_until = HAL_GetTick() + 1500u;
}

/* ------------------------------------------------------- 中断入口转发 */

void app_net_rx_complete(void)
{
    uint16_t next = (uint16_t)((s_ring_head + 1u) & (NET_RING_SIZE - 1u));

    if (next != s_ring_tail)
    {
        s_ring[s_ring_head] = s_rx_byte;
        s_ring_head = next;
    }

    (void)HAL_UART_Receive_IT(&huart3, &s_rx_byte, 1u);
}

void app_net_rx_error(UART_HandleTypeDef *huart)
{
    __HAL_UART_CLEAR_OREFLAG(huart);
    (void)HAL_UART_Receive_IT(&huart3, &s_rx_byte, 1u);
}
