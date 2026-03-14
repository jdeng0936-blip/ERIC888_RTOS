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

/* ================================================================
 * SD 卡字库加载 + LVGL Fallback 机制
 *
 * 工作原理：
 *   Flash 字体（pfht16/pfht18）只包含 UI 用到的 ~280 个汉字
 *   SD 卡字体（gb2312_simhei_16/20）包含 20902 个 CJK 汉字
 *
 *   设置 fallback 后：
 *     LVGL 显示 "刘庄变电站"
 *       → "刘" 不在 pfht16 → 自动去 SD 卡字体找 → ✅
 *       → "变" 在 pfht16 → 直接用（更快） → ✅
 *
 * 硬件链路：
 *   SD 卡 → SDIO(PC8~PC12) → FatFs → lv_font_load()
 *            ↓
 *   字体数据被读入 SDRAM → LVGL 渲染到 LCD
 * ================================================================ */

static lv_font_t *sd_font_16 = NULL;  /* SD 卡 16px 字库（对应 pfht16） */
static lv_font_t *sd_font_20 = NULL;  /* SD 卡 20px 字库（对应 pfht18） */

/**
 * @brief  从 SD 卡加载 GB2312 字库并设置为 UI 字体的 fallback
 *
 * 调用时机：在 lv_fs_fatfs_init() 和 ui_init() 之后调用
 * 原因：必须先有 'S:' 驱动 + UI 字体对象才能设 fallback
 *
 * @return 加载成功的字库数量（0/1/2）
 *
 * 硬件影响：
 *   - 从 SD 卡读取约 1.7MB 数据（16px=691KB + 20px=1MB）
 *   - 字体数据存入 LVGL 管理的内存（从 SDRAM heap 分配）
 *   - 加载时间约 200~500ms（只在启动时执行一次）
 *   - 加载后不再访问 SD 卡字库文件（全在内存中）
 */
int lv_fs_load_sd_fonts(void)
{
    int loaded = 0;

    if (!fatfs_mounted) {
        /* SD 卡没插 → 跳过，UI 正常运行，只是某些罕见汉字显示 □ */
        return 0;
    }

    /* ---- 加载 16px 字库（匹配 pfht16 = 16px 4bpp simhei_b）---- */
    /* lv_font_load() 使用我们注册的 'S:' 驱动从 SD 卡读取 .bin 文件 */
    /* 它会在 LVGL 内部分配内存，把整个字库读入 RAM */
    sd_font_16 = lv_font_load("S:fonts/gb2312_simhei_16.bin");
    // ← "S:" = 我们的 SD 卡驱动器
    // ← "fonts/gb2312_simhei_16.bin" = SD 卡上的路径
    // ← 返回值：成功 = 字体指针，失败 = NULL

    if (sd_font_16 != NULL) {
        loaded++;
    }

    /* ---- 加载 20px 字库（匹配 pfht18 = 20px 4bpp simhei_b）---- */
    sd_font_20 = lv_font_load("S:fonts/gb2312_simhei_20.bin");

    if (sd_font_20 != NULL) {
        loaded++;
    }

    /* ---- 设置 fallback 链 ---- */
    /* fallback 的意思：如果主字体找不到某个字，就去 fallback 字体里找 */
    /* 这是 LVGL 内置的机制，不需要额外代码 */

    if (sd_font_16 != NULL) {
        /* 所有使用 pfht16 (16px) 的标签，找不到的字去 SD 卡字库找 */
        extern lv_font_t ui_font_pfht16;
        ui_font_pfht16.fallback = sd_font_16;
        // ← 一行代码！效果：
        //   lv_label_set_text(label, "刘庄") → "刘"不在pfht16
        //   → 自动查 sd_font_16 → 找到了 → 显示正常

        /* Font14 和 Font16 也设 fallback（它们也是 16px 级别） */
        extern lv_font_t ui_font_Font14;
        extern lv_font_t ui_font_Font16;
        ui_font_Font14.fallback = sd_font_16;
        ui_font_Font16.fallback = sd_font_16;
    }

    if (sd_font_20 != NULL) {
        /* 所有使用 pfht18 (20px) 的输入框/大标题， */
        /* 找不到的字去 SD 卡字库找 */
        extern lv_font_t ui_font_pfht18;
        ui_font_pfht18.fallback = sd_font_20;
        // ← 客户在 textarea 输入 "刘庄变电站进线"
        //   → pfht18 里没有 "刘" → 查 sd_font_20 → 有！
        //   → 字号完全匹配（20px），显示效果无缝衔接

        /* pfht20 也设（22px 用 20px fallback，大小接近） */
        extern lv_font_t ui_font_pfht20;
        ui_font_pfht20.fallback = sd_font_20;
    }

    return loaded;
}

/**
 * @brief  释放 SD 卡字库（正常运行中不需要调用）
 *
 * 只有在动态卸载/重新加载字库时才需要
 * 释放前必须确保没有任何 label 正在使用这些字体
 */
void lv_fs_unload_sd_fonts(void)
{
    if (sd_font_16) {
        /* 先断开 fallback 链 */
        extern lv_font_t ui_font_pfht16;
        extern lv_font_t ui_font_Font14;
        extern lv_font_t ui_font_Font16;
        ui_font_pfht16.fallback = NULL;
        ui_font_Font14.fallback = NULL;
        ui_font_Font16.fallback = NULL;

        lv_font_free(sd_font_16);
        sd_font_16 = NULL;
    }

    if (sd_font_20) {
        extern lv_font_t ui_font_pfht18;
        extern lv_font_t ui_font_pfht20;
        ui_font_pfht18.fallback = NULL;
        ui_font_pfht20.fallback = NULL;

        lv_font_free(sd_font_20);
        sd_font_20 = NULL;
    }
}
