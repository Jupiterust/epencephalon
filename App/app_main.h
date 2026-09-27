/*
 * app_main.h — C++ 应用层入口
 *
 * 由 Core/Src/freertos.c 的 StartDefaultTask 调用（USER CODE 区内）。
 * CubeMX 生成的文件全部保持 C，跨界一律走这个 extern "C" 边界；
 * 不要把生成的 .c 改名成 .cpp，那样下次重新生成就全乱了。
 */
#ifndef APP_MAIN_H
#define APP_MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* 永不返回 */
void app_main(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_MAIN_H */
