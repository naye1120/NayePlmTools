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
/* [CN-FORK] v0.5.1.64 删除 /system_stats 与 /game_icon：主页硬件信息（温度/风扇/内存/显存/
 * 存储/FPS/当前游戏）连同 stats.c 一并下线，不再对内核/显示驱动接口做轮询探测 */
/* [CN-FORK] PKG Manager 内置集成：状态探测 / 启动 / 关闭 / 手动上传更新 */
#define ROUTE_PKG_STATUS "/pkgmanager_status"
#define ROUTE_PKG_LAUNCH "/pkgmanager_launch"
#define ROUTE_PKG_STOP "/pkgmanager_stop"
#define ROUTE_PKG_UPLOAD "/pkgmanager_upload"
/* [CN-FORK] v0.5.2.131 ShadowMount+ 探活：游戏详情「去 ShadowMount 管理」要先问它
 * 起没起（本机 127.0.0.1:10101），没起才去拉。探测必须在本机做——从电脑用浏览器
 * 直连 10101 不一定通（SMP 可能只监听 loopback），前端 fetch 探不到。 */
#define ROUTE_SMP_STATUS "/smp_status"
/* [CN-FORK] 「常用入口」自定义快捷访问清单：列表（含探活）/ 新增 / 删除 */
#define ROUTE_QUICKLINKS "/quicklinks"
#define ROUTE_QUICKLINKS_ADD "/quicklinks_add"
#define ROUTE_QUICKLINKS_REMOVE "/quicklinks_remove"
/* [CN-FORK] 温控管理板块：只读 CPU/GPU/SSD 温度 + 风扇占空比快照（替代历史 /system_stats 的温控子集） */
#define ROUTE_THERMAL "/thermal_stats"
/* [CN-FORK] 游戏管理板块：游戏列表(app.db+扫描dump) + 单游戏图标(icon0.png) */
#define ROUTE_GAME_LIST "/game_list"
#define ROUTE_GAME_ICON "/game_icon"
/* [CN-FORK] v0.5.2.115 多媒体管理（截图/录屏）。契约 = frontend/src/utils/gameApi.js 的文件头，
 * 实现 = include/game_media.h 的文件头，两边是同一份。 */
#define ROUTE_GAME_MEDIA "/game_media"
#define ROUTE_GAME_MEDIA_THUMB "/game_media_thumb"
#define ROUTE_GAME_MEDIA_FILE "/game_media_file"
#define ROUTE_GAME_MEDIA_PACK "/game_media_pack"
#define ROUTE_GAME_MEDIA_PACK_GET "/game_media_pack_get"
#define ROUTE_GAME_MEDIA_DELETE "/game_media_delete"
/* [CN-FORK] 通用任务进度/取消：打包任务在用，日后游戏删除任务同样挂这两条 */
#define ROUTE_GAME_JOB_STATUS "/game_job_status"
#define ROUTE_GAME_JOB_CANCEL "/game_job_cancel"
/* [CN-FORK] v0.5.2.116 游戏管理模块余下的 19 条端点。契约 = frontend/src/utils/gameApi.js
 * 的文件头，与 .115 那批（游戏列表/图标/多媒体）同一份文档的续篇。
 * 分组照契约注释分：详情 → 删除与任务 → 存档 → 改名与磁贴 → 运行时。
 * /game_tile_icon 与 /game_tile_background 是**一路径两方法**（GET 取图 / POST 换图），
 * 所以只有一个宏，分发在 method 上分岔。 */
#define ROUTE_GAME_DETAIL "/game_detail"
#define ROUTE_GAME_DELETE "/game_delete"
#define ROUTE_GAME_SAVES "/game_saves"
#define ROUTE_GAME_SAVE_AUTO "/game_save_auto"
#define ROUTE_GAME_SAVE_TEST "/game_save_test"
#define ROUTE_GAME_SAVE_DOWNLOAD "/game_save_download"
#define ROUTE_GAME_SAVE_UPLOAD "/game_save_upload"
#define ROUTE_GAME_SAVE_DELETE "/game_save_delete"
/* [CN-FORK] v0.5.2.140 本地快照：立即备份 / 还原 / 删除（action 在 body 里）。
 * 列表不另开 GET —— 快照跟着 /game_saves 一起回（snapshots[]），弹窗一次拉全。 */
#define ROUTE_GAME_SAVE_SNAP "/game_save_snap"
/* [CN-FORK] v0.5.2.142 自动存档的**手动**推 / 拉（自动的那份由守护线程做）。
 * 用户手点「立即拉取 / 立即推送」走这里 —— 不依赖「游戏起没起」的探测。 */
