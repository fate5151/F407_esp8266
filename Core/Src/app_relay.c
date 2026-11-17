/**
  ******************************************************************************
  * @file    app_relay.c
  * @brief   6 路继电器驱动，PC0~PC5，推挽输出，高电平吸合
  ******************************************************************************
  */

#include "app_relay.h"
#include "main.h"

/* 触发极性存 Flash sector 9 @ 0x080A0000（程序只占 0~4 扇区） */
#define RELAY_CFG_ADDR   0x080A0000u
#define RELAY_CFG_SECTOR FLASH_SECTOR_9
#define RELAY_CFG_MAGIC  0x52504F4Cu   /* "RPOL" */

/* 继电器状态日志：sector 6 @ 0x08040000（128 KB 全空着，程序要涨到 256 KB 才会撞上）。
   每次变化只在空闲位置追加一个 word（几十 us），不像擦扇区那样卡住主循环 1 s，
   所以触摸/网页每点一次都能立刻落盘。写满 32768 条才擦一次。 */
#define RELAY_ST_ADDR    0x08040000u
#define RELAY_ST_SECTOR  FLASH_SECTOR_6
#define RELAY_ST_WORDS   (128u * 1024u / 4u)
#define RELAY_ST_TAG     0xB3B30000u
#define RELAY_ST_KEEP    0xFFFFFFC0u   /* 低 6 位是状态，高位是标签 */

typedef struct
{
    uint32_t magic;
    uint32_t active_low;
    uint32_t sum;
} relay_cfg_t;

static uint8_t  s_mode;          /* 当前 6 路状态位掩码 */
static uint8_t  s_active_low;    /* 1 = 低电平吸合 */
static uint8_t  s_log_ready;     /* 状态日志已扫描过（app_relay_init 之后才允许写） */
static uint32_t s_log_idx;       /* 状态日志下一个空闲 word 的下标 */

static uint8_t relay_cfg_load(void)
{
    const relay_cfg_t *c = (const relay_cfg_t *)RELAY_CFG_ADDR;

    if (c->magic != RELAY_CFG_MAGIC)
    {
        return 0u;
    }
    if (c->sum != (c->magic + c->active_low + 0x3C3C3C3Cu))
    {
        return 0u;
    }
    return (c->active_low != 0u) ? 1u : 0u;
}

static void relay_cfg_save(uint8_t active_low)
{
    relay_cfg_t c;
    const uint32_t *src = (const uint32_t *)&c;
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t sector_err = 0u;
    uint32_t i;
    uint8_t ok = 1u;

    c.magic = RELAY_CFG_MAGIC;
    c.active_low = (active_low != 0u) ? 1u : 0u;
    c.sum = c.magic + c.active_low + 0x3C3C3C3Cu;

    HAL_FLASH_Unlock();
    erase.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase.Sector = RELAY_CFG_SECTOR;
    erase.NbSectors = 1u;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    if (HAL_FLASHEx_Erase(&erase, &sector_err) != HAL_OK)
    {
        ok = 0u;
    }

    if (ok)
    {
        for (i = 0u; i < (sizeof(c) / 4u); i++)
        {
            if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                                  RELAY_CFG_ADDR + i * 4u, src[i]) != HAL_OK)
            {
                ok = 0u;
                break;
            }
        }
    }
    HAL_FLASH_Lock();
    (void)ok;
}

/* 扫描日志：定位第一个空闲 word，并把最后一条有效记录读出来 */
static void relay_state_init(void)
{
    uint32_t idx;
    uint32_t last = 0xFFFFFFFFu;

    for (idx = 0u; idx < RELAY_ST_WORDS; idx++)
    {
        uint32_t w = *(const volatile uint32_t *)(RELAY_ST_ADDR + idx * 4u);

        if (w == 0xFFFFFFFFu)
        {
            break;
        }
        if ((w & RELAY_ST_KEEP) == RELAY_ST_TAG)
        {
            last = w;
        }
    }

    s_log_idx = idx;
    s_log_ready = 1u;

    if (last != 0xFFFFFFFFu)
    {
        s_mode = (uint8_t)(last & APP_RELAY_MASK);
    }
}

