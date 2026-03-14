/**
 * @file lv_fs_fatfs.h
 * LVGL file system driver — maps 'S:' to FatFs SD card
 */
#ifndef LV_FS_FATFS_H
#define LV_FS_FATFS_H

#include <stdint.h>

/**
 * Mount SD card and register LVGL 'S' drive.
 * Call after lv_init() and MX_SDIO_SD_Init().
 * @return 0 on success, -1 if SD card not found
 */
int lv_fs_fatfs_init(void);

/**
 * Check if SD card is mounted
 */
uint8_t lv_fs_fatfs_is_mounted(void);

#endif /* LV_FS_FATFS_H */
