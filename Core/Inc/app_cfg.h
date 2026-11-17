/**
  ******************************************************************************
  * @file    app_cfg.h
  * @brief   掉电保存的用户配置（WiFi 账号等），存在 Flash 最后一个扇区
  ******************************************************************************
  */

#ifndef __APP_CFG_H
#define __APP_CFG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define APP_CFG_SSID_LEN 33u
#define APP_CFG_PWD_LEN  65u

/* 读到返回 1，没有保存过返回 0 */
uint8_t app_cfg_load_wifi(char *ssid, char *pwd);

/* 和已保存的一致就不写 Flash（减少擦写）
   返回：0 = 写入失败，1 = 已写入，2 = 内容相同未写入 */
uint8_t app_cfg_save_wifi(const char *ssid, const char *pwd);

/* 清除保存的账号（阈值等其它配置保留） */
void app_cfg_erase_wifi(void);

/* 粉尘报警阈值：读到返回 1，没保存过返回 0 */
uint8_t app_cfg_load_th(uint16_t *th);

/* 和已保存的一致就不写 Flash
   返回：0 = 写入失败，1 = 已写入，2 = 内容相同未写入 */
uint8_t app_cfg_save_th(uint16_t th);

#ifdef __cplusplus
}
#endif

#endif /* __APP_CFG_H */
