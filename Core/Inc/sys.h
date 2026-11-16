/**
  ******************************************************************************
  * @file    sys.h
  * @brief   兼容头：正点原子例程里用到的 u8/u16/u32 等类型与位带宏
  *          （我们的工程统一用 stdint，这里只是给 lcd.c 提供旧名字）
  ******************************************************************************
  */

#ifndef __SYS_H
#define __SYS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "main.h"

typedef int32_t  s32;
typedef int16_t  s16;
typedef int8_t   s8;

typedef const int32_t sc32;
typedef const int16_t sc16;
typedef const int8_t  sc8;

typedef __IO int32_t  vs32;
typedef __IO int16_t  vs16;
typedef __IO int8_t   vs8;

typedef __I  int32_t  vsc32;
typedef __I  int16_t  vsc16;
typedef __I  int8_t   vsc8;

typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t  u8;

typedef const uint32_t uc32;
typedef const uint16_t uc16;
typedef const uint8_t  uc8;

typedef __IO uint32_t vu32;
typedef __IO uint16_t vu16;
typedef __IO uint8_t  vu8;

typedef __I  uint32_t vuc32;
typedef __I  uint16_t vuc16;
typedef __I  uint8_t  vuc8;

/* Cortex-M4 位带：ODR 的每个位映射到别名区，PBout(15) = 1 即 PB15 输出高 */
#define BITBAND(addr, bitnum) ((addr & 0xF0000000UL) + 0x2000000UL + \
                               ((addr & 0xFFFFFUL) << 5) + ((bitnum) << 2))
#define MEM_ADDR(addr)        *((volatile uint32_t *)(addr))
#define BIT_ADDR(addr, bitnum) MEM_ADDR(BITBAND(addr, bitnum))

#define GPIOA_ODR_Addr (GPIOA_BASE + 20UL)
#define GPIOB_ODR_Addr (GPIOB_BASE + 20UL)
#define GPIOC_ODR_Addr (GPIOC_BASE + 20UL)
#define GPIOD_ODR_Addr (GPIOD_BASE + 20UL)
#define GPIOE_ODR_Addr (GPIOE_BASE + 20UL)
#define GPIOF_ODR_Addr (GPIOF_BASE + 20UL)
#define GPIOG_ODR_Addr (GPIOG_BASE + 20UL)

#define PAout(n) BIT_ADDR(GPIOA_ODR_Addr, n)
#define PBout(n) BIT_ADDR(GPIOB_ODR_Addr, n)
#define PCout(n) BIT_ADDR(GPIOC_ODR_Addr, n)
#define PDout(n) BIT_ADDR(GPIOD_ODR_Addr, n)
#define PEout(n) BIT_ADDR(GPIOE_ODR_Addr, n)
#define PFout(n) BIT_ADDR(GPIOF_ODR_Addr, n)
#define PGout(n) BIT_ADDR(GPIOG_ODR_Addr, n)

#ifdef __cplusplus
}
#endif

#endif /* __SYS_H */
