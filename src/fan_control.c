/* [CN-FORK] 风扇温控（模型 A + 常驻重发守护）实现。见 fan_control.h 顶部安全说明。
 * 只改 /dev/icc_fan autoservo 目标温度字节(byte5)，固件自己闭环；不写手动 duty、不碰传感器 API。
 *
 * 状态模型（按用户确认）：
 *   - 开关(fan_enabled)：只是「打开功能」，开启后默认失效(fan_applied=0)，不写硬件；
 *     只要不手动关就一直启用，重启/升级后从 config 恢复。
 *   - 应用(fan_applied=1)：设定好温度点「应用」才真正下发到硬件、生效。
 *   - 守护线程：仅当 enabled && applied 时，按 fan_interval 秒检测硬件目标漂移并自动拉回
 *     （对抗进游戏/切状态导致的复位）。运行态以内存为准，变更即持久化，避免与 HTTP 写文件竞争。 */
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
#include "config.h"  /* PldmgrConfig.fan_* */

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
/* 守护重发周期（秒）：参考 owendswang 默认 7s；下限 5s 保守，上限 3600s。 */
#define FAN_INTERVAL_DEFAULT  7
#define FAN_INTERVAL_MIN      5
#define FAN_INTERVAL_MAX      3600

static pthread_mutex_t g_fan_mtx = PTHREAD_MUTEX_INITIALIZER;

/* 运行态（内存为准，变更即持久化到 config） */
static int g_fan_enabled  = 0;
static int g_fan_applied  = 0;
static int g_fan_target   = FAN_TARGET_DEFAULT;
static int g_fan_interval = FAN_INTERVAL_DEFAULT;
static volatile int g_fan_daemon_started = 0;

static int fan_target_in_range(int t) {
    return t >= FAN_TARGET_MIN && t <= FAN_TARGET_MAX;
}

