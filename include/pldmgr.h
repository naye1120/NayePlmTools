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
/* [CN-FORK] 「本程序」卡的重启生效：把已下载好的自更新 slot 用开机同一套
 * 引导交接拉起来（仅加载器确实收下新 ELF 才让当前进程退出、腾出 8911）。 */
#define ROUTE_APPLY_SELF_UPDATE "/apply_self_update"
/* [CN-FORK] v0.6.1.191 「本程序」卡的「启动/重启」：原地重新拉起一份与当前运行
 * 版本相同的已安装 ELF（同版本、不改版本，仅重启进程），复用同一套干净交接。 */
#define ROUTE_RESTART_SELF "/restart_self"
#define ROUTE_LOG "/log"
#define ROUTE_VERSION "/version"
#define ROUTE_GETIP "/getip"
#define ROUTE_GET_CONFIG "/get_config"
#define ROUTE_SET_CONFIG "/set_config"
#define ROUTE_ABORT "/abort"
#define ROUTE_AUTOLOAD_STATUS "/autoload_status"
#define ROUTE_AUTOLOAD_CLEAR "/autoload_clear"
#define ROUTE_REPO_ICONS "/repository_icons"
#define ROUTE_REPO_CACHE_CLEAR "/repository_cache_clear"
#define ROUTE_REPO_LIST "/repository_payloads"
#define ROUTE_REPO_REFRESH "/repository_refresh"
#define ROUTE_REPO_INSTALL "/repository_install"
#define ROUTE_UPDATE_CHECK "/update_check"
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
/* [CN-FORK] v0.6.0.177 多媒体的「远程同步」（设置页「多媒体存档配置」里的自动备份，
 * 与多媒体管理页那个手动推送按钮，共用这一条）。
 *   POST /game_media_sync?tid=   body {ids:[content_path,…]|null} → {job_id}
 * ids 缺省 / null / 空数组 = 全量（该 tid 下全部媒体），与 /game_media_pack 同一形状；
 * 任务进度同样走 /game_job_status 与 /game_job_cancel（全局单槽，两种任务互斥）。 */
#define ROUTE_GAME_MEDIA_SYNC "/game_media_sync"
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
/* [CN-FORK] v0.5.2.159 删掉**远端**那一份存档。POST + body {tid, save} ——
 * 与旁边的 /game_save_delete（GET）刻意不同：这是不可撤销的删除，GET 有被
 * 预取 / 重放的机会，宁可另开一条 POST（新 POST 路由要在 gr_post_limit 登记）。 */
#define ROUTE_GAME_SAVE_REMOTE_DELETE "/game_save_remote_delete"
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
/* [CN-FORK] v0.5.2.163 背景音乐：上传 / 读取 / 恢复默认。
 * 上限 30MB（与前端一致；前端先裁，但这里留 64MB 防止绕过）。 */
#define ROUTE_GAME_MUSIC "/game_music"
#define ROUTE_GAME_MUSIC_RESET "/game_music_reset"
/* [CN-FORK] v0.5.2.166 把编码好的 ATRAC9 写进主机的 <sce_sys>/snd0.at9。
 * `.163` 那条 /game_music 只落我们自己的目录，主机不读 —— 这才是「背景音乐
 * 改动没有生效」的根因。编码在浏览器里做（见 include/game_music.h 的长注释）。 */
#define ROUTE_GAME_MUSIC_AT9 "/game_music_at9"
/* [CN-FORK] v0.5.2.172 两条**下载**路由（GET，参数全走 query，浏览器自己落文件）：
 *   /game_save_snap_download?tid=&uid=&ts=   把一份**本地快照**打成 zip 直出
 *   /game_save_remote_download?tid=&save=    把**远端**那一份存档取回来直出
 * 与 /game_save_download（主机上当前那份存档）合起来，弹窗里三张列表的每一行
 * 都有一个「下载」。后者的要害是**一个字节都不碰主机存档**（不是 sr_pull）。 */
#define ROUTE_GAME_SAVE_SNAP_DOWNLOAD "/game_save_snap_download"
#define ROUTE_GAME_SAVE_REMOTE_DOWNLOAD "/game_save_remote_download"
/* [CN-FORK] v0.5.2.174 「快照活动」（GET，只读）：
 *   /game_save_snap_log?tid=&n=20  → {"ok":true,"tid":"…","events":[{t,lv,x}]}
 * 用户口径：「发生记录也没有写到屏幕上」—— 快照的**负向结果**（被静默窗口挡掉、
 * 内容没变、排队重试）原来只在 pldmgr.log 里，界面上一个像素都不落。
 * 这条只读最近 N 条事件（带时刻 / 级别），给「本地快照」那块界面用。
 * ⚠ 它是 GET 且被界面轮询 —— 记得加进 `is_noisy_route()`，否则每 4 秒一条日志。 */
