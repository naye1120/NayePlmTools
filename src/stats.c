#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/mount.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/stat.h>
#include <kenv.h>
#include <ps5/kernel.h>
#include "stats.h"
#include "pldmgr.h"

/* kenv.h 只带 sys/param.h，KENV_MVALLEN/KENV_GET 等常量在 sys/sysctl.h（已引）；
 * 个别 SDK 版本缺定义时兼容补齐 */
#ifndef KENV_MVALLEN
#define KENV_MVALLEN 128
#endif
#ifndef KENV_GET
#define KENV_GET 1
#define KENV_DUMP 5
#endif

/* ── PS5 内核导出（参考 ps5-new-overlay monitor.cpp） ───────────── */
extern int sceKernelGetCpuTemperature(int *temp);
extern int sceKernelGetSocSensorTemperature(int sensor_id, int *temp);
/* get_page_table_stats 直接返回 MB：type 1=RAM, 2=VRAM */
extern int get_page_table_stats(int vm, int type, int *total, int *free);
/* sceKernelGetCurrentFanDuty 的 NID 并非所有 libkernel 都有，声明为 weak，
 * 运行时判空后再调用，避免链接失败。duty 取值范围 0..1024。 */
__attribute__((weak)) int sceKernelGetCurrentFanDuty(uint16_t *duty, uint64_t *chassis);

/* [CN-FORK] 以下三个均来自 ps5-payload-sdk：kernel_get_fw_version 定义在 SDK 的 crt1.o
 * （本工程链接命令默认带入），纯 userland（SYS_dynlib_get_obj_member 读进程内
 * libc_internal 的 proc param，不走内核 rw）；kenv 是 libc 包装的普通 syscall；
 * sceRegMgrGetKeyStr 来自 libSceRegMgr（弱链接：权限不足时返回错误码而非崩溃）。 */
extern unsigned int kernel_get_fw_version(void);
__attribute__((weak)) int sceRegMgrGetKeyStr(const char *name, char *buf);

/* ── [CN-FORK] 主机型号 / 序列号 / 系统版本的官方正解 ───────────────────────
 * 出处：ps5-payload-sdk（作者 John Törnblom / z80，https://github.com/ps5-payload-dev/sdk）
 *   用例 samples/hwinfo/main.c —— 该 sample 中型号与序列号就是两个普通 libkernel
 *   导出调用，不提权、不做内核 rw、不读 inst0.dat。
 * 符号确认：这些 stub 位于 SDK 的 sce_stubs/libkernel_sys.c（不在 libkernel.c，
 *   只翻一个文件会误判为不存在）。
 * 实机印证：manos555555/PS5-Upload-Suite 的 payload/main.c
 *   （https://github.com/manos555555/PS5-Upload-Suite）用同样两个 API 在实机取到
 *   "CFI-xxxx" 形态型号。
 * 全部声明为 weak：SDK 版本缺 stub 时链接不失败，运行时判空即可，与
 *   sceKernelGetCurrentFanDuty 的处理方式一致。
 * 此前本文件走的是 kenv→sysctl→RegMgr→inst0.dat 五路探测（全部拿不到，界面显 —），
 *   并试过进程内提权读 inst0.dat（v0.5.1.53 导致 12.20 HEN 整机死机）——均属绕远路。 */
__attribute__((weak)) int sceKernelGetHwModelName(char *out);
__attribute__((weak)) int sceKernelGetHwSerialNumber(char *out);
/* 系统版本字符串形态，用作既有 BCD 解码的交叉校验/回退。结构体布局取自
 * drakmor/ps5-hwinfo（https://github.com/drakmor/ps5-hwinfo）main.c：
 * u32 reserved0; u32 reserved1; char version[0x1c]; u32 raw;  —— version 需去前导空格。
 * SDK stub 中确认存在的是带 Genuine 前缀的那个，故优先它。 */
typedef struct {
    uint32_t reserved0;
    uint32_t reserved1;
    char     version[0x1c];
    uint32_t raw;
} pldmgr_sw_version_t;
__attribute__((weak)) int sceKernelGetGenuineProsperoSystemSwVersion(void *out);
/* 硬件平台代号：纯只读内核常量，出处 ps5-payload-sdk samples/test_privileges/main.c
 * （作者 John Törnblom）。仅在 /sysinfo_probe 诊断端点读取，不进常规轮询路径。 */
__attribute__((weak)) uint64_t sceKernelGetMainSocId(void);

/* [CN-FORK] .46 实机教训：kenv/sysctlbyname 对不存在的变量不报错、直接往缓冲区
 * 塞垃圾（实测序列号位探出 "r#d"）。所有字符串探测结果必须过严格格式白名单，
 * 不合格一律丢弃。 */
static int str_is_alnum_range(const char *s, int lo, int hi) {
    int n = 0;
    for (; s[n]; n++) {
        char c = s[n];
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return 0;
    }
    return n >= lo && n <= hi;
}
/* 主机型号：只认官方形态 CUH-/CFI-（可带区域后缀/空格）。
 * .51 实机教训：sysctl hw.model 的 "100-000000891" 是内核内部标识，
 * 不是官方页显示的 CFI 型号，宁可显 — 也不显示它 */
static int model_value_ok(const char *s) {
    if (strncmp(s, "CUH-", 4) != 0 && strncmp(s, "CFI-", 4) != 0) return 0;
    const char *p = s + 4;
    int d = 0;
    while (*p >= '0' && *p <= '9') { p++; d++; }
    return d >= 1 && d <= 3 && (*p == '\0' || *p == '-' || *p == ' ') && *p != '\0' ?
        (d >= 1 && (p[0] == '\0' || ((p[0] == '-' || p[0] == ' ') && p[1] >= '0' && p[1] <= '9'))) : 0;
}
/* 序列号：12~17 位纯大写字母数字（实机 "F32B018RF11266783" 17 位，正好卡上界） */
static int serial_value_ok(const char *s) { return str_is_alnum_range(s, 12, 17); }
/* 系统版本：数字+小数点（1~3 段） */
static int firmware_value_ok(const char *s) {
    int dot = 0, digit = 0;
    for (const char *p = s; *p; p++) {
        if (*p == '.') { if (++dot > 2 || p == s || !digit) return 0; digit = 0; continue; }
        if (*p < '0' || *p > '9') return 0;
        digit++;
    }
    return dot >= 1 && digit > 0;
}
/* 清洗 sysctlbyname/kenv 原始返回：去尾部空白/NUL，过校验才保留 */
static void sanitize_str(char *s, int (*ok)(const char *)) {
    size_t i = 0;
    while (s[i] && (unsigned char)s[i] <= ' ') i++;
    if (i) memmove(s, s + i, strlen(s + i) + 1);
    size_t len = strlen(s);
    while (len && (unsigned char)s[len - 1] <= ' ') s[--len] = '\0';
    if (!ok(s)) s[0] = '\0';
}

/* [CN-FORK] 官方 libkernel API 返回值的校验：数据源本身可信，只做"非空 + 全可打印 +
 * 长度合理"检查。不能复用 model_value_ok/serial_value_ok 那套严格格式白名单，否则会
 * 把 "PlayStation 5" 这类合法但非 CFI/CUH 形态的官方值误杀；而 kenv/sysctl 那些
 * 会塞垃圾的源仍继续走严格白名单（.46 教训）。 */
static int trusted_str_ok(const char *s) {
    size_t n = 0;
    for (; s[n]; n++) {
        unsigned char c = (unsigned char)s[n];
        if (c < 0x20 || c > 0x7e) return 0;
        if (n > 40) return 0;
    }
    return n >= 3;
}

/* [CN-FORK] 官方 API 一次性探测结果（含返回码，供 /sysinfo_probe 定位）。首探后缓存，
 * 失败也缓存——没权限就永远没权限，不重复调 Sony 非可重入内核 API。 */