static int fan_interval_clamp(int s) {
    if (s < FAN_INTERVAL_MIN) return FAN_INTERVAL_MIN;
    if (s > FAN_INTERVAL_MAX) return FAN_INTERVAL_MAX;
    return s;
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

/* 把当前内存态写回 config（调用方须持 g_fan_mtx）。整份 read-modify-write 避免丢其它键。 */
static void fan_persist_locked(void) {
    PldmgrConfig cfg;
    config_read(&cfg);
    cfg.fan_enabled  = g_fan_enabled;
    cfg.fan_applied  = g_fan_applied;
    cfg.fan_target   = g_fan_target;
    cfg.fan_interval = g_fan_interval;
    config_write(&cfg);
}

int fan_control_status_json(char *buf, size_t size) {
    if (!buf || size == 0)
        return 0;

    int enabled, applied, target, interval;
    pthread_mutex_lock(&g_fan_mtx);
    enabled = g_fan_enabled; applied = g_fan_applied;
    target = g_fan_target;   interval = g_fan_interval;
    pthread_mutex_unlock(&g_fan_mtx);

    if (pldmgr_utils_safe_mode())
        return snprintf(buf, size,
            "{\"supported\":false,\"enabled\":%s,\"applied\":false,\"target\":%d,\"hw_target\":-1,\"min\":%d,\"max\":%d,\"interval\":%d,\"message\":\"safe_mode\"}",
            enabled ? "true" : "false", target, FAN_TARGET_MIN, FAN_TARGET_MAX, interval);

    pthread_mutex_lock(&g_fan_mtx);
    int fd = open(ICC_FAN_DEVICE, O_RDWR);
    if (fd < 0) { /* 注意：fd 0 合法，必须判 <0 */
        pthread_mutex_unlock(&g_fan_mtx);
        return snprintf(buf, size,
            "{\"supported\":false,\"enabled\":%s,\"applied\":%s,\"target\":%d,\"hw_target\":-1,\"min\":%d,\"max\":%d,\"interval\":%d,\"message\":\"device_unavailable\"}",
            enabled ? "true" : "false", applied ? "true" : "false", target,
            FAN_TARGET_MIN, FAN_TARGET_MAX, interval);
    }
    int hw = -1;
    int rd_ok = (fan_read_target(fd, &hw) == 0); /* 只读，不写：重发交给守护线程 */
    close(fd);
    pthread_mutex_unlock(&g_fan_mtx);

    return snprintf(buf, size,
        "{\"supported\":true,\"enabled\":%s,\"applied\":%s,\"target\":%d,\"hw_target\":%d,\"min\":%d,\"max\":%d,\"interval\":%d}",
        enabled ? "true" : "false", applied ? "true" : "false", target,
        rd_ok ? hw : -1, FAN_TARGET_MIN, FAN_TARGET_MAX, interval);
}

int fan_control_set_enabled_json(char *buf, size_t size, int enabled) {
    if (!buf || size == 0)
        return 0;
    enabled = enabled ? 1 : 0;

    /* 开关只改启用位并清「生效」标记，绝不碰硬件——开启后默认失效，需再点应用。 */
    pthread_mutex_lock(&g_fan_mtx);
    g_fan_enabled = enabled;
    g_fan_applied = 0;
    fan_persist_locked();
    int en = g_fan_enabled, tg = g_fan_target;
    pthread_mutex_unlock(&g_fan_mtx);

    pldmgr_log("[PLDMGR] fan control switch -> %s (applied cleared)\n", enabled ? "ON" : "OFF");
    return snprintf(buf, size,
        "{\"ok\":true,\"supported\":true,\"enabled\":%s,\"applied\":false,\"target\":%d,\"message\":\"switch_set\"}",
        en ? "true" : "false", tg);
}

int fan_control_apply_json(char *buf, size_t size, int target_c) {
    if (!buf || size == 0)
        return 0;

    if (!fan_target_in_range(target_c))
        return snprintf(buf, size,
            "{\"ok\":false,\"supported\":true,\"enabled\":false,\"applied\":false,\"target\":%d,\"min\":%d,\"max\":%d,\"message\":\"target_out_of_range\"}",
            target_c, FAN_TARGET_MIN, FAN_TARGET_MAX);

    if (pldmgr_utils_safe_mode())
        return snprintf(buf, size,
            "{\"ok\":false,\"supported\":false,\"enabled\":false,\"applied\":false,\"target\":%d,\"message\":\"safe_mode\"}",
            target_c);

    pthread_mutex_lock(&g_fan_mtx);
    int fd = open(ICC_FAN_DEVICE, O_RDWR);
    if (fd < 0) {
        int en = g_fan_enabled;
        pthread_mutex_unlock(&g_fan_mtx);
        return snprintf(buf, size,
            "{\"ok\":false,\"supported\":false,\"enabled\":%s,\"applied\":false,\"target\":%d,\"message\":\"device_unavailable\"}",
            en ? "true" : "false", target_c);
    }
    int ok = (fan_write_target(fd, target_c) == 0);
    close(fd);

    if (!ok) {
        int en = g_fan_enabled;
        pthread_mutex_unlock(&g_fan_mtx);
        return snprintf(buf, size,
            "{\"ok\":false,\"supported\":true,\"enabled\":%s,\"applied\":false,\"target\":%d,\"message\":\"apply_failed\"}",
            en ? "true" : "false", target_c);
    }

    /* 下发成功：置生效 + 隐含启用，持久化。 */
    g_fan_target  = target_c;
    g_fan_enabled = 1;
    g_fan_applied = 1;
    fan_persist_locked();
    pthread_mutex_unlock(&g_fan_mtx);

    pldmgr_log("[PLDMGR] fan control applied target=%d C (生效)\n", target_c);
    return snprintf(buf, size,
        "{\"ok\":true,\"supported\":true,\"enabled\":true,\"applied\":true,\"target\":%d,\"message\":\"applied\"}",
        target_c);
}

/* ── 常驻重发守护 ────────────────────────────────────────────────
 * 只碰 /dev/icc_fan（open/ioctl/close），不调用任何 libkernel 传感器 API，
 * 与当初害死机的 2s 轮询传感器是不同路径；间隔默认 7s（下限 5s）。
 * safe_mode 或 未(enabled&&applied) 时本轮跳过。进游戏/切状态导致目标被复位后，
 * 下一个周期即被拉回。 */
static void *fan_daemon_main(void *arg) {
    (void)arg;
    for (;;) {
        if (!pldmgr_utils_safe_mode()) {
            pthread_mutex_lock(&g_fan_mtx);
            if (g_fan_enabled && g_fan_applied) {
                int target = fan_target_in_range(g_fan_target) ? g_fan_target : FAN_TARGET_DEFAULT;
                int fd = open(ICC_FAN_DEVICE, O_RDWR);
                if (fd >= 0) {
                    int hw = -1;
                    if (fan_read_target(fd, &hw) == 0 && hw != target) {
                        if (fan_write_target(fd, target) == 0)
                            pldmgr_log("[PLDMGR] fan target restored: %d -> %d C\n", hw, target);
                    }
                    close(fd);
                }
            }
            pthread_mutex_unlock(&g_fan_mtx);
        }
        sleep(g_fan_interval);
    }
    return NULL;
}

void fan_control_start_daemon(void) {
    if (g_fan_daemon_started)
        return;

    /* 从 config 恢复运行态（重启/升级后沿用上次启用+生效设置） */
    PldmgrConfig cfg;
    config_read(&cfg);
    pthread_mutex_lock(&g_fan_mtx);
    g_fan_enabled  = cfg.fan_enabled ? 1 : 0;
    g_fan_applied  = cfg.fan_applied ? 1 : 0;
    g_fan_target   = fan_target_in_range(cfg.fan_target) ? cfg.fan_target : FAN_TARGET_DEFAULT;
    g_fan_interval = fan_interval_clamp(cfg.fan_interval ? cfg.fan_interval : FAN_INTERVAL_DEFAULT);
    int en = g_fan_enabled, ap = g_fan_applied, tg = g_fan_target, iv = g_fan_interval;
    pthread_mutex_unlock(&g_fan_mtx);

    pldmgr_log("[PLDMGR] fan control init: enabled=%d applied=%d target=%d interval=%ds\n",
               en, ap, tg, iv);

    pthread_t th;
    if (pthread_create(&th, NULL, fan_daemon_main, NULL) == 0) {
        pthread_detach(th);
        g_fan_daemon_started = 1;
    } else {
        pldmgr_log("[PLDMGR] fan control: FAILED to start daemon thread\n");
    }
}