#define ROUTE_GAME_SAVE_SNAP_LOG "/game_save_snap_log"
#define ROUTE_GAME_MOUNT "/game_mount"
#define ROUTE_GAME_UNMOUNT "/game_unmount"
#define ROUTE_GAME_UNINSTALL "/game_uninstall"
/* [CN-FORK] 风扇温控（模型 A）：读状态 / 下发目标温度到 /dev/icc_fan */
#define ROUTE_FAN_CONTROL "/fan_control"
/* [CN-FORK] v0.5.2.158 「个性化软件」：列出主机内置软件（含每个账号的显隐状态）/
 * 把某个内置软件在主屏上隐藏或显示。状态存在 app.db 的 tbl_iconinfo_<uid>.visible，
 * 不在 nayeplmtools_config.txt 里另抄一份（两边不一致时听谁的没法答）。 */
#define ROUTE_SYSTEM_APPS "/system_apps"
#define ROUTE_SYSTEM_APP_VISIBLE "/system_app_visible"
/* [CN-FORK] v0.5.2.164 「个性化软件」的第二个开关：置顶。
 * v0.6.0.179 起它**写主机**：app.db 的 tbl_group_<uid>（groupName='MyHome'，
 * idx 就是主屏位置）—— 与游戏磁贴那条链同一个载体。`.164` 写的是本面板的
 * app_pins.json，主机压根不知道，用户看到的就是「置顶按了没反应、重启就重置」；
 * 那份文件现在退化成「意图记录」，只给开机对账用（见 src/app_pin.c）。 */
#define ROUTE_SYSTEM_APP_PIN "/system_app_pin"
/* [CN-FORK] v0.6.0.179 「个性化软件」：给内置软件换图标 / 背景。
 *
 * 内置软件（NPXS）没有可写的 sce_sys（资产全在只读的 /system_ex/rnps/apps/<TID>），
 * 所以走的是**列指针**：图存在我们自己的 /data/nayeplmtools/apptiles/<TID>/ 下，
 * 只把 app.db 的 icon0Info / pic0Info 指过去。取值与理由见 include/app_tile.h 头注释。
 *   GET  /app_icon?tid=            → PNG（有自定义回我们那份，否则回主机自带那张）
 *   GET  /app_tile_bg?tid=         → PNG 预览（我们存的那份自定义背景）
 *   POST /app_tile_icon?tid=       → 裸 PNG 请求体 → {ok,bytes,message}
 *   POST /app_tile_background?tid= → 裸 PNG 请求体 → {ok,bytes,dds_bytes,message}
 *   GET  /app_tile_icon_reset?tid= → {ok,message}（还原主机那一列 + 删我们这份）
 *   GET  /app_tile_background_reset?tid= → {ok,message}
 * 两条 reset 与 /game_tile_*_reset 同形：**变更型 GET**，要过 CSRF 校验。 */
#define ROUTE_APP_ICON "/app_icon"
#define ROUTE_APP_TILE_BG "/app_tile_bg"
#define ROUTE_APP_TILE_ICON "/app_tile_icon"
#define ROUTE_APP_TILE_BACKGROUND "/app_tile_background"
#define ROUTE_APP_TILE_ICON_RESET "/app_tile_icon_reset"
#define ROUTE_APP_TILE_BACKGROUND_RESET "/app_tile_background_reset"
/* [CN-FORK] v0.5.2.164 游戏磁贴的收藏 / 置顶（面板侧列表排序，落 game_prefs.json）。
 * GET  /game_prefs           → {"prefs":{"<TID>":{"fav":bool,"pin":bool}, ...}}
 * POST /game_prefs_set       body {tid, fav?, pin?} → {ok, tid, fav, pin} */
#define ROUTE_GAME_PREFS "/game_prefs"
#define ROUTE_GAME_PREFS_SET "/game_prefs_set"
/* [CN-FORK] v0.5.2.166 主屏固定列表（主机 app.db 的 tbl_group_<uid>）。
 * `.164` 的置顶只写我们自己的 JSON，主机不知道 —— 用户看到的就是「置顶在 PS5 里没生效」。
 * 这一组路由操作的是**主机的库**，见 include/tile_group.h 的真机取证。 */
#define ROUTE_TILE_GROUP "/tile_group"             /* GET：读主屏固定列表 */
#define ROUTE_TILE_GROUP_PIN "/tile_group_pin"     /* POST：置顶 / 取消置顶 */
#define ROUTE_TILE_GROUP_ORDER "/tile_group_order" /* POST：按给定顺序重排已固定项 */

