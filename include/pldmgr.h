#pragma once

/* Network Settings */
#define MENU_PORT 8911 /* [CN-FORK] Web 服务端口，上游默认 8084；main.c/autoload.c 均引用此宏。同步改动：assets/param.json 的 deeplinkUri、deploy.sh、前端 QR/显示(StorageHub/ManageSourcesView)、DEVELOPMENT.md、FORK-CN.md */
#define ELFLDR_PORT 9021

/* Routes */
#define ROUTE_INDEX "/"
#define ROUTE_INDEX_HTML "/index.html"
#define ROUTE_LIST_PAYLOADS "/list_payloads"
#define ROUTE_UPLOAD "/manage:upload"
#define ROUTE_CHECK "/manage:check"
#define ROUTE_DELETE "/manage:delete"
#define ROUTE_LOAD_PAYLOAD "/loadpayload:"
#define ROUTE_SHUTDOWN "/shutdown"
#define ROUTE_LOG "/log"
#define ROUTE_VERSION "/version"
#define ROUTE_GETIP "/getip"
#define ROUTE_GET_CONFIG "/get_config"
#define ROUTE_SET_CONFIG "/set_config"
#define ROUTE_ABORT "/abort"
#define ROUTE_AUTOLOAD_STATUS "/autoload_status"
#define ROUTE_AUTOLOAD_CLEAR "/autoload_clear"
#define ROUTE_REPO_LIST "/repository_payloads"
#define ROUTE_REPO_REFRESH "/repository_refresh"
#define ROUTE_REPO_INSTALL "/repository_install"
#define ROUTE_REPO_PUSH "/repository_push"
#define ROUTE_REPO_INSTALL_PUSH "/repository_install_push"
#define ROUTE_SOURCES_LIST "/sources_list"
#define ROUTE_SOURCES_SET "/sources_set"
#define ROUTE_SOURCES_ADD "/sources_add"
#define ROUTE_SOURCES_REMOVE "/sources_remove"
#define ROUTE_USB_MOVE_CHECK "/usb_move_check"
#define ROUTE_USB_MOVE_PERFORM "/usb_move_perform"
#define ROUTE_CACHE_MANIFEST "/cache.appcache"

#define ROUTE_PROCESSES_LIST "/processes_list"
#define ROUTE_PROCESS_KILL "/process_kill"
#define ROUTE_HISTORY_LIST "/history_list"
/* [CN-FORK] 硬件信息读取端点（温度/风扇/内存/显存/存储），受 READ_HARDWARE_INFO 开关控制 */
#define ROUTE_SYSTEM_STATS "/system_stats"
/* [CN-FORK] 当前游戏图标端点（与 /system_stats 同一开关门控，只读服务端探测到的 tid）*/
#define ROUTE_GAME_ICON "/game_icon"
/* [CN-FORK] PKG Manager 内置集成：状态探测 / 启动 / 关闭 / 手动上传更新 */
#define ROUTE_PKG_STATUS "/pkgmanager_status"
#define ROUTE_PKG_LAUNCH "/pkgmanager_launch"
#define ROUTE_PKG_STOP "/pkgmanager_stop"
#define ROUTE_PKG_UPLOAD "/pkgmanager_upload"
/* [CN-FORK] 「常用入口」自定义快捷访问清单：列表（含探活）/ 新增 / 删除 */
#define ROUTE_QUICKLINKS "/quicklinks"
#define ROUTE_QUICKLINKS_ADD "/quicklinks_add"
#define ROUTE_QUICKLINKS_REMOVE "/quicklinks_remove"
/* [CN-FORK] 系统信息诊断端点：fw/型号/序列号探测结果 + kenv 全量 dump（纯文本） */
#define ROUTE_SYSINFO_PROBE "/sysinfo_probe"

#define MENU_VERSION "0.5.1.57" /* [CN-FORK] 本 fork 版本：上游基线 0.5.1 + 构建序号（.57）；每次调整构建自动 +1 */
#define AUTOLOAD_CONFIG_PATH "/data/nayeplmtools/autoload.txt"
#define PLDMGR_CONFIG_PATH "/data/nayeplmtools/nayeplmtools_config.txt"
#define REPOSITORY_CACHE_PATH "/data/nayeplmtools/repository_cache.json"
#define PAYLOADS_STORAGE_DIR "/data/nayeplmtools/payloads"
/* [CN-FORK] 越狱引导器的自动加载目录，安装 payload 后若同名工具在此则同步更新 */
#define PS5_AUTOLOADER_DIR    "/data/ps5_autoloader"
#define PS5_AUTOLOADER_CONFIG PS5_AUTOLOADER_DIR "/autoload.txt"
#define REPOSITORY_SOURCE_URL                                                  \
  "https://itsplk.github.io/ps5-payloads-mirror/payloads.json"
#define REPOSITORY_REFRESH_INTERVAL_SEC 86400

/* Logging (implementation in log_server.c) */
void pldmgr_log(const char *fmt, ...);
int pldmgr_server_is_active();

#include "autoload.h"
#include "notification.h"
#include "utils.h"

/* Paths */
#define BASE_DATA_DIR "/data/nayeplmtools"
#define SOURCES_CONFIG_PATH "/data/nayeplmtools/sources.json"
#define MAX_SOURCES 50

/* Scan Locations (Internal + 8 USB ports) */
static const char *SCAN_DIRS[] = {
    "/data/nayeplmtools",     "/mnt/usb0/nayeplmtools", "/mnt/usb1/nayeplmtools",
    "/mnt/usb2/nayeplmtools", "/mnt/usb3/nayeplmtools", "/mnt/usb4/nayeplmtools",
    "/mnt/usb5/nayeplmtools", "/mnt/usb6/nayeplmtools", "/mnt/usb7/nayeplmtools"};
#define SCAN_DIRS_COUNT 9

/* Messages */
#define MSG_OK "OK"