static char g_api_model[64], g_api_serial[64], g_api_swver[32];
static int g_api_model_rc = -999, g_api_serial_rc = -999, g_api_swver_rc = -999;
static int hwid_api_probed = 0;
static void hwid_api_probe(void) {
    if (hwid_api_probed) return;
    hwid_api_probed = 1;
    char t[64];

    if (sceKernelGetHwModelName) {
        memset(t, 0, sizeof(t));
        g_api_model_rc = sceKernelGetHwModelName(t);
        t[sizeof(t) - 1] = '\0';
        if (g_api_model_rc == 0) sanitize_str(t, trusted_str_ok);
        else t[0] = '\0';
        snprintf(g_api_model, sizeof(g_api_model), "%s", t);
    }
    if (sceKernelGetHwSerialNumber) {
        memset(t, 0, sizeof(t));
        g_api_serial_rc = sceKernelGetHwSerialNumber(t);
        t[sizeof(t) - 1] = '\0';
        if (g_api_serial_rc == 0) sanitize_str(t, trusted_str_ok);
        else t[0] = '\0';
        snprintf(g_api_serial, sizeof(g_api_serial), "%s", t);
    }
    if (sceKernelGetGenuineProsperoSystemSwVersion) {
        pldmgr_sw_version_t sv;
        memset(&sv, 0, sizeof(sv));
        g_api_swver_rc = sceKernelGetGenuineProsperoSystemSwVersion(&sv);
        if (g_api_swver_rc == 0) {
            char v[32];
            memset(v, 0, sizeof(v));
            memcpy(v, sv.version, sizeof(sv.version));   /* version 不保证 NUL 结尾 */
            v[sizeof(v) - 1] = '\0';
            sanitize_str(v, firmware_value_ok);
            snprintf(g_api_swver, sizeof(g_api_swver), "%s", v);
        }
    }
    pldmgr_log("[PLDMGR] libkernel hw api: model rc=%d '%s', serial rc=%d '%s', swver rc=%d '%s'\n",
               g_api_model_rc, g_api_model, g_api_serial_rc, g_api_serial,
               g_api_swver_rc, g_api_swver);
}

#define VM_SYSTEM       1
#define PAGE_TABLE_RAM  1
#define PAGE_TABLE_VRAM 2
#define FAN_DUTY_MAX    1024

/* CPU 采样差分：kern.cp_time[5] = user/nice/sys/intr/idle */
static long prev_cp_time[5] = {0};

static float get_cpu_usage(void) {
    long cp_time[5];
    size_t size = sizeof(cp_time);

    /* CTL_KERN = 1, KERN_CP_TIME = 40 */
    int mib[2] = {1, 40};
    if (sysctl(mib, 2, &cp_time, &size, NULL, 0) < 0) {
        if (sysctlbyname("kern.cp_time", &cp_time, &size, NULL, 0) < 0) {
            return 0.0f;
        }
    }

    long total_diff = 0;
    for (int i = 0; i < 5; i++) {
        total_diff += (cp_time[i] - prev_cp_time[i]);
    }

    if (total_diff <= 0) {
        memcpy(prev_cp_time, cp_time, sizeof(cp_time));
        return 0.0f;
    }

    long idle_diff = cp_time[4] - prev_cp_time[4]; /* CP_IDLE = 4 */
    float usage = 100.0f * (1.0f - ((float)idle_diff / (float)total_diff));

    memcpy(prev_cp_time, cp_time, sizeof(cp_time));
    return usage < 0 ? 0 : (usage > 100 ? 100 : usage);
}

/* 开机时长：kern.boottime 与当前时间差，格式化为 "2h 34m 12s" / "34m 12s"（带秒，便于前端确认轮询在刷新）*/
static void get_uptime_string(char *out, size_t out_size) {
    out[0] = '\0';
    struct timeval boottime;
    size_t size = sizeof(boottime);
    int mib[2] = {1, 21}; /* CTL_KERN, KERN_BOOTTIME */
    if (sysctl(mib, 2, &boottime, &size, NULL, 0) != 0)
        return;

    long uptime_sec = (long)(time(NULL) - boottime.tv_sec);
    if (uptime_sec < 0) uptime_sec = 0;

    long h = uptime_sec / 3600;
    long m = (uptime_sec % 3600) / 60;
    long s = uptime_sec % 60;
    if (h > 0)
        snprintf(out, out_size, "%ldh %ldm %lds", h, m, s);
    else
        snprintf(out, out_size, "%ldm %lds", m, s);
}

/* [CN-FORK] /root/data/inst0.dat：PS4/PS5 安装信息文件，官方“设定→主机→主机信息”
 * 的型号/序列号就存在这里（offset 0x14 型号 16 字节、0x40 序列号 24 字节，
 * 社区通用布局）。首探后缓存（含读失败——没权限就永远没权限，不重复 IO）。
 * 实机证据：官方页型号 "CFI-1209A 01"、序列号 "F32B018RF11266783"，而 sysctl
 * hw.model 是内部标识 "100-000000891"，两者不是一回事。 */
/* [CN-FORK] 进程内提权（v0.5.1.53 引入）：用 SDK crt1.o 自带的内核原语把自身 cred 提成
 * root、摘除 prison/rootdir/jaildir，让 userland open() 能读 /root/data/inst0.dat。
 * ⚠ v0.5.1.54 实机事故：.53 在 12.20 HEN 上运行后整机死机（sysdebug RPC 族调用
 * kernel_set_ucred 系列与 kernel_get_root_vnode 疑似把内核卡死），默认永久禁用，代码仅作
 * 存档；重新实验前必须先定位确切崩溃点，且在可强制断电的环境下进行。 */
#define PLDMGR_ESCALATE_TO_ROOT_ENABLE 0
static int g_escalated = -1;
static void pldmgr_escalate_to_root(void) {
#if PLDMGR_ESCALATE_TO_ROOT_ENABLE
    if (g_escalated >= 0) return;
    pid_t self = getpid();
    intptr_t proc = kernel_get_proc(self);
    if (!proc) {
        pldmgr_log("[PLDMGR] escalate: kernel_get_proc failed (no kernel rw?)\n");
        g_escalated = 0;
        return;
    }
    int ok = 1;
    ok |= kernel_set_ucred_uid(self, 0) != 0;
    ok |= kernel_set_ucred_ruid(self, 0) != 0;
    ok |= kernel_set_ucred_svuid(self, 0) != 0;
    ok |= kernel_set_ucred_rgid(self, 0) != 0;
    ok |= kernel_set_ucred_svgid(self, 0) != 0;
    ok |= kernel_set_ucred_prison(self, 0) != 0;
    intptr_t rootvnode = kernel_get_root_vnode();
    if (rootvnode) {
        ok |= kernel_set_proc_rootdir(self, rootvnode) != 0;
        ok |= kernel_set_proc_jaildir(self, rootvnode) != 0;
    }
    g_escalated = (ok == 0 && kernel_get_ucred_uid(self) == 0) ? 1 : 0;
    pldmgr_log("[PLDMGR] escalate: %s (uid=%d)\n", g_escalated ? "OK" : "partial/failed",
               (int)kernel_get_ucred_uid(self));
#else
    g_escalated = -1; /* 已禁用：不给 inst0 探测增加任何内核调用风险 */
#endif
}

static char g_inst0_model[32], g_inst0_serial[32];
static int inst0_probed = 0;
/* .51 实机：/root/data/inst0.dat open=-1（无权限），多候选路径再试一轮，
 * 哪条可读由 /sysinfo_probe 输出定位；都读不到则型号/序列号回退旧探测/显 — */
