/**
 * @file lv_fs_fatfs.c
 * LVGL file system driver — maps 'S:' to FatFs root
 *
 * SquareLine Studio images use paths like "S:assets/ui_img_bg1_png.bin"
 * This driver translates lv_fs_open("S:assets/...") → f_open("assets/...")
 *
 * Features:
 *  - 8KB read cache in SDRAM to reduce SD card reads
 *  - 4-byte aligned buffer for FastLZ decompression
 *  - Graceful failure if SD card not mounted
 */

#include "lvgl.h"
#include "ff.h"
#include <string.h>

/* ---- 8KB read cache in SDRAM (reduces SD card seeks) ---- */
/* Each open file gets an aligned buffer for FATFS + read cache */
typedef struct {
    FIL   fil;                        /* FatFs file object */
    uint8_t cache[8192] __attribute__((aligned(4)));  /* 8KB aligned read cache */
    uint32_t cache_start;             /* File offset of cached data */
    uint32_t cache_len;               /* Valid bytes in cache */
} lv_fs_fatfs_file_t;

/* ---- FATFS mount state ---- */
static FATFS   fatfs_obj;
static uint8_t fatfs_mounted = 0;

/* ---- Driver callbacks ---- */

static void *fs_open_cb(lv_fs_drv_t *drv, const char *path, lv_fs_mode_t mode)
{
    (void)drv;
    if (!fatfs_mounted) return NULL;

    BYTE ff_mode = 0;
    if (mode == LV_FS_MODE_WR) {
        ff_mode = FA_WRITE | FA_CREATE_ALWAYS;
    } else if (mode == LV_FS_MODE_RD) {
        ff_mode = FA_READ;
    } else {
        ff_mode = FA_READ | FA_WRITE;
    }

    lv_fs_fatfs_file_t *fp = lv_mem_alloc(sizeof(lv_fs_fatfs_file_t));
    if (!fp) return NULL;

    memset(fp, 0, sizeof(lv_fs_fatfs_file_t));

    FRESULT res = f_open(&fp->fil, path, ff_mode);
    if (res != FR_OK) {
        lv_mem_free(fp);
        return NULL;
    }

    return fp;
}

static lv_fs_res_t fs_close_cb(lv_fs_drv_t *drv, void *file_p)
{
    (void)drv;
    lv_fs_fatfs_file_t *fp = (lv_fs_fatfs_file_t *)file_p;

    f_close(&fp->fil);
    lv_mem_free(fp);

    return LV_FS_RES_OK;
}

static lv_fs_res_t fs_read_cb(lv_fs_drv_t *drv, void *file_p,
                               void *buf, uint32_t btr, uint32_t *br)
{
    (void)drv;
    lv_fs_fatfs_file_t *fp = (lv_fs_fatfs_file_t *)file_p;

    /* Try to serve from cache first */
    uint32_t fpos = f_tell(&fp->fil);

    /* Check if requested data is in cache */
    if (fp->cache_len > 0 &&
        fpos >= fp->cache_start &&
        fpos + btr <= fp->cache_start + fp->cache_len) {
        /* Cache hit — copy from cache */
        uint32_t offset = fpos - fp->cache_start;
        memcpy(buf, fp->cache + offset, btr);
        *br = btr;
        /* Advance file pointer */
        f_lseek(&fp->fil, fpos + btr);
        return LV_FS_RES_OK;
    }

    /* Cache miss — read from SD card */
    if (btr <= sizeof(fp->cache)) {
        /* Read a full cache block from current position */
        UINT bytes_read = 0;
        FRESULT res = f_read(&fp->fil, fp->cache, sizeof(fp->cache), &bytes_read);
        if (res != FR_OK) {
            *br = 0;
            return LV_FS_RES_FS_ERR;
        }

        fp->cache_start = fpos;
        fp->cache_len = bytes_read;

        /* Copy requested amount */
        uint32_t copy_len = (btr <= bytes_read) ? btr : bytes_read;
        memcpy(buf, fp->cache, copy_len);
        *br = copy_len;

        /* Seek to the position after what was requested */
        f_lseek(&fp->fil, fpos + copy_len);
        return LV_FS_RES_OK;
    }

    /* Request larger than cache — direct read */
    UINT bytes_read = 0;
    FRESULT res = f_read(&fp->fil, buf, btr, &bytes_read);
    *br = bytes_read;

    /* Invalidate cache after direct read */
    fp->cache_len = 0;

    return (res == FR_OK) ? LV_FS_RES_OK : LV_FS_RES_FS_ERR;
}