#define MENU_VERSION "0.6.1.192" /* [CN-FORK] 本 fork 版本：上游基线 0.6.1（已合并 upstream/main v0.6.1，merge 见 HANDOVER.md §0）+ 构建序号（.192，与 .191 同代码集，仅版本号 +1 作为面板内 OTA 自更新的验证目标）；每次调整构建自动 +1 */
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
/* [CN-FORK] v0.6.0.177 多媒体存档配置（设置页「多媒体存档配置」弹窗）。
 * 与 ARCHIVE_GLOBAL_PATH 是**两份互不相干**的配置（用户口径：多媒体有自己的
 * SMB/FTP 位置，不跟存档那份绑死）。同样是 JSON 原文单独成文件，理由同上：
 * 行式 config_upsert_value 的 256 字节行缓冲装不下它，装下了也会截断成半个 JSON。 */
#ifndef MEDIA_ARCHIVE_PATH
#define MEDIA_ARCHIVE_PATH "/data/nayeplmtools/media_archive.json"
#endif
/* [CN-FORK] v0.6.0.177 增量备份的「已经传过哪些文件」账本，一个游戏一份
 * （<tid>.json）。按 tid 分文件而不是一个大账本：媒体条目动辄上千条，
 * 单账本要整读整写，而分文件之后每次同步只碰自己那一份。 */
#ifndef MEDIA_BACKUP_STATE_DIR
#define MEDIA_BACKUP_STATE_DIR "/data/nayeplmtools/media_backup"
#endif
/* [CN-FORK] 上游 v0.6.0 新增「收藏」列表（payload 槽位级，见 src/slot.c）。
 * 与 .164 的 game_prefs.json（游戏磁贴收藏）是两件事：那份管游戏列表排序，
 * 这份管 payload 槽位的收藏标记。路径按本 fork 约定落在 nayeplmtools 下。 */
#define FAVORITES_CONFIG_PATH "/data/nayeplmtools/favorites.txt"
#define REPOSITORY_CACHE_PATH "/data/nayeplmtools/repository_cache.json"
/* [CN-FORK] 安全模式哨兵文件：存在（内容无所谓）即在下次启动跳过全部引导期高危动作。
 * v0.5.1.64 起硬件探测已整体删除，本哨兵只负责跳过装机/杀光盘/开浏览器/自启序列。 */
#define PLDMGR_SAFE_MODE_FLAG "/data/nayeplmtools/safe_mode"
/* [CN-FORK] v0.6.0.178 起下面三个路径也允许宿主测试用 -D 覆盖（`#ifndef` 保护，
 * 同 BASE_DATA_DIR 那批）：引导器同步的真实用例要凭空造 /data/ps5_autoloader
 * 与 /data/nayeplmtools/payloads 两个目录，主机上跑不了。 */
#ifndef PAYLOADS_STORAGE_DIR
#define PAYLOADS_STORAGE_DIR "/data/nayeplmtools/payloads"
#endif
/* [CN-FORK] v0.6.0.176 U 盘上本工具的库目录名。上游用 "pldmgr"；本 fork 的数据目录
 * 整体叫 nayeplmtools（见 BASE_DATA_DIR 与 SCAN_DIRS），U 盘上保持一致。
 * 旧名 pldmgr / payload-manager 仍被当作 legacy 识别（见 src/slot.c 的迁移判定）。 */
#define PLDMGR_USB_LIB_NAME "nayeplmtools"
/* [CN-FORK] 越狱引导器的自动加载目录，安装 payload 后若同名工具在此则同步更新 */
#ifndef PS5_AUTOLOADER_DIR
#define PS5_AUTOLOADER_DIR    "/data/ps5_autoloader"
#endif
#ifndef PS5_AUTOLOADER_CONFIG
#define PS5_AUTOLOADER_CONFIG PS5_AUTOLOADER_DIR "/autoload.txt"
#endif
/* [CN-FORK] v0.6.0.178 本工具自己的名字。autoload.txt 里按「去掉版本号的工具名」匹配
 * 工具，这个名字同时决定引导器目录里的文件名前缀：NayePlmTools_<版本><变体>.elf。
 * 注意它不是 is_pldmgr_name() 认的名字 —— 那个白名单是给「本工具不许从网页上传」
 * 用的历史遗留，两者故意不同名。 */
#define PLDMGR_APP_NAME "NayePlmTools"
/* [CN-FORK] 出包变体后缀：加 -DTILE_VARIANT_GAME 出的是游戏栏包（NAYE00004），
 * 默认是媒体栏包（NAYE00001）。引导器目录里沿用这套后缀。 */
#ifdef TILE_VARIANT_GAME
#define PLDMGR_VARIANT_SUFFIX "-ga"
#else
#define PLDMGR_VARIANT_SUFFIX "-me"
#endif
#define REPOSITORY_SOURCE_URL                                                  \
  "https://itsplk.github.io/ps5-payloads-mirror/payloads_v2.json"
#define LEGACY_REPOSITORY_SOURCE_URL                                           \
  "https://itsplk.github.io/ps5-payloads-mirror/payloads.json"
#define REPOSITORY_REFRESH_INTERVAL_SEC 10800

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
