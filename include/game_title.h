#ifndef GAME_TITLE_H
#define GAME_TITLE_H

/* [CN-FORK] v0.5.2.116 游戏管理 → 改名 + 磁贴两层图（icon0 / pic0）后端
 *
 * 前端契约定死在 frontend/src/utils/gameApi.js 文件头（那一轮「只做前端 + mock」）：
 *
 *   POST /game_rename          {tid, name}      → {ok, name}
 *   POST /game_tile_icon?tid=  (PNG 原始字节)   → {ok, bytes}
 *   GET  /game_tile_icon?tid=                  → PNG（没设过 → 404）
 *   GET  /game_tile_icon_reset?tid=            → {ok}
 *   POST /game_tile_background?tid=            → {ok, bytes, dds_bytes}
 *   GET  /game_tile_bg?tid=                    → PNG 预览（没设过 → 404）
 *   GET  /game_tile_bg_dds?tid=                → DDS 原始字节（没设过 → 404）
 *   GET  /game_tile_background_reset?tid=      → {ok}
 *
 * ── 为什么自定义图要存两份 ────────────────────────────────────────────────
 *   ① 我们自己那一份（GAME_TITLE_DIR/<TID>/）是**接口的事实源**：
 *      /game_list 的 icon/bg 版本号、/game_detail 的 title_icon_custom/title_bg_custom、
 *      以及三条 GET 全都读它。它不受主机权限影响 —— 就算一张图都写不进 sce_sys，
 *      面板里的磁贴照样是自定义的，而且 set/reset 也不会因此假失败。
 *   ② 真正落到主机上那份（<sce_sys>/icon0.png 与 pic0.{png,dds}）是**给 VSH 看的**。
 *      路径不是猜的：src/app_installer.c 装自己的磁贴就是写这三个文件，
 *      而且 pic0 是「PNG + 真 BC1 DDS 一起装，VSH 到底认哪个待实机定论」——
 *      这里沿用同一套，一个变量都不多加。
 *      写之前先把它备份到 <GAME_TITLE_DIR>/<TID>/orig/，*_reset 还原备份。
 *      写不进去（只读 PFS、dump 在只读介质上、目录不存在）只记日志，不报错。
 *
 * ── 背景为什么必须是 1920×1080 DXT1 ──────────────────────────────────────
 *   前端上传前已经缩到 1920×1080，但**契约的硬指标是后端输出的那份**：
 *   128 + 480*270*8 = 1,036,928 字节，头布局与 console 已验证的 assets/pic0.dds
 *   逐字节一致（同 make_pic0.py 的 build_bc1_header）。所以这里**不信任入参尺寸**，
 *   不是 1920×1080 就先缩到 1920×1080，保证 /game_tile_bg_dds 永远是这个规格。
 */

#include <stddef.h>

#ifndef GAME_TITLE_DIR
#define GAME_TITLE_DIR "/data/nayeplmtools/titles"
#endif

/* 落到主机上的磁贴目标根（app_installer.c 同款）。 */
#ifndef GAME_TITLE_APP_PREFIX
#define GAME_TITLE_APP_PREFIX "/user/app"
#endif

/* 单张图的原始字节上限。前端会先缩到 1024×1024 / 1920×1080，正常几百 KB；
 * 64MB 这条线是照着 mock 的 express.raw({limit:'64mb'}) 对齐的，防内存被打穿。 */
#define GAME_TITLE_MAX_PNG (64u << 20)

/* 名称长度上限（UTF-8 字节）。PS5 磁贴上那行字不长，128 足够，也避免有人塞一整段文章。 */
#define GAME_TITLE_MAX_NAME 128

/* ── 改名 ──────────────────────────────────────────────────────────────────
 * v0.5.2.132 改名写**三层**，事实源是第四层（游戏包里的 param.json）：
 *   ① GAME_TITLE_DIR/<TID>/name.txt   —— 面板层的接口事实源（列表/详情都读它盖名）
 *   ② app.db 平铺列 + appinfo.db       —— 电视主屏磁贴读它（见 src/tile_db.c）
 *   ③ 源 <sce_sys>/param.json          —— SMP 重扫时据此重建 ②，**只有改了它才不回退**
 *   ④ name.txt + ② + ③ 三者一致才算「改完」
 * ③ 只对 folder（裸目录）游戏可写；image（.ffpfsc 镜像）与 pkg（系统内置）不可写，
 * 由 persisted / pnote 两个出参如实报告，**不假成功**。
 *
 * [CN-FORK] v0.5.2.165 再加一对出参 host_ok / hnote，报的是**第 ② 层（主机磁贴数据库）**：
 * 这一层的成败以前只进日志、不回前端，于是「app.db 根本没写进去」和「写进去了但 Shell
 * 还没重绘」在界面上长得一模一样 —— 用户看到的就是「改完名，PS5 上没生效」。
 *   · host_ok = 1：app.db / appinfo.db 已更新（或本来就一致）；
 *   · host_ok = 0：**主机端没生效**，hnote 说原因（「这个游戏还没注册进主机数据库」等）。
 * ⚠ host_ok = 1 **不代表主屏立刻变名**：磁贴名字与自定义图标同源，Shell 只在
 * **重启 / 待机唤醒**时才重绘（真机取证见 src/tile_db.c 头注释）。这句提示归前端说，
 * 后端只负责把「写没写进去」这件事说清楚。
 *
 * 成功返回 0（persisted / host_ok 已置位），-1 参数非法（msg 里给人话）。
 * persisted / pnote / host_ok / hnote 都可为 NULL（调用方不关心对应状态）。 */
