/*
 * Payload Manager Core - Main Entry Point
 *
 * This is a native PS5 ELF daemon that hosts a web server
 * to manage payloads and system settings.
 *
 * All HTTP routing lives in http_server.c.
 * This file handles: process init, signal setup, MHD lifecycle, and watchdog.
 */

#include <microhttpd.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/sysctl.h>
#include <unistd.h>

#include "pldmgr.h"
#include "config.h"
#include "http_server.h"
#include "repository.h"
#include "sources.h"     /* [CN-FORK] v0.5.2.67 预置默认来源 sources_seed_defaults */
#include "payload_mgr.h"  /* [CN-FORK] bootstrap_self_sync */
#include "ps5_launcher.h"
#include "app_installer.h"
#include "utils.h" /* [CN-FORK] pldmgr_get_local_ip 提前取 IP */
#include "fan_control.h" /* [CN-FORK] 风扇温控常驻重发守护 */
#include "game_title.h"  /* [CN-FORK] v0.5.2.137 启动自愈：把 name.txt 重放到主机各层 */
#include "game_saves.h"  /* [CN-FORK] v0.5.2.140 本地快照守护（存档的本地兜底） */
#include "game_auto.h"   /* [CN-FORK] v0.5.2.142 自动存档守护（启动拉取 / 退出回传） */
/* [CN-FORK] v0.5.2.99 PS5 Account Tools 源码并入：提权入口 + 远程游玩会话收尾 */
#include "psaccounts/sys.h"
#include "psaccounts/accounts.h"

#define DEFAULT_PORT MENU_PORT

/* Defined in http_server.c — set to 0 by /shutdown route */
extern volatile int http_keep_running;

/* [CN-FORK] v0.5.2.99 MHD 生命周期收成一个函数：本文件原有 4 处 MHD_start_daemon
 * （首启、待机唤醒、换 IP、断网回环），账号接口的流式上传状态需要在请求结束时
 * 回收（头像传到一半关页面），补 NOTIFY_COMPLETED 必须 4 处都带上，漏一处就是
 * 只在某种重启路径下才泄漏。参数与原来完全一致，只是多一个回调。 */
/* [CN-FORK] v0.5.2.141 连接线程的栈显式给足 1MB。
 *
 * MHD_USE_THREAD_PER_CONNECTION 是「一条连接一条 pthread 线程」，栈走的是
 * 真机 pthread 的**默认值**，而那个默认值只有 64KB 上下（宿主上量出来的临界点
 * 是 56KB：整条 http_on_request → … 的链一旦超过 56KB 就 SIGSEGV）。后果是
 * 「某个接口的局部数组一大，整个面板就死」—— 到 `.141` 为止已经栽过两次：
 *   `.116` 改磁贴图标/背景（tt_apply 里 64KB 栈数组）
 *   `.141` 点「立即备份」（gs_copy_file 里 32KB 栈数组）
 * 两次都是**只在真机上炸、宿主全绿**，因为宿主跑在主线程的 8MB 栈上。
 *
 * 与其等着第三次，不如把这条线程的栈一次性给足。顺带把所有「大数组放栈上」
 * 的收益也一并消掉：真机日志里 `saves: 本地快照守护已启动` 之后会跟一行
 * 实际生效的栈大小，下次再遇到这类问题不必靠猜。
 *
 * 注意这个值只覆盖**连接线程**；自己 pthread_create 出来的守护线程（快照、
 * 风扇、名字自愈）不受它管，各自在创建处单独给栈。 */
#define HTTP_THREAD_STACK (1u << 20)

static struct MHD_Daemon *http_daemon_start(unsigned short port) {
    /* 带栈大小先试一把：万一这个 SDK 的 MHD 不认这个选项，start_daemon 会直接
     * 返回 NULL —— 那等于整个面板起不来。所以失败就**去掉选项重试一次**，
     * 宁可退回默认栈，也不能因为一个优化项把服务弄没。 */
    struct MHD_Daemon *d = MHD_start_daemon(
        MHD_USE_THREAD_PER_CONNECTION | MHD_USE_DEBUG, port, NULL, NULL,
        &http_on_request, NULL, MHD_OPTION_NOTIFY_COMPLETED,
        &http_on_request_completed, NULL,
        /* ⚠ 这个选项的参数类型是 size_t（MHD 那边 va_arg(ap, size_t)），
         * 传 unsigned int 在 x86-64 变参下会读到高 32 位的垃圾。 */
        MHD_OPTION_THREAD_STACK_SIZE, (size_t)HTTP_THREAD_STACK,
        MHD_OPTION_END);
    if (d) {
        pldmgr_log("[PLDMGR] http: 连接线程栈 %uKB\n", (unsigned)HTTP_THREAD_STACK >> 10);
        return d;
    }
    pldmgr_log("[PLDMGR] http: 不接受 THREAD_STACK_SIZE，退回默认栈重试\n");
    return MHD_start_daemon(
        MHD_USE_THREAD_PER_CONNECTION | MHD_USE_DEBUG, port, NULL, NULL,
        &http_on_request, NULL, MHD_OPTION_NOTIFY_COMPLETED,
        &http_on_request_completed, NULL, MHD_OPTION_END);
}