static const char *const g_inst0_paths[] = {
    "/root/data/inst0.dat", "/system_data/inst0.dat", "/system_data/priv/inst0.dat",
    "/dev_flash/data/inst0.dat", "/user/data/inst0.dat",
};
static void inst0_probe(void) {
    if (inst0_probed) return;
    inst0_probed = 1;
    pldmgr_escalate_to_root();
    unsigned char buf[128];
    ssize_t rd = -1;
    for (unsigned i = 0; i < sizeof(g_inst0_paths) / sizeof(g_inst0_paths[0]); i++) {
        int fd = open(g_inst0_paths[i], O_RDONLY);
        if (fd < 0) continue;
        rd = read(fd, buf, sizeof(buf));
        close(fd);
        pldmgr_log("[PLDMGR] inst0.dat read from %s (%zd bytes)\n", g_inst0_paths[i], rd);
        break;
    }
    if (rd < 0x50) {
        if (rd < 0) pldmgr_log("[PLDMGR] inst0.dat: no candidate path readable\n");
        else pldmgr_log("[PLDMGR] inst0.dat short read: %zd\n", rd);
        return;
    }
    for (int i = 0; i < 16; i++) {
        char c = (char)buf[0x14 + i];
        if (!c) break;
        if ((unsigned char)c < 0x20 || (unsigned char)c > 0x7e) { g_inst0_model[0] = '\0'; break; }
        if (i < (int)sizeof(g_inst0_model) - 1) g_inst0_model[i] = c;
        else { g_inst0_model[0] = '\0'; break; }
    }
    for (int i = 0; i < 24; i++) {
        char c = (char)buf[0x40 + i];
        if (!c) break;
        if ((unsigned char)c < 0x20 || (unsigned char)c > 0x7e) { g_inst0_serial[0] = '\0'; break; }
        if (i < (int)sizeof(g_inst0_serial) - 1) g_inst0_serial[i] = c;
        else { g_inst0_serial[0] = '\0'; break; }
    }
    sanitize_str(g_inst0_model, model_value_ok);
    sanitize_str(g_inst0_serial, serial_value_ok);
    pldmgr_log("[PLDMGR] inst0.dat model='%s' serial='%s'\n", g_inst0_model, g_inst0_serial);
}

/* 主机型号：【v0.5.1.55 修正】主源改为官方 libkernel API sceKernelGetHwModelName()
 * （出处 ps5-payload-sdk samples/hwinfo，作者 John Törnblom）；inst0.dat → kenv /
 * sysctlbyname / RegMgr 降为兜底。全部过格式校验；首探后缓存，前端拿空串显示占位。 */
static void get_model_string(char *out, size_t out_size) {
    static char cached[64];
    static int probed = 0;
    if (!probed) {
        probed = 1;
        cached[0] = '\0';
        char v[KENV_MVALLEN + 1];
        hwid_api_probe();
        inst0_probe();
        if (g_api_model[0]) {
            snprintf(cached, sizeof(cached), "%s", g_api_model);
            pldmgr_log("[PLDMGR] hw model: from sceKernelGetHwModelName()\n");
        } else if (g_inst0_model[0]) {
            snprintf(cached, sizeof(cached), "%s", g_inst0_model);
            pldmgr_log("[PLDMGR] hw model: from inst0.dat\n");
        } else {
            memset(v, 0, sizeof(v));
            if (kenv(KENV_GET, "hw.model", v, sizeof(v)) > 0) {
                v[sizeof(v) - 1] = '\0';
                sanitize_str(v, model_value_ok);
            }
            if (!v[0]) {
                size_t sz = sizeof(v) - 1;
                memset(v, 0, sizeof(v));
                if (sysctlbyname("hw.model", v, &sz, NULL, 0) == 0) {
                    v[sizeof(v) - 1] = '\0';
                    sanitize_str(v, model_value_ok);
                }
            }
            if (!v[0] && sceRegMgrGetKeyStr) {
                memset(v, 0, sizeof(v));
                if (sceRegMgrGetKeyStr("SYS_STYLE/system_version", v) == 0) {
                    v[sizeof(v) - 1] = '\0';
                    sanitize_str(v, model_value_ok);
                }
            }
            snprintf(cached, sizeof(cached), "%s", v);
            pldmgr_log("[PLDMGR] hw model probe: '%s'\n", cached);
        }
    }
    snprintf(out, out_size, "%s", cached);
}

/* [CN-FORK] 系统版本：SDK crt1.o 的 kernel_get_fw_version()。实机铁证定格式：
 * raw=0x12200005 对应官方页 12.20.00 → 布局 (major BCD<<24)|(minor BCD<<16)|build，
 * 字节按 BCD 读（0x12=12、0x20=20）；SDK 文档的 0x900020→9.00 是同一布局
 * （major=0x09、minor=0x00、build=0x0020）。历史弯路：.46 按 >>20/>>8 十进制解出
 * 34.00，.48 按 >>24 十进制解被合理性拦成 —，.51 按 >>20 BCD 解又拦成 —。 */
static int bcd_byte(unsigned int b) {
    if (((b >> 4) & 0xf) > 9 || (b & 0xf) > 9) return -1;
    return (int)(((b >> 4) & 0xf) * 10 + (b & 0xf));
}
static int fw_raw_sane(unsigned int v) {
    int major = bcd_byte((v >> 24) & 0xff);
    int minor = bcd_byte((v >> 16) & 0xff);
    if (major < 1 || major > 13) return 0;   /* 当前已知最高固件 13.x */
    if (minor < 0 || minor > 99) return 0;
    return (v & 0xffff) <= 9999;
}
static void get_firmware_string(char *out, size_t out_size) {
    static char cached[16];
    static int probed = 0;
    if (!probed) {
        probed = 1;
        cached[0] = '\0';
        unsigned int v = kernel_get_fw_version();
        if (fw_raw_sane(v)) {
            /* 只显主次两段，与官方页一致；低 16 位含义未定（实机 0x0005 但官方显 00），
             * 仅由 probe 输出供分析 */
            snprintf(cached, sizeof(cached), "%d.%02d", bcd_byte((v >> 24) & 0xff), bcd_byte((v >> 16) & 0xff));
        } else {
            /* BCD 解不出（新固件改了编码等）→ 官方版本字符串→kern.osrelease 两级回退。
             * 出处 ps5-payload-sdk samples/hwinfo + drakmor/ps5-hwinfo 的结构体布局 */
            hwid_api_probe();
            if (g_api_swver[0]) {
                snprintf(cached, sizeof(cached), "%s", g_api_swver);
            } else {
                char rel[64];
                size_t sz = sizeof(rel) - 1;
                memset(rel, 0, sizeof(rel));
                if (sysctlbyname("kern.osrelease", rel, &sz, NULL, 0) == 0) {
                    rel[sizeof(rel) - 1] = '\0';
                    sanitize_str(rel, firmware_value_ok);
                    snprintf(cached, sizeof(cached), "%s", rel);
                }
            }
        }
        pldmgr_log("[PLDMGR] fw version raw=0x%x -> '%s'\n", v, cached);
    }
    snprintf(out, out_size, "%s", cached);
}

/* [CN-FORK] 序列号：【v0.5.1.55 修正】主源改为官方 libkernel API
 * sceKernelGetHwSerialNumber()（出处 ps5-payload-sdk samples/hwinfo，作者 John Törnblom）；
 * inst0.dat（与官方主机信息页同源）→ kenv/sysctl 候选名降为兜底（实机全部 ENOENT/空，
 * 历史教训：不过白名单的非空值是垃圾），探不到即空。 */
static void get_serial_string(char *out, size_t out_size) {
    static char cached[64];
    static int probed = 0;
    if (!probed) {
        probed = 1;
        cached[0] = '\0';
        hwid_api_probe();
        inst0_probe();
        if (g_api_serial[0]) {
            snprintf(cached, sizeof(cached), "%s", g_api_serial);
            pldmgr_log("[PLDMGR] serial: from sceKernelGetHwSerialNumber()\n");
        } else if (g_inst0_serial[0]) {
            snprintf(cached, sizeof(cached), "%s", g_inst0_serial);
            pldmgr_log("[PLDMGR] serial: from inst0.dat\n");
        }
        static const char *names[] = {"hw.serial", "kern.serial", "hw.srom0.serial"};
        for (unsigned i = 0; i < 3 && !cached[0]; i++) {
            char v[KENV_MVALLEN + 1];
            memset(v, 0, sizeof(v));
            if (kenv(KENV_GET, names[i], v, sizeof(v)) > 0) {
                v[sizeof(v) - 1] = '\0';
                sanitize_str(v, serial_value_ok);
                if (v[0]) { snprintf(cached, sizeof(cached), "%s", v); break; }
            }
            size_t sz = sizeof(v) - 1;
            memset(v, 0, sizeof(v));
            if (sysctlbyname(names[i], v, &sz, NULL, 0) == 0) {
                v[sizeof(v) - 1] = '\0';
                sanitize_str(v, serial_value_ok);
                if (v[0]) snprintf(cached, sizeof(cached), "%s", v);
            }
        }
        pldmgr_log("[PLDMGR] serial probe: '%s'\n", cached);
    }
    snprintf(out, out_size, "%s", cached);
}

