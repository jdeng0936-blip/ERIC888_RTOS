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

/**
 * Load GB2312 font files from SD card and set as fallback
 * for UI fonts (pfht16/pfht18).
 * Call AFTER lv_fs_fatfs_init() and ui_init().
 * @return number of fonts loaded (0/1/2)
 */
int lv_fs_load_sd_fonts(void);

/**
 * Unload SD card fonts and clear fallback pointers.
 * Only needed for dynamic reload scenarios.
 */
void lv_fs_unload_sd_fonts(void);

#endif /* LV_FS_FATFS_H */
