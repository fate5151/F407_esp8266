/**
  ******************************************************************************
  * @file    app_util.c
  ******************************************************************************
  */

#include "app_util.h"

#include <stddef.h>

uint8_t app_parse_u16(const char *s, uint16_t *out)
{
    uint32_t v = 0u;
    uint32_t base = 10u;

    if (s == NULL || out == NULL || *s == '\0')
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