static volatile sig_atomic_t resume_flag = 0;

static void handle_sigcont(int sig) { resume_flag = 1; }

static pid_t find_pid(const char *name) {
    int mib[4] = {1, 14, 8, 0};
    pid_t mypid = getpid();
    pid_t pid = -1;
    size_t buf_size;
    uint8_t *buf;

    if (sysctl(mib, 4, 0, &buf_size, 0, 0)) {
        pldmgr_log("[PLDMGR] sysctl failed\n");
        return -1;
    }

    if (!(buf = malloc(buf_size))) {
        pldmgr_log("[PLDMGR] malloc failed\n");
        return -1;
    }

    if (sysctl(mib, 4, buf, &buf_size, 0, 0)) {
        pldmgr_log("[PLDMGR] sysctl failed\n");
        free(buf);
        return -1;
    }

    for (uint8_t *ptr = buf; ptr < (buf + buf_size);) {
        int ki_structsize = *(int *)ptr;
        pid_t ki_pid = *(pid_t *)&ptr[72];
        char *ki_tdname = (char *)&ptr[447];

        ptr += ki_structsize;
        if (!strncmp(ki_tdname, name, strlen(name)) && ki_pid != mypid) { /* [CN-FORK] 前缀匹配，兼容线程名长度截断，确保旧进程必被 kill */
            pid = ki_pid;
        }
    }

    free(buf);
    return pid;
}

/* PS5 System Calls (Internal) */
extern int sceNetCtlInit();
extern int sceUserServiceInitialize(void *);

__attribute__((used)) volatile const char pldmgr_version_sig[] = "PLDMGR_VER:" MENU_VERSION;

