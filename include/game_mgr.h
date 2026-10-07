#ifndef GAME_MGR_H
#define GAME_MGR_H

#include <stddef.h>
#include <time.h>

/* [CN-FORK] 游戏管理模块。参考 ShadowMountPlus 的「游戏识别」思路，并移植 v0.5.1.63
 * stats.c 里那套成熟的 app.db(SQLite) 只读解析器与 param.sfo 解析，实现：
 *   ① 已安装   —— 枚举 /system_data/priv/mms/app.db 的 tbl_contentinfo（PPSA/CUSA/… 游戏本体）
 *   ② 已存在   —— 扫描 ShadowMount 式 scan paths（/data、/mnt/ext*、/mnt/usb*、/mnt/shadowmnt）
 *                  里含 sce_sys/param.sfo 的 <TITLE_ID> 目录
 * 合并去重（按 TITLE_ID，已安装优先）。
 *
 * 重要：进页面一次性拉取 + mutex 串行 + 60s 缓存，绝不轮询。app.db 全库读取 + 目录扫描
 * 是有成本的 IO，频繁调用同样有风险（参考 /system_stats 死机教训）。
 *
 * v0.5.2.133 起加 force 参数：用户手点「刷新」时跳过 60s 快照直接重建。
 * 进页面那次 load 仍然吃缓存（快），手动刷新才付一次全库读的成本 —— 手点本来就是
 * 显式意图，不是轮询。不加这条的话，外部工具（FTP、SMP 自己扫了新游戏）改了 app.db，
 * 用户连点十次刷新也看不到，只会等 60 秒自己过期，体感就是「刷新按钮没反应」。 */

/* 输出 {"count":N,"games":[{"tid":"PPSAxxxxx","name":"…","src":"installed|dump"},...]}。
 * name 经 JSON 转义（含中文）。返回写入长度。
 * force 非 0 时忽略缓存快照，重新枚举（app.db 全库 + 扫描路径），并刷新缓存时间戳。 */
int game_mgr_list_json(char *buf, size_t size, int force);

/* [CN-FORK] v0.5.2.116 单个游戏的完整详情（/game_detail）。
 * 字段与 frontend/src/utils/gameApi.js 文件头的「详情」那一段一一对应。
 * 返回写入长度；**-1 = 这个 tid 不在任何一份枚举里**，路由据此回 404
 * （宁可 404 也不回一份凭空拼的详情 —— 前端拿到假字段会显示假按钮）。 */
int game_mgr_detail_json(const char *tid, char *buf, size_t size);

/* [CN-FORK] v0.5.2.116 /game_delete 要的三元组，路径算法与 /game_detail 共用。
 * v0.5.2.127 起 src 回的是**来源类别**（"pkg" | "image" | "folder"，由物理源那两个
 * lnk 文件判定），**不是** a.d_src 那个「从哪枚举到的」—— ShadowMount 的游戏也在
 * app.db 里，用 d_src 会把它们全当成内置 PKG，删源被 403 挡死。
 * 返回 1 = 找到这个游戏（src/path/runtime 均已填，runtime 可能为空串 = 没注册挂载）；
 * 返回 0 = 这个 tid 不在任何一份枚举里（路由据此 404）。 */
int game_mgr_source(const char *tid, char *src, size_t ssz,
                    char *path, size_t psz, char *runtime, size_t rsz);

/* [CN-FORK] v0.5.2.132 同上但**不枚举 app.db**：只读 /user/app/<TID>/ 下那两个 lnk
 * 文件解析物理源，毫秒级。给改名这种写路径用 —— 写之前没必要为了个来源类别
 * 把 app.db 全库读一遍（真机上几十到上百毫秒）。
 * kind 出 "pkg" | "image" | "folder"；path 出物理源（folder = 源目录，image = 镜像文件）。
 * 返回 1 = 判定完成，0 = tid 不合法。 */
int game_mgr_source_paths(const char *tid, char *kind, size_t ksz,
                          char *path, size_t psz);

/* [CN-FORK] v0.5.2.116 让 60s 列表缓存立刻过期（删除/卸载/删除源之后调）。 */
void game_mgr_invalidate(void);

/* [CN-FORK] v0.5.2.133 诊断用：返回当前快照的时间戳（0 = 空 / 已作废）。
 * 只给 test/game_mgr_test.c 做时间戳递进的断言用，产品代码不该依赖它。 */
time_t game_mgr_cache_ts(void);

/* [CN-FORK] v0.5.2.116 path 是否落在某个已知 dump 扫描根**之下**（必须是子项，
 * 根本身不算）。删除前的白名单只认两种源：注册过的镜像文件，或这个判据为真的目录。 */
int game_mgr_is_scan_path(const char *path);

/* 解析某 titleId 的图标文件绝对路径写入 out（icon0.png）。成功返回 1，找不到返回 0。
 * 候选顺序：/user/app/<TID>/sce_sys → 该 TID 沙盒 app0/sce_sys → /user/appmeta/<TID> →
 * /system_data/priv/appmeta/<TID>。供 /game_icon?tid= 端点读取。tid 必须是 9 位 alnum。 */
int game_mgr_icon_path(const char *tid, char *out, size_t osz);

/* [CN-FORK] v0.5.2.116 找这个游戏**放磁贴图的 sce_sys 目录**（已存在才返回，绝不新建）。
 * 顺序：/user/app/<TID>/sce_sys → mount.lnk 解出的 dump 根/sce_sys → scan paths 下的
 * <path>/<TID>/sce_sys。找不到返回 0 —— 调用方**不要**自己 mkdir，见 game_title.c。 */
int game_mgr_sce_sys_dir(const char *tid, char *out, size_t osz);

/* [CN-FORK] v0.5.2.116 读一个 param.sfo 里的字符串字段（key 如 "SAVEDATA_TITLE"）。
 * 0 成功（out 已填），非 0 失败（out 置空）。存档槽的显示名用它。 */
int game_mgr_sfo_text(const char *path, const char *key, char *out, size_t osz);

/* [CN-FORK] app.db 路径统一出口：游戏列表枚举与「常用入口」磁贴链接同步共用。
 * #ifndef 与 test/run_host_tests.sh 里 -DGAME_MGR_APPDB_PATH 那条同一个手法
 * （game_media.h / game_saves.h 已经是这么写的），给 game_mgr.c 留一个能在主机上跑的
 * 口子 —— v0.5.2.116「游戏列表整个空掉」的 bug 就卡在这个文件主机编不过、一条断言都没有。
 * 别去掉 #ifndef，默认值保持原样。 */
#ifndef GAME_MGR_APPDB_PATH
#define GAME_MGR_APPDB_PATH "/system_data/priv/mms/app.db"
#endif

/* [CN-FORK] deeplink 枚举回调：tid/titleId 原样（不做游戏前缀过滤，自制磁贴也可能收录）、
 * name 取不到时回落 tid、uri 保证以 http:// 或 https:// 开头才回调。返回非 0 提前停止遍历。 */
typedef int (*game_mgr_deeplink_cb)(const char *tid, const char *name, const char *uri, void *user);

/* [CN-FORK] 遍历 app.db tbl_contentinfo 的 pprDeeplinkUri 列（列序号从建表 SQL 实时定位，
 * 不写死）。返回命中条数；app.db 打不开/损坏返回 -1（调用方可重试），
 * 页面正常但没有该列视为 0 条（非错误，别每次请求都重试全库解析）。 */
int game_mgr_enum_deeplinks(game_mgr_deeplink_cb cb, void *user);

#endif
