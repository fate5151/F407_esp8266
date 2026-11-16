/**
  ******************************************************************************
  * @file    app_util.h
  * @brief   小工具：字符串转数字（命令行与 HTTP 参数共用）
  ******************************************************************************
  */

#ifndef __APP_UTIL_H
#define __APP_UTIL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* 支持十进制与 0x 前缀十六进制；遇到非数字或超过 0xFFFF 返回 0 */
uint8_t app_parse_u16(const char *s, uint16_t *out);

#ifdef __cplusplus
}
#endif

#endif /* __APP_UTIL_H */
