/**
  ******************************************************************************
  * @file    app_cfg.c
  * @brief   WiFi 账号掉电保存：sector 10 @ 0x080C0000（128 KB，程序只用到 sector 0~4），
  *          触摸校准放在 sector 11，两者互不影响
  ******************************************************************************
  */

#include "app_cfg.h"
#include "main.h"

#include <string.h>

#define CFG_MAGIC   0x57494649u     /* "WIFI" */
#define CFG_ADDR    0x080C0000u
#define CFG_SECTOR  FLASH_SECTOR_10

typedef struct
{
    uint32_t magic;
    uint32_t sum;
    char     ssid[APP_CFG_SSID_LEN];
    char     pwd[APP_CFG_PWD_LEN];
} cfg_wifi_t;

static uint32_t cfg_sum(const cfg_wifi_t *c)
{
    const uint8_t *p = (const uint8_t *)c->ssid;
    uint32_t s = 0x1234567u;
    uint32_t i;

    for (i = 0u; i < APP_CFG_SSID_LEN; i++)
    {
        s = (s << 5) - s + p[i];
    }
    p = (const uint8_t *)c->pwd;
    for (i = 0u; i < APP_CFG_PWD_LEN; i++)
    {
        s = (s << 5) - s + p[i];
    }
    return s;
}

uint8_t app_cfg_load_wifi(char *ssid, char *pwd)
{
    const cfg_wifi_t *c = (const cfg_wifi_t *)CFG_ADDR;
    uint32_t sl;
    uint32_t pl;

    if (ssid == NULL || pwd == NULL)
    {
        return 0u;
    }

    if (c->magic != CFG_MAGIC || c->sum != cfg_sum(c))
    {
        return 0u;
    }

    sl = strnlen(c->ssid, APP_CFG_SSID_LEN);
    pl = strnlen(c->pwd, APP_CFG_PWD_LEN);
    if (sl == 0u || sl >= APP_CFG_SSID_LEN || pl >= APP_CFG_PWD_LEN)
    {
        return 0u;
    }

    memcpy(ssid, c->ssid, sl + 1u);
    memcpy(pwd, c->pwd, pl + 1u);
    return 1u;
}

static uint8_t cfg_write(const char *ssid, const char *pwd)
{
    cfg_wifi_t c;
    const uint32_t *src;
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t sector_err = 0u;
    uint32_t words;
    uint32_t i;
    uint8_t ok = 1u;

    memset(&c, 0, sizeof(c));
    c.magic = CFG_MAGIC;
    (void)strncpy(c.ssid, ssid, APP_CFG_SSID_LEN - 1u);
    (void)strncpy(c.pwd, pwd, APP_CFG_PWD_LEN - 1u);
    c.sum = cfg_sum(&c);

    HAL_FLASH_Unlock();

    erase.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase.Sector = CFG_SECTOR;
    erase.NbSectors = 1u;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    if (HAL_FLASHEx_Erase(&erase, &sector_err) != HAL_OK)
    {
        ok = 0u;
    }

    if (ok)
    {
        src = (const uint32_t *)&c;
        words = sizeof(c) / 4u;
        for (i = 0u; i < words; i++)
        {
            if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                                  CFG_ADDR + i * 4u, src[i]) != HAL_OK)
            {
                ok = 0u;
                break;
            }
        }
    }

    HAL_FLASH_Lock();
    return ok;
}

uint8_t app_cfg_save_wifi(const char *ssid, const char *pwd)
{
    char old_ssid[APP_CFG_SSID_LEN];
    char old_pwd[APP_CFG_PWD_LEN];

    if (ssid == NULL || pwd == NULL)
    {
        return 0u;
    }

    /* 内容没变就不擦写 Flash，避免每次上电都消耗寿命 */
    if (app_cfg_load_wifi(old_ssid, old_pwd) &&
        strcmp(old_ssid, ssid) == 0 && strcmp(old_pwd, pwd) == 0)
    {
        return 2u;
    }

    return (cfg_write(ssid, pwd) != 0u) ? 1u : 0u;
}

void app_cfg_erase_wifi(void)
{
    cfg_wifi_t c;
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t sector_err = 0u;

    memset(&c, 0xFF, sizeof(c));    /* 只把首字清成非 magic 即可 */

    HAL_FLASH_Unlock();
    erase.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase.Sector = CFG_SECTOR;
    erase.NbSectors = 1u;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    (void)HAL_FLASHEx_Erase(&erase, &sector_err);
    HAL_FLASH_Lock();
}