#define ROUTE_GAME_SAVE_SYNC "/game_save_sync"
/* [CN-FORK] v0.5.2.145 只读地取某用户远程存档的元数据（大小 / 文件数 / 修改时间），
 * 供前端「远程游戏」标签展示。GET，tid / save 走 query。 */
#define ROUTE_GAME_SAVE_REMOTE_INFO "/game_save_remote_info"
/* [CN-FORK] v0.5.2.154 garlic 兼容导入：接受 garlic-savemgr（earthonion）导出的
 * **加密**存档镜像（PS5 裸 `sdimg_<名>` / PS4 的 `sdimg_<名>` + `<名>.bin` 的 zip），
 * 逐文件合并进对应存档目录（**不是**上传那种整份替换）。理由见 game_saves.h。 */
#define ROUTE_GAME_SAVE_GARLIC "/game_save_import_garlic"
#define ROUTE_GAME_RENAME "/game_rename"
/* [CN-FORK] v0.5.2.137 名字回填：把面板里的 name.txt 重放到主机 app.db/appinfo.db
 * （folder 类再写一次源 sce_sys/param.json）。见 game_title.h 的 game_title_sync_all。 */
#define ROUTE_GAME_SYNC "/game_title_sync"
#define ROUTE_GAME_TILE_ICON "/game_tile_icon"
#define ROUTE_GAME_TILE_ICON_RESET "/game_tile_icon_reset"
#define ROUTE_GAME_TILE_BACKGROUND "/game_tile_background"
#define ROUTE_GAME_TILE_BACKGROUND_RESET "/game_tile_background_reset"
#define ROUTE_GAME_TILE_BG "/game_tile_bg"
#define ROUTE_GAME_TILE_BG_DDS "/game_tile_bg_dds"
#define ROUTE_GAME_MOUNT "/game_mount"
#define ROUTE_GAME_UNMOUNT "/game_unmount"
#define ROUTE_GAME_UNINSTALL "/game_uninstall"
/* [CN-FORK] 风扇温控（模型 A）：读状态 / 下发目标温度到 /dev/icc_fan */
#define ROUTE_FAN_CONTROL "/fan_control"

#define MENU_VERSION "0.5.2.156" /* [CN-FORK] 本 fork 版本：上游基线 0.5.2（已合并 upstream/main）+ 构建序号（.156，延续自 0.5.1 时代的计数）；每次调整构建自动 +1 */
/* [CN-FORK] v0.5.2.155 下面这几个路径宏允许宿主测试用 -D 覆盖（`#ifndef` 保护）。
 * 「逗号列表 ↔ autoload.txt 行」的写回幂等是个纯逻辑问题（`.154`/`.155` 两轮都在修
 * 用户报的「自动加载页面自己增加 1 秒延时」），但这些路径原先写死在头文件里，
 * 宿主测不了 —— 两轮都只能靠真机复现。加了保护之后 test/config_autoload_test.c
 * 能把它们指到 /tmp 下去穷举。**默认值一个字没变。** */
#ifndef AUTOLOAD_CONFIG_PATH
#define AUTOLOAD_CONFIG_PATH "/data/nayeplmtools/autoload.txt"
#endif
#ifndef PLDMGR_CONFIG_PATH
#define PLDMGR_CONFIG_PATH "/data/nayeplmtools/nayeplmtools_config.txt"
#endif
/* [CN-FORK] v0.5.2.145 全局存档配置（设置页「存档配置」）单独一份 JSON 文件。
 * 不塞进 nayeplmtools_config.txt：那是 key=value 行式文件，config_upsert_value 的行缓冲
 * 只有 256 字节，而这个 JSON 六百多字节，写进去会被截断成半个 JSON（静默失效）。
 * 单独成文件还省掉了「JSON 里的引号怎么塞进行式文件」这一层转义。 */
#ifndef ARCHIVE_GLOBAL_PATH
#define ARCHIVE_GLOBAL_PATH "/data/nayeplmtools/archive_global.json"
#endif
#define REPOSITORY_CACHE_PATH "/data/nayeplmtools/repository_cache.json"
/* [CN-FORK] 安全模式哨兵文件：存在（内容无所谓）即在下次启动跳过全部引导期高危动作。
 * v0.5.1.64 起硬件探测已整体删除，本哨兵只负责跳过装机/杀光盘/开浏览器/自启序列。 */
#define PLDMGR_SAFE_MODE_FLAG "/data/nayeplmtools/safe_mode"
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
#ifndef BASE_DATA_DIR
#define BASE_DATA_DIR "/data/nayeplmtools"
#endif
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
