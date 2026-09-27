#include "sdcard.hpp"

#include "fatfs.h"
#include "fatfs_platform.h"
#include "sdio.h"
#include "bsp_driver_sd.h"

#include <cstdio>
#include <cstring>

namespace drv {

SdCard sdcard;

bool SdCard::detected()
{
    return BSP_PlatformIsDetected() == 1;
}

bool SdCard::mount()
{
    if (mounted_)
        return true;

    if (!detected())
    {
        last_err_ = -1;     /* 没插卡，和 FatFs 的错误码区分开 */
        return false;
    }

    /* 必须先走 BSP_SD_Init()：MX_SDIO_SD_Init() 只填 hsd.Init，真正的
     * HAL_SD_Init() 在这里，而 __HAL_LINKDMA 又是在它触发的 HAL_SD_MspInit()
     * 里执行的。在此之前查 hdmarx/hdmatx 一定是 NULL，判不出"有没有配 DMA"。
     * 这一步同时把总线从 1-bit 切到 4-bit。 */
    if (BSP_SD_Init() != MSD_OK)
    {
        last_err_ = -3;     /* 卡在位但初始化失败（接触不良/时钟/卡本身） */
        return false;
    }

    /* 到这里句柄才有意义。sd_diskio.c 用的是 DMA 版 BSP_SD_ReadBlocks_DMA()，
     * 若 .ioc 里没给 SDIO 配 DMA，HAL 会解引用野指针 -> HardFault
     * （BFAR = 0x20030000，RAM 末尾外一字节）。宁可在这里挡住。 */
    if (hsd.hdmarx == nullptr || hsd.hdmatx == nullptr)
    {
        last_err_ = -2;     /* SDIO 的 DMA 没配，去 CubeMX 补 */
        return false;
    }

    /* 第三个参数 1 = 立即挂载（而非延迟到首次访问），
     * 这样这里就能拿到真实的失败原因，而不是把错误推到后面某次 f_open */
    FRESULT r = f_mount(&SDFatFS, SDPath, 1);
    last_err_ = static_cast<int>(r);
    mounted_  = (r == FR_OK);
    return mounted_;
}

void SdCard::unmount()
{
    if (!mounted_)
        return;
    f_mount(nullptr, SDPath, 0);
    mounted_ = false;
}

bool SdCard::capacity(uint32_t *total_mb, uint32_t *free_mb)
{
    if (!mounted_)
        return false;

    FATFS   *fs = nullptr;
    DWORD    free_clusters = 0;
    FRESULT  r = f_getfree(SDPath, &free_clusters, &fs);
    last_err_ = static_cast<int>(r);
    if (r != FR_OK || fs == nullptr)
        return false;

    /* 扇区按 512B 算：簇数 * 每簇扇区数 / 2 = KB，再 /1024 = MB */
    const uint32_t total_sectors = (fs->n_fatent - 2) * fs->csize;
    const uint32_t free_sectors  = free_clusters * fs->csize;

    if (total_mb) *total_mb = total_sectors / 2048u;
    if (free_mb)  *free_mb  = free_sectors  / 2048u;
    return true;
}

bool SdCard::file_size(const char *path, uint32_t *size)
{
    if (!mounted_ || path == nullptr)
        return false;

    FILINFO fno{};
    FRESULT r = f_stat(path, &fno);
    last_err_ = static_cast<int>(r);
    if (r != FR_OK)
        return false;

    if (size)
        *size = static_cast<uint32_t>(fno.fsize);
    return true;
}

bool SdCard::append_line(const char *path, const char *line)
{
    if (!mounted_ || path == nullptr || line == nullptr)
        return false;

    FIL f;
    FRESULT r = f_open(&f, path, FA_OPEN_APPEND | FA_WRITE);
    last_err_ = static_cast<int>(r);
    if (r != FR_OK)
        return false;

    UINT wrote = 0;
    const UINT len = static_cast<UINT>(std::strlen(line));
    r = f_write(&f, line, len, &wrote);
    if (r == FR_OK && wrote == len)
        r = f_write(&f, "\r\n", 2, &wrote);

    last_err_ = static_cast<int>(r);
    f_close(&f);
    return r == FR_OK;
}

} // namespace drv
