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

#define MENU_VERSION "0.5.1.4" /* [CN-FORK] 本 fork 版本：上游基线 0.5.1 + 构建序号（.4）；每次调整构建自动 +1 */
#define AUTOLOAD_CONFIG_PATH "/data/nayeplmtools/autoload.txt"
#define PLDMGR_CONFIG_PATH "/data/nayeplmtools/nayeplmtools_config.txt"
#define REPOSITORY_CACHE_PATH "/data/nayeplmtools/repository_cache.json"
#define PAYLOADS_STORAGE_DIR "/data/nayeplmtools/payloads"
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
