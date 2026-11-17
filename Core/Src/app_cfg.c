/**
  ******************************************************************************
  * @file    app_cfg.c
  * @brief   掉电保存的用户配置（WiFi 账号 + 粉尘报警阈值）：
  *          sector 10 @ 0x080C0000（128 KB，程序只用到 sector 0~4），
  *          触摸校准在 sector 11、继电器状态日志在 sector 5，互不影响
  *
  *          整块擦扇区很慢（128 KB 约 1 s），所以只在内容真的变了才写；
  *          继电器这种会频繁变的状态不放在这个扇区，见 app_relay.c
  ******************************************************************************
  */

#include "app_cfg.h"
#include "main.h"

#include <string.h>

#define CFG_MAGIC   0x32474643u     /* "CFG2"：加了 th 字段，旧格式自动失效 */
#define CFG_ADDR    0x080C0000u
#define CFG_SECTOR  FLASH_SECTOR_10

typedef struct
{
    uint32_t magic;
    uint32_t sum;
    uint16_t th;                    /* 粉尘报警阈值 ug/m3 */
    uint16_t rsv;                   /* 对齐用，保证 sizeof 能被 4 整除 */
    char     ssid[APP_CFG_SSID_LEN];
    char     pwd[APP_CFG_PWD_LEN];
} cfg_t;

static uint32_t cfg_sum(const cfg_t *c)
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
    s = (s << 5) - s + (uint32_t)c->th;
    return s;
}

/* 读出整块配置，magic / 校验和不对返回 0 */
static uint8_t cfg_read(cfg_t *out)
{
    const cfg_t *c = (const cfg_t *)CFG_ADDR;

    *out = *c;
    if (out->magic != CFG_MAGIC || out->sum != cfg_sum(out))
    {
        return 0u;
    }
    return 1u;
}

/* 拿有效配置当基底；没有就开一个空框架（只改单个字段时用） */
static void cfg_fill(cfg_t *c)
{
    if (cfg_read(c) == 0u)
    {
        memset(c, 0, sizeof(*c));
        c->magic = CFG_MAGIC;
    }
}

static uint8_t cfg_erase(void)
{
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t sector_err = 0u;

    erase.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase.Sector = CFG_SECTOR;
    erase.NbSectors = 1u;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    return (HAL_FLASHEx_Erase(&erase, &sector_err) == HAL_OK) ? 1u : 0u;
}

static uint8_t cfg_store(const cfg_t *c)
{
    const uint32_t *src = (const uint32_t *)c;
    uint32_t words = (uint32_t)(sizeof(cfg_t) / 4u);
    uint32_t i;
    uint8_t ok;

    HAL_FLASH_Unlock();
    ok = cfg_erase();
    if (ok)
    {
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

uint8_t app_cfg_load_wifi(char *ssid, char *pwd)
{
    cfg_t c;
    uint32_t sl;
    uint32_t pl;

    if (ssid == NULL || pwd == NULL || cfg_read(&c) == 0u)
    {
        return 0u;
    }

    sl = strnlen(c.ssid, APP_CFG_SSID_LEN);
    pl = strnlen(c.pwd, APP_CFG_PWD_LEN);
    if (sl == 0u || sl >= APP_CFG_SSID_LEN || pl >= APP_CFG_PWD_LEN)
    {
        return 0u;
    }

    memcpy(ssid, c.ssid, sl + 1u);
    memcpy(pwd, c.pwd, pl + 1u);
    return 1u;
}

uint8_t app_cfg_save_wifi(const char *ssid, const char *pwd)
{
    cfg_t c;

    if (ssid == NULL || pwd == NULL)
    {
        return 0u;
    }

    cfg_fill(&c);

    /* 内容没变就不擦写 Flash，避免每次上电都消耗寿命 */
    if (strncmp(c.ssid, ssid, APP_CFG_SSID_LEN) == 0 &&
        strncmp(c.pwd, pwd, APP_CFG_PWD_LEN) == 0)
    {
        return 2u;
    }

    memset(c.ssid, 0, sizeof(c.ssid));
    memset(c.pwd, 0, sizeof(c.pwd));
    (void)strncpy(c.ssid, ssid, APP_CFG_SSID_LEN - 1u);
    (void)strncpy(c.pwd, pwd, APP_CFG_PWD_LEN - 1u);
    c.sum = cfg_sum(&c);

    return (cfg_store(&c) != 0u) ? 1u : 0u;
}

void app_cfg_erase_wifi(void)
{
    cfg_t c;

    /* 只清账号，阈值等其它配置要留着 */
    if (cfg_read(&c) == 0u)
    {
        FLASH_EraseInitTypeDef erase = {0};
        uint32_t sector_err = 0u;

        HAL_FLASH_Unlock();
        erase.TypeErase = FLASH_TYPEERASE_SECTORS;
        erase.Sector = CFG_SECTOR;
        erase.NbSectors = 1u;
        erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
        (void)HAL_FLASHEx_Erase(&erase, &sector_err);
        HAL_FLASH_Lock();
        return;
    }

    memset(c.ssid, 0, sizeof(c.ssid));
    memset(c.pwd, 0, sizeof(c.pwd));
    c.sum = cfg_sum(&c);
    (void)cfg_store(&c);
}

uint8_t app_cfg_load_th(uint16_t *th)
{
    cfg_t c;

    if (th == NULL || cfg_read(&c) == 0u || c.th == 0u)
    {
        return 0u;
    }

    *th = c.th;
    return 1u;
}

uint8_t app_cfg_save_th(uint16_t th)
{
    cfg_t c;

    cfg_fill(&c);
    if (c.th == th)
    {
        return 2u;
    }

    c.th = th;
    c.sum = cfg_sum(&c);
    return (cfg_store(&c) != 0u) ? 1u : 0u;
}
