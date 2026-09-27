/*
 * sdcard.hpp — SDIO + FATFS 封装
 *
 * CubeMX 已生成 sd_diskio.c / bsp_driver_sd.c / fatfs_platform.c，
 * 其中 fatfs_platform.c 的 BSP_PlatformIsDetected() 已自动接到 PE15 的
 * SD_DETECT 标签上，所以插卡检测不用自己写。
 *
 * 注意当前 SDIO 没有配 DMA，sd_diskio 走的是轮询，读写期间会占住调用它的任务。
 * 对小脑来说日志写入应该放在低优先级任务里，别和逆解算在同一个任务。
 */
#pragma once

#include <cstdint>

namespace drv {

class SdCard
{
public:
    /* 卡在位？（读 PE15） */
    static bool detected();

    /* 挂载文件系统。未插卡直接返回 false，不会阻塞。 */
    bool mount();
    void unmount();

    bool mounted() const { return mounted_; }

    /* 容量信息，单位 MB。未挂载时返回 false。 */
    bool capacity(uint32_t *total_mb, uint32_t *free_mb);

    /* 追加一行文本到指定文件，用于日志。自动补换行。 */
    bool append_line(const char *path, const char *line);

    /* 查文件大小。用来确认写入真的落盘：重启后这个数字应该持续增长。 */
    bool file_size(const char *path, uint32_t *size);

    /* 最近一次 FatFs 调用的返回码，排错用 */
    int last_error() const { return last_err_; }

private:
    bool mounted_  = false;
    int  last_err_ = 0;
};

extern SdCard sdcard;

} // namespace drv