int main(int argc, char *argv[]) {
    struct MHD_Daemon *daemon;
    unsigned short port = DEFAULT_PORT;
    pid_t pid;

    syscall(SYS_thr_set_name, -1, "NayePlmTools.elf"); /* [CN-FORK] 进程/线程名，与 find_pid / 前端 critical 判断一致；上游为 "pldmgr.elf" */

    while ((pid = find_pid("NayePlmTools")) > 0) { /* [CN-FORK] 用不含 .elf 的安全前缀匹配旧进程 */
        if (kill(pid, SIGKILL)) {
            pldmgr_log("[PLDMGR] kill failed\n");
            return EXIT_FAILURE;
        }
        sleep(1);
    }

    pldmgr_log("[PLDMGR] Starting NayePlmTools v%s on port %d...\n",
               pldmgr_version_sig + 11, port);

    /* Check for Self-Update */
    char new_payload_path[512];
    if (repository_check_self_update(new_payload_path, sizeof(new_payload_path)) == 0) {
        pldmgr_log("[PLDMGR] Found updated payload manager at %s. Launching...\n",
                    new_payload_path);
        ps5_launch_elf(new_payload_path);
        return 0;
    }

    /* Initialize PS5 System Services */
    pldmgr_log("[PLDMGR] Initializing system services...\n");
    if (sceNetCtlInit() == 0) {
        pldmgr_log("[PLDMGR] Network Controller initialized.\n");
    }

    int user_prio = 256;
    /* [CN-FORK] v0.5.2.99 PS5 Account Tools 源码并入：原项目靠 ELF 加载器的
     * .init 构造函数自动提权（写 SysReg 槽位与 /system_data/priv 的 auth.dat
     * 必须要 root + 换 rootdir/jaildir）。并入主 ELF 后没有构造函数这回事，
     * 所以在 UserService 初始化之前显式调用一次；失败只记日志，不拦启动，
     * 后续每个 /api 请求会各自返回真实的权限错误。 */
    psaccounts_escalate();
    if (sceUserServiceInitialize(&user_prio) == 0) {
        pldmgr_log("[PLDMGR] User Service initialized.\n");
    }

    /* Read startup config */
    PldmgrConfig cfg;
    config_read(&cfg);

    /* [CN-FORK] 死机排查：v0.5.1.59 自愈后本工具第一次真的开机自启，启动序列里几个
     * 默认开启的高危动作（杀光盘播放器/开浏览器/装机）从此都在引导阶段执行。
     * 把开关值连同落盘日志一起记下来，下次死机后才能从 pldmgr.log 直接指认现场。
     * v0.5.1.64：硬件读取开关随主页一起删除，日志不再输出 hw_stats。
     * v0.5.1.65：新增 default_repo（默认云端仓库总开关，关掉后开机不再拉 payloads.json）。 */
    pldmgr_log("[PLDMGR] config: auto_browser=%d kill_disc=%d auto_install=%d autoload=%d "
               "default_repo=%d\n",
               cfg.auto_browser_open ? 1 : 0,
               cfg.kill_disc_player ? 1 : 0, cfg.auto_install_app ? 1 : 0,
               cfg.autoload_enabled ? 1 : 0, cfg.default_repo_enabled ? 1 : 0);

    /* [CN-FORK] v0.5.1.63 安全模式：/data/nayeplmtools/safe_mode 存在（内容无所谓）就
     * 跳过引导期全部高危动作 —— 装启动器 app、杀光盘播放器、自动开浏览器、自动加载
     * 序列全不做。死机后用 FTP 建这个空文件即可止血。 */
    if (pldmgr_utils_safe_mode()) {
        cfg.auto_install_app  = 0;
        cfg.kill_disc_player  = 0;
        cfg.auto_browser_open = 0;
        cfg.autoload_enabled  = 0;
        pldmgr_log("[PLDMGR] !!! SAFE MODE on (%s): skip install-app / kill-disc / "
                   "auto-browser / autoload\n", PLDMGR_SAFE_MODE_FLAG);
    }

    /* [CN-FORK] v0.5.2.67 首次开机预置默认来源：内置官方源默认关，改成启用 repo.630810.xyz。
     * 仅当 /data/nayeplmtools/sources.json 不存在时写一次，safe_mode 下也无妨（只是个文件创建）。 */
    sources_seed_defaults();

    /* [CN-FORK] IP 提前获取：供启动日志与最后一条通知复用（通知为排他显示，顺序见下方） */
    char current_ip[64] = "unknown";
    pldmgr_get_local_ip(current_ip, sizeof(current_ip));

    /* Install app if requested */
    if (cfg.auto_install_app) {
        pldmgr_install_app_if_needed();
    }

    /* Kill Disc Player if running (BD-JB host) and enabled in config */
    if (cfg.kill_disc_player) {
        ps5_kill_disc_player();
    }

    /* Signal Resilience */
    signal(SIGPIPE, SIG_IGN);
    signal(SIGHUP, SIG_IGN);
    signal(SIGTERM, SIG_IGN);
    signal(SIGCONT, handle_sigcont);

    /* Start the MHD daemon */
    daemon = http_daemon_start(port);

    if (NULL == daemon) {
        pldmgr_log("[PLDMGR] Failed to start HTTP daemon!\n");
        pldmgr_notify("Error: HTTP Server Failed\nPort 8911 may be busy");
        return 1;
    }

    pldmgr_log("[PLDMGR] Server is running. Visit /shutdown to exit.\n");
    pldmgr_log("[PLDMGR] Web UI available at http://%s:%d\n", current_ip, port); /* [CN-FORK] 启动 debug 显示 ip 和端口 */

    /* Try cache refresh
     * [CN-FORK] v0.5.1.65 默认云端仓库关掉时 repository_ensure_fresh() 内部直接返回，不会发请求 */
    repository_ensure_fresh(0);

    /* [CN-FORK] 启动自检：将已安装的 NayePlmTools 同步到 ps5_autoloader，
     * 解决旧版本没有 sync 代码导致首次升级到 .20 时 ps5_autoloader 未更新的问题 */
    payload_mgr_bootstrap_self_sync();

    /* Start Autoload Sequence (if config exists) */
    pldmgr_autoload_start();

    /* [CN-FORK] 风扇温控守护：从 config 恢复启用/生效态并常驻重发（只碰 /dev/icc_fan，
     * safe_mode 下自动跳过写入）；主进程常驻，待机唤醒不会重复起线程。 */
    fan_control_start_daemon();

    /* [CN-FORK] v0.5.2.137 名字回填自愈：改名只在改名那一刻写主机，历史遗留（改名发生在
     * .132 写源之前）与「SMP 重启重新注册把 app.db 冲回原名」都只能靠事后补。
     * 这里起一条 detached 线程，延迟一会儿（等启动期 IO 过去）跑一次幂等重放 ——
     * 已经一致的款一次写都不发，所以可以每次开机无条件跑。safe_mode 下整条跳过。 */
    game_title_sync_start_daemon();

    /* [CN-FORK] v0.5.2.154 本地快照守护**已删除**：快照改成「游戏退出时触发一次」，
     * 由下面的自动存档引擎线程在做（见 game_auto.c 的 game_auto_tick 与
     * include/game_auto.h 的 GA_ACT_SNAP）。用户口径：「每次游戏结束后，在本地快照
     * 存一份存档，本地自动存储快照删除（定时的功能不要了）」。 */

    /* [CN-FORK] v0.5.2.142 自动存档执行引擎：`on_launch` / `on_exit` 这两个开关
     * 在这之前**只被读写、没有任何人在跑**。这里起一条线程探测「前台大应用是谁」
     * （sceLncUtil），游戏启动时把远程那份拉下来、退出时把主机这份推上去
     * （`.154` 起还负责在退出那一刻做一次本地快照）。safe_mode 下整条跳过。 */
    game_auto_start_daemon();

    if (cfg.auto_browser_open) {
        char browser_url[128];
        snprintf(browser_url, sizeof(browser_url), "http://127.0.0.1:%d", port);
        ps5_launch_browser(browser_url);
    }

    /* [CN-FORK] 启动通知无条件压轴发送：PS5 通知为排他显示（最后一条生效），此前仅在关闭“自动开浏览器”时才发，
       且会被装机/自动加载序列顶掉，导致左上角 IP:端口通知时有时无 */
    if (strcmp(current_ip, "unknown") != 0) {
        sleep(1); /* 等自动加载线程的倒计时通知先发出，本条作为最后一条胜出 */
        pldmgr_notify("NayePlmTools v%s Ready!\nIP: %s\nPort: %d", MENU_VERSION, current_ip, port);
    }

    /* Watchdog and main loop */
    int network_check_timer = 0;
    while (http_keep_running) {
        usleep(100000); /* 100ms sleep */

        /* Immediate Wake-up Recovery */
        if (resume_flag) {
            resume_flag = 0;
            pldmgr_log("[PLDMGR] Console resumed from standby. Restarting "
                       "server...\n");
            pldmgr_autoload_reset();

            /* Force full server restart — the old socket is likely dead */
            if (daemon)
                MHD_stop_daemon(daemon);

            usleep(1000000); /* 1s for network stack to stabilize */

            daemon = http_daemon_start(port);

            if (daemon) {
                /* Re-read current IP */
                if (pldmgr_get_local_ip(current_ip, sizeof(current_ip)) != 0)
                    strcpy(current_ip, "unknown");
                pldmgr_log("[PLDMGR] Server restarted after standby. IP: %s\n",
                           current_ip);
            } else {
                pldmgr_log("[PLDMGR] !!! Failed to restart server after standby!\n");
                pldmgr_notify("NayePlmTools: Server restart failed after standby");
                strcpy(current_ip, "unknown");
            }

            /* Reset timer so we don't immediately re-check */
            network_check_timer = 0;
        }

        /* Network Watchdog (every 5 seconds) */
        if (++network_check_timer >= 50) {
            network_check_timer = 0;
            char new_ip[64] = "unknown";
            int has_ip = (pldmgr_get_local_ip(new_ip, sizeof(new_ip)) == 0);

            if (has_ip && (strcmp(new_ip, current_ip) != 0 ||
                           strcmp(current_ip, "unknown") == 0)) {
                pldmgr_log("[PLDMGR] Network state refresh: %s -> %s. Restarting "
                           "server...\n",
                           current_ip, new_ip);
                if (daemon)
                    MHD_stop_daemon(daemon);

                usleep(800000);

                daemon = http_daemon_start(port);

                if (daemon) {
                    strcpy(current_ip, new_ip);
                    pldmgr_log("[PLDMGR] Server restored on %s:%d\n", current_ip, port);
                    pldmgr_notify("NayePlmTools: Service Restored\nIP: %s", current_ip);
                } else {
                    pldmgr_log("[PLDMGR] !!! Failed to restore server!\n");
                }
            } else if (!has_ip && strcmp(current_ip, "unknown") != 0) {
                pldmgr_log("[PLDMGR] Network lost (was %s). Restarting server "
                           "for loopback...\n", current_ip);
                strcpy(current_ip, "unknown");

                /* Restart daemon to ensure clean socket for loopback */
                if (daemon)
                    MHD_stop_daemon(daemon);

                usleep(300000);

                daemon = http_daemon_start(port);

                if (daemon) {
                    pldmgr_log("[PLDMGR] Server restarted after network loss "
                               "(loopback only)\n");
                } else {
                    pldmgr_log("[PLDMGR] !!! Failed to restart server after "
                               "network loss!\n");
                    pldmgr_notify("NayePlmTools: Server restart failed");
                }
            }
        }
    }

    pldmgr_log("[PLDMGR] Shutting down...\n");
    if (daemon)
        MHD_stop_daemon(daemon);

    /* [CN-FORK] v0.5.2.99 收尾远程游玩会话：必须停在 MHD_stop_daemon 之后，
     * 否则还能收到 /api/remoteplay/pin。带着已初始化的 libSceRemoteplay 会话
     * 退出的话，服务被死进程占住，下次 Initialize 稳定吃 0x80fc0003，只能重启主机解锁。 */
    accounts_pin_shutdown();

    sleep(1);

    return 0;
}
