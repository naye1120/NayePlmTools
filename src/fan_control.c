/* [CN-FORK] 风扇温控（模型 A）实现。见 fan_control.h 顶部安全说明。
 * 只改 /dev/icc_fan autoservo 目标温度字节(byte5)，固件自己闭环；不轮询、不写手动 duty。 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <pthread.h>

#include "fan_control.h"
#include "pldmgr.h"  /* pldmgr_log */
#include "utils.h"   /* pldmgr_utils_safe_mode */
#include "config.h"  /* PldmgrConfig.fan_enabled / fan_target */

/* ── /dev/icc_fan autoservo ioctl（协议：drakmor/fan_target 技术参考）────────
 *   0xC01C8F08 GET 运行时 autoservo（28 字节）
 *   0xC01C8F07 SET 运行时 autoservo（28 字节）
 * 28 字节对象：byte[2]=selector(0=默认/主档)，byte[5]=目标温度(°C)。 */
#define ICC_FAN_DEVICE        "/dev/icc_fan"
#define ICC_FAN_GET_AUTOSERVO 0xC01C8F08UL
#define ICC_FAN_SET_AUTOSERVO 0xC01C8F07UL
#define AUTOSERVO_SEL         2   /* selector 字节偏移 */
#define AUTOSERVO_TARGET_OFF  5   /* 目标温度字节偏移 */
#define AUTOSERVO_BUF         28

/* UI/后端一致的安全区间：全部低于原厂 ~91°C，只会更凉不会更热。 */
#define FAN_TARGET_MIN        60
#define FAN_TARGET_MAX        85
#define FAN_TARGET_DEFAULT    72

static pthread_mutex_t g_fan_mtx = PTHREAD_MUTEX_INITIALIZER;

static int fan_target_in_range(int t) {
    return t >= FAN_TARGET_MIN && t <= FAN_TARGET_MAX;
}

/* GET 当前 autoservo 对象；成功 0，失败 -1 */
static int fan_get_autoservo(int fd, uint8_t cfg[AUTOSERVO_BUF]) {
    memset(cfg, 0, AUTOSERVO_BUF);
    cfg[AUTOSERVO_SEL] = 0; /* selector = 0（默认档） */
    return ioctl(fd, ICC_FAN_GET_AUTOSERVO, cfg);
}

/* 读硬件当前目标温度；成功 0 写 *out，失败 -1 */
static int fan_read_target(int fd, int *out) {
    uint8_t cfg[AUTOSERVO_BUF];
    if (fan_get_autoservo(fd, cfg) < 0)
        return -1;
    *out = (int)cfg[AUTOSERVO_TARGET_OFF];
    return 0;
}

/* GET-modify-SET-verify：只改 byte5，保留其余字节；回读校验。成功 0，失败 -1 */
static int fan_write_target(int fd, int target_c) {
    uint8_t cur[AUTOSERVO_BUF], chk[AUTOSERVO_BUF];

    if (fan_get_autoservo(fd, cur) < 0)
        return -1;
    cur[AUTOSERVO_TARGET_OFF] = (uint8_t)target_c;
    if (ioctl(fd, ICC_FAN_SET_AUTOSERVO, cur) < 0)
        return -1;
    if (fan_get_autoservo(fd, chk) < 0)
        return -1;
    if ((int)chk[AUTOSERVO_TARGET_OFF] != target_c) {
        errno = EIO;
        return -1;
    }
    return 0;
}

int fan_control_status_json(char *buf, size_t size) {
    if (!buf || size == 0)
        return 0;

    PldmgrConfig cfg;
    config_read(&cfg);
    int target  = fan_target_in_range(cfg.fan_target) ? cfg.fan_target : FAN_TARGET_DEFAULT;
    int enabled = cfg.fan_enabled ? 1 : 0;

    if (pldmgr_utils_safe_mode())
        return snprintf(buf, size,
            "{\"supported\":false,\"enabled\":false,\"target\":%d,\"min\":%d,\"max\":%d,\"message\":\"safe_mode\"}",
            target, FAN_TARGET_MIN, FAN_TARGET_MAX);

    pthread_mutex_lock(&g_fan_mtx);
    int fd = open(ICC_FAN_DEVICE, O_RDWR);
    if (fd < 0) { /* 注意：fd 0 合法，必须判 <0 */
        pthread_mutex_unlock(&g_fan_mtx);
        return snprintf(buf, size,
            "{\"supported\":false,\"enabled\":false,\"target\":%d,\"min\":%d,\"max\":%d,\"message\":\"device_unavailable\"}",
            target, FAN_TARGET_MIN, FAN_TARGET_MAX);
    }

    int hw = -1;
    int rd_ok = (fan_read_target(fd, &hw) == 0);
    /* 已启用且硬件目标漂移（进游戏 pattern 切换会复位）→ 重新下发把目标拉回 */
    if (enabled && rd_ok && hw != target && fan_write_target(fd, target) == 0)
        hw = target;
    close(fd);
    pthread_mutex_unlock(&g_fan_mtx);

    return snprintf(buf, size,
        "{\"supported\":true,\"enabled\":%s,\"target\":%d,\"hw_target\":%d,\"min\":%d,\"max\":%d}",
        enabled ? "true" : "false", target, rd_ok ? hw : -1, FAN_TARGET_MIN, FAN_TARGET_MAX);
}

int fan_control_apply_json(char *buf, size_t size, int enabled, int target_c) {
    if (!buf || size == 0)
        return 0;

    if (enabled && !fan_target_in_range(target_c))
        return snprintf(buf, size,
            "{\"ok\":false,\"supported\":true,\"enabled\":false,\"target\":%d,\"min\":%d,\"max\":%d,\"message\":\"target_out_of_range\"}",
            target_c, FAN_TARGET_MIN, FAN_TARGET_MAX);

    if (pldmgr_utils_safe_mode())
        return snprintf(buf, size,
            "{\"ok\":false,\"supported\":false,\"enabled\":false,\"target\":%d,\"message\":\"safe_mode\"}",
            target_c);

    int ok;
    pthread_mutex_lock(&g_fan_mtx);
    int fd = open(ICC_FAN_DEVICE, O_RDWR);
    if (fd < 0) {
        pthread_mutex_unlock(&g_fan_mtx);
        return snprintf(buf, size,
            "{\"ok\":false,\"supported\":false,\"enabled\":false,\"target\":%d,\"message\":\"device_unavailable\"}",
            target_c);
    }
    if (!enabled) {
        ok = 1; /* 关闭：不再重设，硬件保持当前值（≤85，安全） */
    } else {
        ok = (fan_write_target(fd, target_c) == 0);
    }
    close(fd);
    pthread_mutex_unlock(&g_fan_mtx);

    if (!ok)
        return snprintf(buf, size,
            "{\"ok\":false,\"supported\":true,\"enabled\":false,\"target\":%d,\"message\":\"apply_failed\"}",
            target_c);

    /* 持久化：整份 config_read → 改风扇两键 → config_write，避免丢其它键 */
    PldmgrConfig cfg;
    config_read(&cfg);
    cfg.fan_enabled = enabled ? 1 : 0;
    if (enabled)
        cfg.fan_target = target_c;
    config_write(&cfg);
    pldmgr_log("[PLDMGR] fan control %s (target=%d)\n", enabled ? "enabled" : "disabled",
               enabled ? target_c : cfg.fan_target);

    return snprintf(buf, size,
        "{\"ok\":true,\"supported\":true,\"enabled\":%s,\"target\":%d,\"message\":\"applied\"}",
        enabled ? "true" : "false", enabled ? target_c : cfg.fan_target);
}