/* [CN-FORK] 诊断端点：dump 整个 kenv 空间（hello_kenv 同款），实机跑一次就能从日志页
 * 确定 PS5 上到底有哪些内核环境变量可用（型号/序列号线索全在里面）。 */
int stats_sysinfo_probe(char *buf, size_t size) {
    int n, pos = 0;
    char *kv = NULL;
    char fw[16], model[64], serial[64];

    get_firmware_string(fw, sizeof(fw));
    get_model_string(model, sizeof(model));
    get_serial_string(serial, sizeof(serial));
    unsigned int fwr = kernel_get_fw_version();
    pos += snprintf(buf + pos, size - pos,
                    "fw=0x%x (%s) build=0x%x\nmodel='%s'\nserial='%s'\nescalated=%d (disabled)\n", fwr,
                    fw[0] ? fw : "-", fwr & 0xffff, model, serial, g_escalated);

    /* [CN-FORK] 官方 libkernel API 逐个体检：符号在不在（weak 未被加载器解析时为 no）、
     * 返回码、过校验后的取值。实机跑一次就能分清“没这个符号”/“有但被拒”/“成功但空值”
     * 三种情况，不用再来一轮猜。 */
    hwid_api_probe();
    pos += snprintf(buf + pos, size - pos,
                    "\n== libkernel hw api ==\n"
                    "sceKernelGetHwModelName        sym=%s rc=%d val='%s'\n"
                    "sceKernelGetHwSerialNumber     sym=%s rc=%d val='%s'\n"
                    "sceKernelGetGenuineProsperoSystemSwVersion sym=%s rc=%d val='%s'\n",
                    sceKernelGetHwModelName ? "yes" : "no", g_api_model_rc, g_api_model,
                    sceKernelGetHwSerialNumber ? "yes" : "no", g_api_serial_rc, g_api_serial,
                    sceKernelGetGenuineProsperoSystemSwVersion ? "yes" : "no", g_api_swver_rc, g_api_swver);
    if (sceKernelGetMainSocId) {
        pos += snprintf(buf + pos, size - pos, "sceKernelGetMainSocId          val=0x%016llx\n",
                        (unsigned long long)sceKernelGetMainSocId());
    }
#ifdef KERNEL_ADDRESS_TARGETID
    /* 只读内核常量，出处 ps5-payload-sdk samples/test_privileges/main.c */
    pos += snprintf(buf + pos, size - pos, "TARGETID=0x%02x SECURITY_FLAGS=0x%02x UTOKEN_FLAGS=0x%02x\n",
                    (unsigned)kernel_getchar(KERNEL_ADDRESS_TARGETID),
                    (unsigned)kernel_getchar(KERNEL_ADDRESS_SECURITY_FLAGS),
                    (unsigned)kernel_getchar(KERNEL_ADDRESS_UTOKEN_FLAGS));
#endif
    if (pos >= (int)size) pos = (int)size - 1;

    /* inst0.dat 证据：逐候选路径 open 结果（裸 errno），定位真实可读位置 */
    inst0_probe();
    {
        unsigned char b[128];
        for (unsigned i = 0; i < sizeof(g_inst0_paths) / sizeof(g_inst0_paths[0]); i++) {
            errno = 0;
            int fd = open(g_inst0_paths[i], O_RDONLY);
            pos += snprintf(buf + pos, size - pos, "inst0[%s] open=%d errno=%d\n", g_inst0_paths[i], fd, errno);
            if (fd < 0 || pos >= (int)size - 8) { if (fd >= 0) close(fd); continue; }
            ssize_t rd = read(fd, b, sizeof(b));
            close(fd);
            pos += snprintf(buf + pos, size - pos, "  read=%zd model14='", rd);
            if (rd >= 0x50) {
                for (int j = 0; j < 16 && pos < (int)size - 8; j++)
                    pos += snprintf(buf + pos, size - pos, (isprint(b[0x14 + j]) ? "%c" : "\\x%02x"), b[0x14 + j]);
                pos += snprintf(buf + pos, size - pos, "'\n  serial40='");
                if (rd >= 0x58) {
                    for (int j = 0; j < 24 && pos < (int)size - 8; j++)
                        pos += snprintf(buf + pos, size - pos, (isprint(b[0x40 + j]) ? "%c" : "\\x%02x"), b[0x40 + j]);
                }
            }
            pos += snprintf(buf + pos, size - pos, "'\n");
        }
    }

    /* 原始证据（未过白名单的裸值）：用于在 PC 侧分析垃圾值的来源和真实格式 */
    static const char *cand[] = {"hw.model", "kern.osrelease", "hw.serial", "kern.serial", "hw.srom0.serial"};
    for (unsigned i = 0; i < 5; i++) {
        char v[KENV_MVALLEN + 1];
        memset(v, 0, sizeof(v));
        int kr = kenv(KENV_GET, cand[i], v, sizeof(v));
        v[sizeof(v) - 1] = '\0';
        pos += snprintf(buf + pos, size - pos, "kenv[%s]=%d '", cand[i], kr);
        for (int j = 0; j < (int)sizeof(v) && v[j] && pos < (int)size - 8; j++)
            pos += snprintf(buf + pos, size - pos, (isprint((unsigned char)v[j]) ? "%c" : "\\x%02x"), (unsigned char)v[j]);
        pos += snprintf(buf + pos, size - pos, "'\n");
        size_t sz = sizeof(v) - 1;
        memset(v, 0, sizeof(v));
        int sr = sysctlbyname(cand[i], v, &sz, NULL, 0);
        v[sizeof(v) - 1] = '\0';
        pos += snprintf(buf + pos, size - pos, "sysctl[%s]=%d '", cand[i], sr);
        for (unsigned j = 0; j < sz && v[j] && pos < (int)size - 8; j++)
            pos += snprintf(buf + pos, size - pos, (isprint((unsigned char)v[j]) ? "%c" : "\\x%02x"), (unsigned char)v[j]);
        pos += snprintf(buf + pos, size - pos, "'\n");
    }

    n = kenv(KENV_DUMP, 0, NULL, 0);
    if (n > 0 && (kv = malloc(n + 1))) {
        if (kenv(KENV_DUMP, 0, kv, n) > 0) {
            kv[n] = '\0';
            for (char *p = kv; *p && pos < (int)size - 2; p += strlen(p) + 1) {
                pos += snprintf(buf + pos, size - pos, "%s\n", p);
            }
        }
        free(kv);
    } else {
        pos += snprintf(buf + pos, size - pos, "kenv KENV_DUMP failed (n=%d)\n", n);
    }
    return pos;
}

/* ── FPS + 当前游戏：移植自 ps5mon（DCE 换帧差分 / OnionHEN seqlock；       */
/*    KERN_PROC + sceKernelGetAppInfo 找前台大应用，app.db/param.sfo 取名字）*/

/* ps5mon 实机 hex 校准：app_id@0, title_id@16, app_type@26 */
typedef struct {
    uint32_t app_id;        /* @0  */
    uint32_t unk4;          /* @4  */
    uint64_t unk8;          /* @8  */
    char     title_id[10];  /* @16 */
    uint16_t app_type;      /* @26 */
    char     unknown2[0x60];
} __attribute__((packed)) stats_app_info_t;
extern int sceKernelGetAppInfo(pid_t pid, stats_app_info_t *info);

