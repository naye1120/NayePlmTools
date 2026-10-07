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
 * 成功返回 0（persisted 已置位），-1 参数非法（msg 里给人话）。
 * persisted 与 pnote 都可为 NULL（调用方不关心源持久化状态）。 */
int game_title_set_name(const char *tid, const char *name,
                        char *msg, size_t msgsz,
                        int *persisted, char *pnote, size_t pnotesz);

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

#endif