static void relay_state_save(uint8_t mode)
{
    uint32_t word = RELAY_ST_TAG | (uint32_t)(mode & APP_RELAY_MASK);

    if (!s_log_ready)
    {
        return;
    }

    HAL_FLASH_Unlock();

    if (s_log_idx >= RELAY_ST_WORDS)   /* 写满了才擦一次，然后从头再来 */
    {
        FLASH_EraseInitTypeDef erase = {0};
        uint32_t sector_err = 0u;

        erase.TypeErase = FLASH_TYPEERASE_SECTORS;
        erase.Sector = RELAY_ST_SECTOR;
        erase.NbSectors = 1u;
        erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
        if (HAL_FLASHEx_Erase(&erase, &sector_err) == HAL_OK)
        {
            s_log_idx = 0u;
        }
    }

    if (s_log_idx < RELAY_ST_WORDS)
    {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                              RELAY_ST_ADDR + s_log_idx * 4u, word) == HAL_OK)
        {
            s_log_idx++;
        }
    }

    HAL_FLASH_Lock();
}

/* 一块继电器模块只有 3 路：PC0 / PC1 / PC2（CubeMX 标签 RELAY1~RELAY3） */
static const uint16_t k_relay_pin[APP_RELAY_COUNT] = {
    RELAY1_Pin, RELAY2_Pin, RELAY3_Pin
};

void app_relay_init(void)
{
    s_active_low = relay_cfg_load();     /* 用上次保存的极性 */
    s_mode = 0u;
    app_relay_refresh();                 /* 先全部断开 */
    relay_state_init();                  /* 读回掉电前的状态，顺便缓存日志写入位置 */
    app_relay_refresh();
}

void app_relay_preinit(void)
{
    s_active_low = relay_cfg_load();
    s_mode = 0u;
    app_relay_refresh();
}

uint32_t app_relay_off_level(void)
{
    /* 低电平触发：断开 = 高电平 */
    return (relay_cfg_load() != 0u) ? (uint32_t)GPIO_PIN_SET : (uint32_t)GPIO_PIN_RESET;
}

void app_relay_set_polarity(uint8_t active_low)
{
    uint8_t v = (active_low != 0u) ? 1u : 0u;

    if (v != s_active_low)
    {
        s_active_low = v;
        relay_cfg_save(v);
    }
    app_relay_refresh();
}

uint8_t app_relay_polarity(void)
{
    return s_active_low;
}

void app_relay_refresh(void)
{
    uint8_t i;
    uint8_t on;
    GPIO_PinState pin;

    for (i = 0u; i < APP_RELAY_COUNT; i++)
    {
        on = (uint8_t)((s_mode >> i) & 1u);
        if (s_active_low)
        {
            pin = on ? GPIO_PIN_RESET : GPIO_PIN_SET;
        }
        else
        {
            pin = on ? GPIO_PIN_SET : GPIO_PIN_RESET;
        }
        HAL_GPIO_WritePin(GPIOC, k_relay_pin[i], pin);
    }
}

void app_relay_set_mode(uint8_t mode)
{
    uint8_t m = (uint8_t)(mode & APP_RELAY_MASK);

    if (m == s_mode)
    {
        return;
    }

    s_mode = m;
    app_relay_refresh();
    relay_state_save(m);        /* 掉电保存：追加一个 word，几十 us，不卡主循环 */
}

void app_relay_set_channel(uint8_t ch, uint8_t on)
{
    if (ch < 1u || ch > APP_RELAY_COUNT)
    {
        return;
    }

    if (on)
    {
        app_relay_set_mode((uint8_t)(s_mode | (1u << (ch - 1u))));
    }
    else
    {
        app_relay_set_mode((uint8_t)(s_mode & ~(1u << (ch - 1u))));
    }
}

void app_relay_toggle(uint8_t ch)
{
    if (ch < 1u || ch > APP_RELAY_COUNT)
    {
        return;
    }

    app_relay_set_mode((uint8_t)(s_mode ^ (1u << (ch - 1u))));
}

uint8_t app_relay_mode(void)
{
    return s_mode;
}