static long now_ms(void) {
    struct timeval tv; gettimeofday(&tv, NULL);
    return (long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

/* JSON 字符串转义，但保留 UTF-8 高位字节（游戏名可能含中文） */
static void stats_escape_utf8(const char *src, char *dst, size_t dst_size) {
    size_t pos = 0;
    if (dst_size == 0) return;
    for (size_t i = 0; src[i] && pos + 1 < dst_size; i++) {
        unsigned char c = (unsigned char)src[i];
        if ((c == '"' || c == '\\') && pos + 2 < dst_size) { dst[pos++] = '\\'; dst[pos++] = (char)c; }
        else if (c == '\n' && pos + 2 < dst_size) { dst[pos++] = '\\'; dst[pos++] = 'n'; }
        else if (c == '\r' && pos + 2 < dst_size) { dst[pos++] = '\\'; dst[pos++] = 'r'; }
        else if (c == '\t' && pos + 2 < dst_size) { dst[pos++] = '\\'; dst[pos++] = 't'; }
        else if (c >= 0x20) dst[pos++] = (char)c; /* >=0x80 的 UTF-8 字节原样输出，JSON 允许 */
    }
    dst[pos] = '\0';
}

/* ---- FPS（ps5-new-overlay / ps5mon 同款两级降级）---- */
typedef struct { uint64_t selector, mask, output, reserved[3]; } dce_arg_t;
#define DCE_IOC_GET 0x80308217UL
static int      s_dce_fd = -1;
static uint64_t s_flip_prev;
static struct timespec s_flip_t0;
static float    s_fps = -1.0f;   /* -1 = 不可用 */

static void sample_fps(void) {
    /* 源1：OnionHEN seqlock 共享文件，magic 'OFPS'（两种字节序都比对） */
    int fd = open("/system_tmp/fps_sample", O_RDONLY);
    if (fd >= 0) {
        uint8_t buf[128] = {0};
        ssize_t n = read(fd, buf, sizeof(buf));
        close(fd);
        if (n >= 48) {
            uint32_t magic; memcpy(&magic, buf, 4);
            if (magic == 0x4F465053u || magic == 0x5350464Fu) {
                if (buf[12]) {
                    float f; memcpy(&f, buf + 16, 4);
                    if (f > 0.0f && f <= 245.0f) { s_fps = f; return; }
                }
            }
        }
    }
    /* 源2：/dev/dce 显示控制器换帧(flip)硬件计数器差分 */
    if (s_dce_fd < 0) s_dce_fd = open("/dev/dce", O_RDWR);
    if (s_dce_fd >= 0) {
        uint8_t out[0x60] = {0};
        dce_arg_t arg;
        memset(&arg, 0, sizeof(arg));
        arg.selector = 0x10000000AULL;
        arg.mask     = 0x8000000000ULL;
        arg.output   = (uint64_t)(uintptr_t)out;
        int rc = ioctl(s_dce_fd, DCE_IOC_GET, &arg);
        if (rc < 0) rc = ioctl(s_dce_fd, (unsigned long)0xFFFFFFFF80308217ULL, &arg);
        if (rc == 0) {
            uint64_t flips; memcpy(&flips, out + 8, 8);
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            if (s_flip_t0.tv_sec == 0) {
                s_flip_prev = flips;
                s_flip_t0 = now;
            } else {
                double dt = (double)(now.tv_sec - s_flip_t0.tv_sec) +
                            (double)(now.tv_nsec - s_flip_t0.tv_nsec) * 1e-9;
                if (dt >= 0.2) {
                    uint64_t d = flips >= s_flip_prev ? flips - s_flip_prev : 0;
                    float raw = (float)((double)d / dt);
                    if (raw >= 0.0f && raw <= 245.0f) s_fps = raw;
                    s_flip_prev = flips;
                    s_flip_t0 = now;
                }
            }
        }
    }
}

/* ---- param.sfo 解析（游戏名兜底）---- */
static uint32_t st_le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t st_le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

static int sfo_str(const uint8_t *d, size_t sz, const char *key, char *out, size_t osz) {
    if (!d || sz < 20 || st_le32(d) != 0x46505300u /* "\0PSF" */) return -1;
    uint32_t ko = st_le32(d + 8), vo = st_le32(d + 12), n = st_le32(d + 16);
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t *e = d + 20 + i * 20;
        if ((size_t)(e - d) + 20 > sz) return -1;
        uint32_t k = ko + st_le32(e), fmt = st_le16(e + 8), v = vo + st_le32(e + 16);
        if (fmt != 2 || k >= sz || v >= sz) continue;
        if (strcmp((const char *)d + k, key) != 0) continue;
        size_t l = 0;
        while (v + l < sz && l < osz - 1 && d[v + l]) l++;
        memcpy(out, d + v, l);
        out[l] = 0;
        return 0;
    }
    return -1;
}

static int sfo_file_name(const char *path, char *name, size_t nsz) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -errno;
    off_t fsz = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    if (fsz <= 20 || fsz > (1 << 20)) { close(fd); return -2; }
    uint8_t *img = (uint8_t *)malloc((size_t)fsz);
    int rc = -2;
    if (img && read(fd, img, (size_t)fsz) == fsz) {
        name[0] = 0;
        if (sfo_str(img, (size_t)fsz, "title", name, nsz) == 0 && name[0]) rc = 0;
        else if (sfo_str(img, (size_t)fsz, "TITLE", name, nsz) == 0 && name[0]) rc = 0;
    }
    free(img);
    close(fd);
    return rc;
}

/* ---- 极简 SQLite 只读解析：定位 tbl_contentinfo，按 titleId 取 titleName ---- */
#define ST_GVS ((uint64_t)~0ull)

static uint64_t st_gvar(const uint8_t *d, size_t sz, size_t *o) {
    uint64_t v = 0;
    for (int i = 0; i < 9; i++) {
        if (*o >= sz) return ST_GVS;
        uint8_t b = d[(*o)++];
        if (i == 8) return (v << 8) | b;
        v = (v << 7) | (b & 0x7f);
        if (!(b & 0x80)) return v;
    }
    return ST_GVS;
}
static uint16_t st_be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static uint32_t st_be32(const uint8_t *p) { return (uint32_t)(st_be16(p) << 16 | st_be16(p + 2)); }

static int st_rec_col(const uint8_t *d, size_t sz, size_t ro, int idx,
                      const uint8_t **tp, size_t *tl, uint64_t *num) {
    size_t o = ro;
    uint64_t hsz = st_gvar(d, sz, &o);
    if (hsz == ST_GVS || hsz == 0 || ro + hsz > sz) return -1;
    size_t bo = ro + hsz;
    for (int i = 0; i <= idx; i++) {
        uint64_t st = st_gvar(d, sz, &o);
        if (st == ST_GVS) return -1;
        size_t len;
        int kind;
        uint64_t small = 0;
        int hassmall = 0;
        if (st == 0) { len = 0; kind = 0; }
        else if (st >= 1 && st <= 7) { len = st == 5 ? 6 : (st >= 6 ? 8 : st); kind = 3; }
        else if (st == 8) { len = 0; kind = 3; small = 0; hassmall = 1; }
        else if (st == 9) { len = 0; kind = 3; small = 1; hassmall = 1; }
        else if (st == 10) { len = 0; kind = 3; small = 48; hassmall = 1; }
        else if (st == 11) { len = 0; kind = 3; small = 1; hassmall = 1; }
        else if (st >= 12) { len = (st - 12) / 2; kind = (st & 1) ? 2 : 1; }
        else { len = 0; kind = 1; }
        if (bo + len > sz) return -1;
        if (i == idx) {
            if (kind == 2) { if (tp) *tp = d + bo; if (tl) *tl = len; return 2; }
            if (kind == 3) {
                if (num) {
                    if (hassmall) *num = small;
                    else { uint64_t v = 0; for (size_t k = 0; k < len; k++) v = v << 8 | d[bo + k]; *num = v; }
                }
                return 3;
            }
            return kind == 0 ? 0 : -1;
        }
        bo += len;
    }
    return -1;
}

typedef int (*st_rec_cb)(const uint8_t *, size_t, size_t, void *);

