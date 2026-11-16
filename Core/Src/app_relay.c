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

typedef struct
{
    uint32_t magic;
    uint32_t active_low;
    uint32_t sum;
} relay_cfg_t;

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

/* CH1..CH6 全部在 GPIOC 上，端口常量可以共用 */
static const uint16_t k_relay_pin[APP_RELAY_COUNT] = {
    RELAY_Pin, RELAYC1_Pin, RELAYC2_Pin, RELAYC3_Pin, RELAYC4_Pin, RELAYC5_Pin
};

static uint8_t s_mode;
static uint8_t s_active_low;

void app_relay_init(void)
{
    s_active_low = relay_cfg_load();     /* 用上次保存的极性 */
    app_relay_set_mode(0u);
}

void app_relay_preinit(void)
{
    s_active_low = relay_cfg_load();
    s_mode = 0u;
    app_relay_refresh();
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
    s_mode = (uint8_t)(mode & 0x3Fu);
    app_relay_refresh();
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