static lv_fs_res_t fs_write_cb(lv_fs_drv_t *drv, void *file_p,
                                const void *buf, uint32_t btw, uint32_t *bw)
{
    (void)drv;
    lv_fs_fatfs_file_t *fp = (lv_fs_fatfs_file_t *)file_p;
    UINT bytes_written = 0;

    fp->cache_len = 0;  /* Invalidate cache on write */

    FRESULT res = f_write(&fp->fil, buf, btw, &bytes_written);
    *bw = bytes_written;

    return (res == FR_OK) ? LV_FS_RES_OK : LV_FS_RES_FS_ERR;
}

static lv_fs_res_t fs_seek_cb(lv_fs_drv_t *drv, void *file_p,
                               uint32_t pos, lv_fs_whence_t whence)
{
    (void)drv;
    lv_fs_fatfs_file_t *fp = (lv_fs_fatfs_file_t *)file_p;

    uint32_t target = 0;
    switch (whence) {
    case LV_FS_SEEK_SET:
        target = pos;
        break;
    case LV_FS_SEEK_CUR:
        target = f_tell(&fp->fil) + pos;
        break;
    case LV_FS_SEEK_END:
        target = f_size(&fp->fil) + pos;
        break;
    default:
        return LV_FS_RES_INV_PARAM;
    }

    FRESULT res = f_lseek(&fp->fil, target);
    return (res == FR_OK) ? LV_FS_RES_OK : LV_FS_RES_FS_ERR;
}

static lv_fs_res_t fs_tell_cb(lv_fs_drv_t *drv, void *file_p, uint32_t *pos_p)
{
    (void)drv;
    lv_fs_fatfs_file_t *fp = (lv_fs_fatfs_file_t *)file_p;
    *pos_p = f_tell(&fp->fil);
    return LV_FS_RES_OK;
}

/* ---- Public API ---- */

/**
 * Mount SD card and register LVGL 'S' drive.
 * Call after lv_init() and MX_SDIO_SD_Init().
 *
 * @return 0 on success, -1 if SD card not found
 */
int lv_fs_fatfs_init(void)
{
    /* Try to mount the SD card (FAT32) */
    FRESULT res = f_mount(&fatfs_obj, "", 1);  /* "" = drive 0, 1 = mount now */
    if (res != FR_OK) {
        /* SD card not present or not formatted */
        fatfs_mounted = 0;
        /* Don't crash — UI will just have blank images */
        /* Register driver anyway so lv_fs_open("S:...") returns NULL gracefully */
    } else {
        fatfs_mounted = 1;
    }

    /* Register 'S' drive with LVGL */
    static lv_fs_drv_t fs_drv;
    lv_fs_drv_init(&fs_drv);

    fs_drv.letter   = 'S';
    fs_drv.open_cb  = fs_open_cb;
    fs_drv.close_cb = fs_close_cb;
    fs_drv.read_cb  = fs_read_cb;
    fs_drv.write_cb = fs_write_cb;
    fs_drv.seek_cb  = fs_seek_cb;
    fs_drv.tell_cb  = fs_tell_cb;

    lv_fs_drv_register(&fs_drv);

    return fatfs_mounted ? 0 : -1;
}

/**
 * Check if SD card is mounted
 */
uint8_t lv_fs_fatfs_is_mounted(void)
{
    return fatfs_mounted;
}