static int st_bwalk(const uint8_t *d, size_t sz, uint32_t pg, uint32_t pgsz, int depth, st_rec_cb cb, void *arg) {
    uint64_t off64 = (uint64_t)(pg - 1) * pgsz;
    if (!pg || pg > 2000000 || off64 + 120 > sz) return 0;
    size_t off = (size_t)off64;
    size_t hdr = off + (pg == 1 ? 100 : 0);
    uint8_t flags = d[hdr];
    int ncell = st_be16(d + hdr + 3);
    if (ncell > 4096) return 0;
    if (flags != 13 && flags != 5) return 0;
    if (flags == 5 && depth >= 6) return 0;
    size_t cpt = hdr + (flags == 5 ? 12 : 8);
    for (int i = 0; i < ncell; i++) {
        size_t cp = cpt + (size_t)i * 2;
        if (cp + 2 > sz) return 0;
        size_t po = off + st_be16(d + cp);
        if (po + 2 > sz) return 0;
        if (flags == 13) {
            size_t ro = po;
            if (st_gvar(d, sz, &ro) == ST_GVS || st_gvar(d, sz, &ro) == ST_GVS) continue;
            if (cb(d, sz, ro, arg)) return 1;
        } else {
            if (st_bwalk(d, sz, st_be32(d + po), pgsz, depth + 1, cb, arg)) return 1;
        }
    }
    if (flags == 5) {
        uint32_t rm = st_be32(d + hdr + 8);
        if (st_bwalk(d, sz, rm, pgsz, depth + 1, cb, arg)) return 1;
    }
    return 0;
}

typedef struct { uint32_t root; int icon_col; } st_master_arg;

/* 从 CREATE TABLE 语句里找 iconPath 列的序号（顶层逗号项朴素解析；值格式在读取时再验） */
static int st_sql_icon_col(const uint8_t *sp, size_t sl) {
    size_t p = 0;
    while (p < sl && sp[p] != '(') p++;
    if (p >= sl) return -1;
    p++;
    int idx = 0, depth = 1;
    while (p < sl) {
        while (p < sl && isspace((unsigned char)sp[p])) p++;
        size_t s2 = p;
        if (p < sl && (sp[p] == '"' || sp[p] == '`' || sp[p] == '[')) {
            char q = sp[p] == '[' ? ']' : (char)sp[p];
            p++;
            s2 = p;
            while (p < sl && sp[p] != (uint8_t)q) p++;
        } else {
            while (p < sl && !isspace((unsigned char)sp[p]) && sp[p] != '(' && sp[p] != ',' && sp[p] != ')') p++;
        }
        size_t e2 = p;
        if (e2 > s2) {
            if (e2 - s2 == 8 && strncasecmp((const char *)(sp + s2), "iconPath", 8) == 0) return idx;
            idx++;
        }
        while (p < sl) {
            if (sp[p] == '(') depth++;
            else if (sp[p] == ')') { depth--; if (!depth) return -1; }
            else if (sp[p] == ',' && depth == 1) { p++; break; }
            p++;
        }
    }
    return -1;
}

static int st_cb_master(const uint8_t *d, size_t sz, size_t rec, void *arg) {
    st_master_arg *m = (st_master_arg *)arg;
    const uint8_t *tp;
    size_t tl;
    uint64_t num = 0;
    if (st_rec_col(d, sz, rec, 1, &tp, &tl, NULL) != 2 || tl != 15 || memcmp(tp, "tbl_contentinfo", 15) != 0) return 0;
    if (st_rec_col(d, sz, rec, 3, NULL, NULL, &num) != 3 || !num) return 0;
    m->root = (uint32_t)num;
    {
        const uint8_t *sp;
        size_t sl;
        if (st_rec_col(d, sz, rec, 4, &sp, &sl, NULL) == 2 && sl > 20)
            m->icon_col = st_sql_icon_col(sp, sl);
    }
    return 1;
}

typedef struct { const char *tid; char *name; size_t nsz; int icon_col; char *icon; size_t isz; } st_appdb_arg;

static int st_cb_app(const uint8_t *d, size_t sz, size_t rec, void *arg) {
    st_appdb_arg *a = (st_appdb_arg *)arg;
    const uint8_t *tp;
    size_t tl, idl = strlen(a->tid);
    if (st_rec_col(d, sz, rec, 0, &tp, &tl, NULL) != 2 || tl != idl || memcmp(tp, a->tid, idl) != 0) return 0;
    if (a->icon_col >= 0 && a->icon && a->isz) { /* 先抓 iconPath，名字列异常时图标仍可用 */
        if (st_rec_col(d, sz, rec, a->icon_col, &tp, &tl, NULL) == 2 && tl && tl < a->isz) {
            memcpy(a->icon, tp, tl);
            a->icon[tl] = 0;
        }
    }
    if (st_rec_col(d, sz, rec, 2, &tp, &tl, NULL) != 2 || !tl || tl > 96) return 0;
    memcpy(a->name, tp, tl);
    a->name[tl] = 0;
    return 1;
}

static int st_appdb_parse(const uint8_t *d, size_t sz, const char *tid, char *out, size_t osz, char *icon, size_t isz) {
    out[0] = 0;
    if (icon && isz) icon[0] = 0;
    if (sz < 200 || memcmp(d, "SQLite format 3", 16) != 0) return -1;
    uint32_t pgsz = st_be16(d + 16);
    if (pgsz == 1) pgsz = 65536;
    if (pgsz < 512 || pgsz > 65536) return -2;
    st_master_arg m = { 0, -1 };
    st_bwalk(d, sz, 1, pgsz, 0, st_cb_master, &m);
    st_appdb_arg a = { tid, out, osz, m.icon_col, icon, isz };
    if (m.root && st_bwalk(d, sz, m.root, pgsz, 0, st_cb_app, &a)) return 0;
    return -3;
}

/* app.db 查询带缓存：同 tid 命中直接复用；未命中 60s 内不重读。额外返回 iconPath 列原值 */
static int appdb_lookup(const char *tid, char *out, size_t osz, char *icon, size_t isz, char *err, size_t esz) {
    static char c_tid[12] = "", c_name[128] = "", c_icon[256] = "";
    static long c_ms = 0;
    static int c_ok = 0;
    out[0] = 0;
    if (icon && isz) icon[0] = 0;
    if (!strcmp(c_tid, tid) && (c_ok || (c_ms && now_ms() - c_ms < 60000))) {
        if (c_ok) {
            snprintf(out, osz, "%s", c_name);
            if (icon && isz) snprintf(icon, isz, "%s", c_icon);
        }
        else snprintf(err, esz, "cached miss");
        return c_ok ? 0 : -1;
    }
    snprintf(c_tid, sizeof(c_tid), "%s", tid);
    c_ms = now_ms();
    c_ok = 0;
    c_name[0] = 0;
    c_icon[0] = 0;
    const char *path = "/system_data/priv/mms/app.db";
    int fd = open(path, O_RDONLY);
    if (fd < 0) { snprintf(err, esz, "open: %s", strerror(errno)); return -1; }
    off_t fsz = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    if (fsz < 100 || fsz > (64 << 20)) { close(fd); snprintf(err, esz, "bad size"); return -1; }
    uint8_t *d = (uint8_t *)malloc((size_t)fsz);
    if (!d) { close(fd); snprintf(err, esz, "OOM"); return -1; }
    ssize_t rd = read(fd, d, (size_t)fsz);
    close(fd);
    int r = rd > 0 ? st_appdb_parse(d, (size_t)rd, tid, out, osz, icon, isz) : -1;
    free(d);
    if (r == 0 && out[0]) {
        c_ok = 1;
        snprintf(c_name, sizeof(c_name), "%s", out);
        if (icon && isz) snprintf(c_icon, sizeof(c_icon), "%s", icon);
        err[0] = 0;
        return 0;
    }
    out[0] = 0;
    snprintf(err, esz, "no hit rc=%d", r);
    return -1;
}

/* ---- 当前运行的游戏（5s 缓存）---- */
static char s_tid[12] = "";
static char s_title[128] = "";
static char s_sb_app0[160] = ""; /* 当前前台游戏的沙盒 app0 挂载点（供图标读取） */
static char s_icon_db[256] = ""; /* app.db iconPath 列原值（绝对路径或相对 /app0） */
static long s_title_probe_ms = 0;