int game_title_set_name(const char *tid, const char *name,
                        char *msg, size_t msgsz,
                        int *persisted, char *pnote, size_t pnotesz,
                        int *host_ok, char *hnote, size_t hnotesz);

/* 读自定义名。1 = 有自定义名（out 已填），0 = 没改过（out 置空）。
 * 游戏列表 / 详情都用它盖掉 app.db 里的原名 —— 两边必须走同一个口，
 * 否则列表显示新名、详情显示旧名，用户会以为改名没生效。 */
int game_title_get_name(const char *tid, char *out, size_t osz);

/* ── 两层图 ────────────────────────────────────────────────────────────────
 * 版本号：0 = 没设过（前端据此不去取图，省掉每款游戏两次白跑的 404）；
 * >0 = 自定义过，取到的值当 query 参数打散浏览器缓存。 */
long long game_title_icon_ver(const char *tid);
long long game_title_bg_ver(const char *tid);

/* 三个 GET 的落点。0 = 存在（out 填绝对路径），-1 = 没设过（路由据此回 404）。 */
int game_title_icon_path(const char *tid, char *out, size_t osz);
int game_title_bg_png_path(const char *tid, char *out, size_t osz);
int game_title_bg_dds_path(const char *tid, char *out, size_t osz);

/* POST 上传。png/len 是请求体原始字节。
 * 成功返回 0（*out_bytes = 收到的字节数；out_dds 仅背景用，填 DDS 尺寸），
 * -1 参数/内容非法，msg 里给人话；-2 内部写失败。 */
int game_title_set_icon(const char *tid, const unsigned char *png, size_t len,
                        long long *out_bytes, char *msg, size_t msgsz);
int game_title_set_bg(const char *tid, const unsigned char *png, size_t len,
                      long long *out_bytes, long long *out_dds,
                      char *msg, size_t msgsz);

/* *_reset：删我们这一份 + 把备份还原回 sce_sys。都返回 0。 */
int game_title_reset_icon(const char *tid);
int game_title_reset_bg(const char *tid);

/* ── 给 game_mgr.c 用的小工具（列表项 / 详情都要盖名）──────────────────
 * 把 src 里的原名按我们存的覆盖，没改过就原样返回。out 可以和 src 相同。 */
const char *game_title_override_name(const char *tid, const char *orig,
                                     char *out, size_t osz);

/* 供 /game_detail 一次拿两个标志位（1 = 该层自定义过）。 */
void game_title_detail_flags(const char *tid, int *icon_custom, int *bg_custom);

/* ── [CN-FORK] v0.5.2.137 名字回填（把 name.txt 重放到主机各层）──────────────
 *
 * 为什么需要它：改名只在**改名那一刻**写主机。真机上撞到过一次非常典型的时序坑 ——
 * 用户在 .132（写源 param.json）之前改的 8 款游戏，主机侧只有 name.txt + app.db；
 * 之后 app.db 被 SMP 重扫按源重建，中文名全被冲回英文原名，而面板一直是中文。
 * 用户看到的现象就是「面板是对的、SMP 与主屏是错的」，而重新手点 8 次改名谁也不愿意。
 *
 * 这条链做的是**幂等重放**：遍历 GAME_TITLE_DIR 下每一个有 name.txt 的 tid，
 *   ① tile_db_set_name  → 主机 app.db 平铺列 + appinfo.db（**SMP 的 UI 读的就是它**）
 *   ② folder 类再加 tt_rename_in_param → 源 sce_sys/param.json + /user/app 副本
 * 已经一致的那些一次写都不发（tile_db.c 里那句 COUNT/SUM 先比后写），所以可以随时跑。
 *
 * 真机实测（v0.5.2.137 侦察）：SMP 1.7beta4 的 title_name 读的是系统 app.db，
 * **不是**源、也不是 /user/app/<TID>/sce_sys/param.json（后者单独改过、SMP 纹丝不动）；
 * 而 app.db 一改，SMP 的 UI 立刻跟着变，**镜像类也一样**。所以①是主力，
 * ②只是给「SMP 重启后重新注册」那条路留的持久化，镜像类写不了源也不影响两边一致。
 *
 * 只碰我们自己拥有的两个库与（folder 类）游戏源，**不动镜像本体**；全程 best-effort，
 * 任何一款失败都只记日志、不打断其余。返回 0（永远），出参给路由/日志用：
 *   scanned   = 扫到几个有 name.txt 的 tid
 *   applied   = 其中主机索引（app.db/appinfo.db）真的被写过或本来就一致的个数
 *   persisted = 其中名字**源**也写成功的个数（只有 folder 类可能）
 *   failed    = 写了但失败的个数
 * msg 里给一句给人话的总结（路由直接回给前端）。 */
int game_title_sync_all(char *msg, size_t msgsz, int *scanned, int *applied,
                        int *persisted, int *failed);

/* 启动自愈：起一条 detached 线程，等启动期那阵 IO 过去后跑一次 game_title_sync_all。
 * 这一步是「重启之后名字又回去了」的兜底 —— 不依赖用户手点，也不依赖 SMP 何时重扫。
 * safe_mode 下整条跳过（见 utils.h 的 pldmgr_utils_safe_mode）。主进程常驻，
 * 待机唤醒不会重复起线程（与 fan_control_start_daemon 同一口径）。 */
void game_title_sync_start_daemon(void);

#endif
