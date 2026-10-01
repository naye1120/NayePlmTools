/* [CN-FORK] 温控只读模块。移植自 v0.5.1.63 src/stats.c 的温度/风扇采集 + 传感器安全闸，
 * 只保留温控所需最小集：CPU/GPU/SSD 温度 + 风扇占空比。型号/序列号/FPS/DCE 不带回。 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/time.h>
#include <pthread.h>

#include "thermal.h"
#include "pldmgr.h" /* pldmgr_log / pldmgr_utils_safe_mode（经 utils.h）/ PLDMGR_SAFE_MODE_FLAG */

/* ── PS5 内核导出（实机验证过的正解，出处 v0.5.1.63 stats.c / ps5mon）──
 *   CPU 温度        : sceKernelGetCpuTemperature
 *   GPU/APU 温度     : sceKernelGetSocSensorTemperature(sensor_id = 0)
 *   SSD/硬盘温度     : sceKernelGetSocSensorTemperature(sensor_id = 1)  // ps5mon hdd_c
 *   风扇占空比       : sceKernelGetCurrentFanDuty  duty ∈ [0,1024]
 * 前两个 SDK libkernel 一定有（stats.c 一直用 extern）；FanDuty 的 NID 不一定存在，
 * 声明为 weak，运行判空后再调，避免链接/加载失败。 */
extern int sceKernelGetCpuTemperature(int *temp);
extern int sceKernelGetSocSensorTemperature(int sensor_id, int *temp);
__attribute__((weak)) int sceKernelGetCurrentFanDuty(uint16_t *duty, uint64_t *chassis);

#define FAN_DUTY_MAX               1024
#define THERMAL_PROBE_INTERVAL_MS  5000

static long long now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

/* ── 传感器安全闸 ────────────────────────────────────────────────
 * 项目规范（PS5 传感器 API 安全调用规范）：所有 Sensor API 必须互斥串行化 + 限流
 * （≤ 5s 一次）。历史上首页 2s 轮询 + 客户端 abort（只断客户端、服务端 handler 线程照跑）
 * + MHD thread-per-connection，把不可重入的传感器 ioctl 打串 → 整机死机。
 * 现在整份快照在锁内构建并缓存 5s：并发请求直接命中缓存，不再产生任何内核调用。 */
static pthread_mutex_t g_thermal_mtx = PTHREAD_MUTEX_INITIALIZER;
static char      g_thermal_cache[512];
static size_t    g_thermal_cache_len = 0;
static long long g_thermal_cache_ms  = 0;

static size_t build_thermal_json(char *buf, size_t size) {
    int cpu_temp = -1, gpu_temp = -1, ssd_temp = -1, fan_duty = 0;

    if (sceKernelGetCpuTemperature(&cpu_temp) != 0) cpu_temp = -1;
    if (sceKernelGetSocSensorTemperature(0, &gpu_temp) != 0) gpu_temp = -1;
    if (sceKernelGetSocSensorTemperature(1, &ssd_temp) != 0) ssd_temp = -1;

    if (sceKernelGetCurrentFanDuty != NULL) {
        uint16_t duty = 0;
        uint64_t chassis = 0;
        if (sceKernelGetCurrentFanDuty(&duty, &chassis) == 0) {
            fan_duty = (int)(((double)duty * 100.0) / (double)FAN_DUTY_MAX);
            if (fan_duty < 0)   fan_duty = 0;
            if (fan_duty > 100) fan_duty = 100;
        }
    }

    return (size_t)snprintf(buf, size,
        "{\"enabled\":true,"
        "\"cpu\":{\"temp\":%d},"
        "\"gpu\":{\"temp\":%d},"
        "\"ssd\":{\"temp\":%d},"
        "\"fan\":{\"duty\":%d,\"max\":100}}",
        cpu_temp, gpu_temp, ssd_temp, fan_duty);
}

int thermal_stats_json(char *buf, size_t size) {
    if (!buf || size == 0) return 0;

    /* safe_mode 哨兵存在（FTP 建 /data/nayeplmtools/safe_mode 即可止血，免重编译）：
     * 一个内核调用都不发，直接回 disabled，前端见 enabled:false 即显占位破折号。 */
    if (pldmgr_utils_safe_mode())
        return snprintf(buf, size, "{\"enabled\":false}");

    pthread_mutex_lock(&g_thermal_mtx);
    long long t = now_ms();
    if (g_thermal_cache_len && t >= g_thermal_cache_ms &&
        (t - g_thermal_cache_ms) < THERMAL_PROBE_INTERVAL_MS) {
        size_t n = g_thermal_cache_len;
        if (n > size - 1) n = size - 1;
        memcpy(buf, g_thermal_cache, n);
        buf[n] = '\0';
        pthread_mutex_unlock(&g_thermal_mtx);
        return (int)n;
    }

    size_t n = build_thermal_json(buf, size);
    if (n > 0 && n < sizeof(g_thermal_cache) && n < size) {
        memcpy(g_thermal_cache, buf, n);
        g_thermal_cache[n] = '\0';
        g_thermal_cache_len = n;
        g_thermal_cache_ms  = t;
    } else {
        g_thermal_cache_len = 0; /* 被截断或超长：不缓存，宁可多探一次也不回半截 JSON */
    }
    pthread_mutex_unlock(&g_thermal_mtx);
    return (int)n;
}