static void probe_title(int force) {
    if (!force && s_title_probe_ms && now_ms() - s_title_probe_ms < 5000) return;
    s_title_probe_ms = now_ms();

    /* 枚举进程找前台大应用 */
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0};
    size_t buf_size = 0;
    pid_t best_pid = -1, mypid = getpid();
    uint32_t best_appid = 0;
    int best_score = -1;
    char best_tid[12] = "";
    if (sysctl(mib, 4, NULL, &buf_size, NULL, 0) == 0 && buf_size) {
        uint8_t *buf = (uint8_t *)malloc(buf_size);
        if (buf && sysctl(mib, 4, buf, &buf_size, NULL, 0) == 0) {
            for (uint8_t *p = buf; p < buf + buf_size;) {
                struct kinfo_proc *ki = (struct kinfo_proc *)p;
                if (ki->ki_structsize == 0) break;
                p += ki->ki_structsize;
                pid_t pid = ki->ki_pid;
                if (pid == mypid) continue;
                stats_app_info_t ai;
                memset(&ai, 0, sizeof ai);
                if (sceKernelGetAppInfo(pid, &ai) != 0) continue;
                char tid9[10];
                memcpy(tid9, ai.title_id, 9);
                tid9[9] = 0;
                int ok = tid9[0] != 0;
                for (int i = 0; i < 9; i++) if (!isalnum((unsigned char)tid9[i])) { ok = 0; break; }
                if (!ok) continue;
                int score = 0;
                if (!memcmp(tid9, "PPSA", 4)) score = 2;
                else if (!memcmp(tid9, "CUSA", 4) || !memcmp(tid9, "PPJB", 4) ||
                         !memcmp(tid9, "EMUA", 4) || !memcmp(tid9, "PAUS", 4)) score = 1;
                if (score == 1 && ai.app_type == 0) score = 0;
                if (score > best_score || (score == best_score && ai.app_id > best_appid)) {
                    best_score = score;
                    best_appid = ai.app_id;
                    best_pid = pid;
                    snprintf(best_tid, sizeof(best_tid), "%s", tid9);
                }
            }
        }
        free(buf);
    }

    /* 沙盒挂载识别：/mnt/sandbox/<TID>_xxx/app0 */
    char sb_path[160] = "", sb_tid[12] = "";
    int sb_score = -1;
    {
        struct statfs *mnts = NULL;
        int mn = getmntinfo(&mnts, MNT_NOWAIT);
        for (int i = 0; mn > 0 && i < mn; i++) {
            const char *to = mnts[i].f_mntonname, *fr = mnts[i].f_mntfromname;
            if (strncmp(to, "/mnt/sandbox/", 13)) continue;
            const char *sl = strchr(to + 13, '/');
            if (!sl || strncmp(sl + 1, "app0", 4) || (sl[5] && sl[5] != '/')) continue;
            if (sl - (to + 13) < 10 || to[13 + 9] != '_') continue;
            char id9[10];
            memcpy(id9, to + 13, 9);
            id9[9] = 0;
            int ok = 1;
            for (int c = 0; c < 9; c++) if (!isalnum((unsigned char)id9[c])) { ok = 0; break; }
            if (!ok) continue;
            int sc = 0;
            if (!memcmp(id9, "PPSA", 4)) sc = 3;
            else if (!memcmp(id9, "CUSA", 4) || !memcmp(id9, "PPJB", 4) ||
                     !memcmp(id9, "EMUA", 4) || !memcmp(id9, "PAUS", 4)) sc = 2;
            if (sc && !strncmp(fr, "/system", 7)) sc--;
            if (sc > 0 && sc >= sb_score) {
                sb_score = sc;
                snprintf(sb_path, sizeof(sb_path), "%s", to);
                snprintf(sb_tid, sizeof(sb_tid), "%s", id9);
            }
        }
    }

    s_tid[0] = 0;
    s_title[0] = 0;
    s_icon_db[0] = 0;
    snprintf(s_sb_app0, sizeof(s_sb_app0), "%s", sb_path); /* 可能为空（无沙盒） */
    if (best_pid >= 0 && best_score > 0) snprintf(s_tid, sizeof(s_tid), "%s", best_tid);
    else if (sb_score > 0) snprintf(s_tid, sizeof(s_tid), "%s", sb_tid);

    char name[128] = "";
    if (s_tid[0]) {
        char derr[80] = "";
        if (appdb_lookup(s_tid, name, sizeof(name), s_icon_db, sizeof(s_icon_db), derr, sizeof(derr)) != 0) {
            name[0] = 0;
            if (sb_path[0]) {
                char c0[200];
                snprintf(c0, sizeof(c0), "%s/sce_sys/param.sfo", sb_path);
                if (sfo_file_name(c0, name, sizeof(name)) != 0) name[0] = 0;
            }
        }
    }
    /* 沙盒与当前 tid 对不上说明是挂起的历史游戏，名字作废只报 tid */
    if (name[0] && s_tid[0] && sb_tid[0] && strcmp(s_tid, sb_tid) != 0) name[0] = 0;
    if (name[0]) snprintf(s_title, sizeof(s_title), "%s", name);
    else if (s_tid[0]) snprintf(s_title, sizeof(s_title), "%s", s_tid);
}

/* ── 当前游戏图标（psdevwiki：sce_sys/icon0.png，512x512 PNG，无 gif 变体）──
 * 候选顺序：1) /user/app/<TID>/sce_sys/icon0.png（系统/补丁图标位，app_installer 同路径）
 *           2) <沙盒 app0>/sce_sys/icon0.png（光盘原版，沙盒 tid 与当前一致才用）
 *           3) app.db iconPath 列（系统真正显示的图标路径；可能带 ?ts= 缓存参数需剥除，/app0/ 开头映射沙盒）
 *           4) /user/appmeta/<TID>/icon0.png（实机确认：PS4 兼容模式游戏图标缓存位）
 *           5) /system_data/priv/appmeta/<TID>/icon0.png（旧主机时代 appmeta 路径）
 * tid 为服务端探测结果并再验 alnum（防路径穿越）；全部落空时每个 tid 记一行日志便于定位。*/
static int icon_file_ok(const char *p) {
    struct stat st;
    return p && p[0] && !strstr(p, "..") && stat(p, &st) == 0 && S_ISREG(st.st_mode)
        && st.st_size > 0 && st.st_size <= 4 * 1024 * 1024;
}

int pldmgr_get_game_icon_path(char *out, size_t out_size) {
    char cand[256];
    if (!out || out_size < 256 || !s_tid[0]) return 0;
    for (int i = 0; s_tid[i]; i++)
        if (!isalnum((unsigned char)s_tid[i])) return 0;

    snprintf(cand, sizeof(cand), "/user/app/%s/sce_sys/icon0.png", s_tid);
    if (icon_file_ok(cand)) { snprintf(out, out_size, "%s", cand); return 1; }

    if (s_sb_app0[0] && !strncmp(s_sb_app0, "/mnt/sandbox/", 13) &&
        !strncmp(s_sb_app0 + 13, s_tid, 9)) {
        snprintf(cand, sizeof(cand), "%s/sce_sys/icon0.png", s_sb_app0);
        if (icon_file_ok(cand)) { snprintf(out, out_size, "%s", cand); return 1; }
    }

    if (s_icon_db[0]) {
        char dbpath[256];
        snprintf(dbpath, sizeof(dbpath), "%s", s_icon_db);
        char *q = strchr(dbpath, '?'); /* iconPath 可能带 ?ts=时间戳 缓存参数，先剥掉再当文件路径 */
        if (q) *q = 0;
        if (icon_file_ok(dbpath)) { snprintf(out, out_size, "%s", dbpath); return 1; }
        if (!strncmp(dbpath, "/app0/", 6) && s_sb_app0[0]) {
            snprintf(cand, sizeof(cand), "%s%s", s_sb_app0, dbpath + 5);
            if (icon_file_ok(cand)) { snprintf(out, out_size, "%s", cand); return 1; }
        }
    }

    /* 实机确认：PS4 兼容模式游戏的图标缓存在 /user/appmeta/<TID>/ */
    snprintf(cand, sizeof(cand), "/user/appmeta/%s/icon0.png", s_tid);
    if (icon_file_ok(cand)) { snprintf(out, out_size, "%s", cand); return 1; }

    snprintf(cand, sizeof(cand), "/system_data/priv/appmeta/%s/icon0.png", s_tid);
    if (icon_file_ok(cand)) { snprintf(out, out_size, "%s", cand); return 1; }

    {
        static char logged_tid[12] = "";
        if (strcmp(logged_tid, s_tid)) {
            snprintf(logged_tid, sizeof(logged_tid), "%s", s_tid);
            pldmgr_log("[PLDMGR] game_icon miss tid=%s sb=%s dbIcon=%s\n",
                       s_tid, s_sb_app0[0] ? s_sb_app0 : "-", s_icon_db[0] ? s_icon_db : "-");
        }
    }
    return 0;
}

size_t pldmgr_get_system_stats_json(char *buf, size_t size) {
    /* CPU */
    float cpu_usage = get_cpu_usage();
    int cpu_temp = -1;
    if (sceKernelGetCpuTemperature(&cpu_temp) != 0) cpu_temp = -1;

    /* GPU / SOC（APU 温度，sensor 0） */
    int gpu_temp = -1;
    if (sceKernelGetSocSensorTemperature(0, &gpu_temp) != 0) gpu_temp = -1;

    /* SSD / 硬盘温度：SoC sensor 1（参考 ps5mon 的 hdd_c；读不到置 -1）*/
    int ssd_temp = -1;
    if (sceKernelGetSocSensorTemperature(1, &ssd_temp) != 0) ssd_temp = -1;

    /* 风扇占空比（0..1024 → 百分比） */
    int fan_duty = 0;
    if (sceKernelGetCurrentFanDuty != NULL) {
        uint16_t duty = 0;
        uint64_t chassis = 0;
        if (sceKernelGetCurrentFanDuty(&duty, &chassis) == 0) {
            fan_duty = (int)(((double)duty * 100.0) / (double)FAN_DUTY_MAX);
            if (fan_duty < 0) fan_duty = 0;
            if (fan_duty > 100) fan_duty = 100;
        }
    }

    /* 运行内存 / 显存（get_page_table_stats 直接返回 MB） */
    int ram_total_mb = 0, ram_free_mb = 0;
    get_page_table_stats(VM_SYSTEM, PAGE_TABLE_RAM, &ram_total_mb, &ram_free_mb);
    int ram_used_mb = (ram_total_mb > 0 && ram_free_mb <= ram_total_mb)
                          ? (ram_total_mb - ram_free_mb) : 0;

    int vram_total_mb = 0, vram_free_mb = 0;
    get_page_table_stats(VM_SYSTEM, PAGE_TABLE_VRAM, &vram_total_mb, &vram_free_mb);
    int vram_used_mb = (vram_total_mb > 0 && vram_free_mb <= vram_total_mb)
                           ? (vram_total_mb - vram_free_mb) : 0;

    /* 存储（参考 ps5mon 的两块内置盘）：
     *   内置 SSD = /user（games+apps 分区）→ 前端“内置 NVMe SSD”卡（label: Internal storage）
     *   内置 M.2 扩展盘 = 动态探测：与 /user 同 fstype、非 / 非 /user、容量最大者
     *     → 前端“M.2 扩展存储”卡（label: NVME SSD）；探测失败 60s 重试。
     * 单位统一为 MB。*/
    char storage_json[1024] = "[";
    size_t storage_pos = 1;
    int entry_count = 0;

    /* 内置 SSD：/user */
    {
        struct statfs st;
        if (statfs("/user", &st) == 0 && st.f_blocks > 0) {
            unsigned long long total = (unsigned long long)st.f_blocks * st.f_bsize;
            unsigned long long avail = (unsigned long long)st.f_bavail * st.f_bsize;
            storage_pos += snprintf(storage_json + storage_pos, sizeof(storage_json) - storage_pos,
                "%s{\"label\":\"Internal storage\",\"total\":%llu,\"used\":%llu,\"path\":\"/user\"}",
                entry_count ? "," : "", total / 1048576ULL, (total - avail) / 1048576ULL);
            entry_count++;
        }
    }

    /* 内置 M.2 扩展盘：探测 + 60s 缓存（移植 ps5mon probe_m2）*/
    {
        static char m2_path[64] = "";
        static long m2_probe_ms = 0;
        if (!m2_path[0] && (m2_probe_ms == 0 || now_ms() - m2_probe_ms > 60000)) {
            m2_probe_ms = now_ms();
            struct statfs usr;
            memset(&usr, 0, sizeof usr);
            statfs("/user", &usr);
            struct statfs *ms = NULL;
            int n = getmntinfo(&ms, MNT_NOWAIT);
            unsigned long long best = 0;
            for (int i = 0; n > 0 && i < n; i++) {
                const char *to = ms[i].f_mntonname;
                unsigned long long tot = (unsigned long long)ms[i].f_blocks * ms[i].f_bsize;
                if (tot <= 0 || to[0] != '/') continue;
                if (strcmp(to, "/") == 0 || strcmp(to, "/user") == 0) continue;
                if (usr.f_fstypename[0] && strcmp(ms[i].f_fstypename, usr.f_fstypename) != 0) continue;
                if (tot > best) { best = tot; snprintf(m2_path, sizeof m2_path, "%s", to); }
            }
        }
        if (m2_path[0]) {
            struct statfs st;
            if (statfs(m2_path, &st) == 0 && st.f_blocks > 0) {
                unsigned long long total = (unsigned long long)st.f_blocks * st.f_bsize;
                unsigned long long avail = (unsigned long long)st.f_bavail * st.f_bsize;
                storage_pos += snprintf(storage_json + storage_pos, sizeof(storage_json) - storage_pos,
                    "%s{\"label\":\"NVME SSD\",\"total\":%llu,\"used\":%llu,\"path\":\"%s\"}",
                    entry_count ? "," : "", total / 1048576ULL, (total - avail) / 1048576ULL, m2_path);
                entry_count++;
            } else {
                m2_path[0] = 0; /* 挂载消失，下轮重探 */
            }
        }
    }

    strcat(storage_json, "]");

    /* 开机时长 + 型号 + 系统版本 + 序列号（后三者首次探测后缓存） */
    char uptime_str[32];
    get_uptime_string(uptime_str, sizeof(uptime_str));
    char model_raw[64];
    get_model_string(model_raw, sizeof(model_raw));
    char model_escaped[160];
    pldmgr_json_escape(model_raw, model_escaped, sizeof(model_escaped));
    char fw_str[16], serial_raw[64], serial_escaped[160];
    get_firmware_string(fw_str, sizeof(fw_str));
    get_serial_string(serial_raw, sizeof(serial_raw));
    pldmgr_json_escape(serial_raw, serial_escaped, sizeof(serial_escaped));

    /* FPS + 当前游戏（移植 ps5mon：受 READ_HARDWARE_INFO 开关间接门控，取不到即 -1 / 空串）*/
    sample_fps();
    probe_title(0);
    char game_name_e[512], game_tid_e[32];
    stats_escape_utf8(s_title, game_name_e, sizeof(game_name_e));
    stats_escape_utf8(s_tid, game_tid_e, sizeof(game_tid_e));

    return snprintf(buf, size,
        "{\"cpu\":{\"usage\":%.1f,\"temp\":%d},"
        "\"gpu\":{\"temp\":%d},"
        "\"ssd\":{\"temp\":%d},"
        "\"fan\":{\"duty\":%d,\"max\":100},"
        "\"ram\":{\"total\":%d,\"used\":%d},"
        "\"vram\":{\"total\":%d,\"used\":%d},"
        "\"fps\":%.1f,"
        "\"game\":{\"name\":\"%s\",\"tid\":\"%s\"},"
        "\"uptime\":\"%s\",\"model\":\"%s\",\"firmware\":\"%s\",\"serial\":\"%s\","
        "\"storage\":%s}",
        cpu_usage, cpu_temp,
        gpu_temp,
        ssd_temp,
        fan_duty,
        ram_total_mb, ram_used_mb,
        vram_total_mb, vram_used_mb,
        s_fps,
        game_name_e, game_tid_e,
        uptime_str, model_escaped, fw_str, serial_escaped, storage_json);
}
