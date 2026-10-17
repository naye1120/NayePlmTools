#ifndef APP_TILE_H
#define APP_TILE_H

/* [CN-FORK] v0.6.0.179 「个性化软件」→ 换图标 / 换背景。
 *
 * ── 为什么内置软件不能走游戏那条链 ─────────────────────────────────────────
 * 游戏那两条（game_title_set_icon / _set_bg）先把图写进**这款游戏自己的
 * sce_sys**，再登记 app.db。内置软件（NPXS）没有可写的 sce_sys：真机取证
 * （`_ftp_probe/vis_evidence/app_current.db`，19 条内置软件全量）里
 * `tbl_contentinfo.metaDataPath` 与 AppInfoJson 的 `_org_path` 全部落在
 * **`/system_ex/rnps/apps/<TID>/appdb/default`**（系统只读区），而
 * `game_mgr_sce_sys_dir()` 只认 /user/app 那几个前缀，对 NPXS 一律返回 0。
 * 照游戏那条路走，结果就是「面板说写好了、主屏一动不动」——
 * 与 `.164` 的「置顶」是同一个病，这一轮不重犯。
 *
 * ── 内置软件的正确载体：那两列指针 ─────────────────────────────────────────
 * 同一次取证里，内置软件的
 *     icon0Info = /system_ex/rnps/apps/NPXS40037/appdb/default/icon0.png?ts=…
 *     pic0Info  = (NULL)
 * 形状与游戏换背景时写的那一列**完全一样**：`<文件路径>?ts=<秒>`。
 * 也就是说 Shell 读的是「这一列指的文件」，而不是「某个固定目录下的文件」。
 * 于是内置软件这条链全部落在我们自己的目录里：
 *
 *     /data/nayeplmtools/apptiles/<TID>/icon0.png   ← icon0Info 指它
 *     /data/nayeplmtools/apptiles/<TID>/pic0.dds    ← pic0Info 指它
 *     /data/nayeplmtools/apptiles/<TID>/bg.png      ← 面板自己预览用（/app_tile_bg）
 *
 * 两个文件名都是**照抄主机原来的形状**取的，不是随手起的：
 * 原来的值结尾就是 `/icon0.png`；而背景那条 `.118` 真机消融的结论是
 * 「Shell 找的是 pic0.dds 这个名字」。两种解释（Shell 用整条路径 / Shell 拿目录
 * 再按名字找）下，这两个名字都对。
 *
 * ⚠ **尚未真机验收**：这套「把列指到 /data 下我们自己的文件」对内置软件是否
 * 真的被 Shell 采纳，只有插上电视点一次才知道（游戏那条是消融验证过的，
 * 内置软件只是形状相同）。所以这条链做了三件事兜底：
 *   ① 写之前先把**原始那一串**存进底片（`orig_icon0info.txt` / `orig_pic0info.txt`），
 *      还原时按原样写回（原来是 SQL NULL 就写回 NULL，不是空串）；
 *   ② 面板这一份图永远先落盘 —— 就算主机一列都没写进去，面板里也看得见自己设的图，
 *      而且接口会**如实**把「主机没写进去」放进 message，不假报成功；
 *   ③ 开机对账（app_tile_sync_all）会重放一次：主机重扫把行冲掉了能自己补回来。
 *
 * ── 分层 ───────────────────────────────────────────────────────────────────
 * 本文件分两组：路径 / 版本号 / 底片是**纯文件**（app_tile.c，不依赖 sqlite 与
 * stb，宿主测试量最轻的那几个套件也能链）；真正写主机那四条在 app_tile_write.c
 * （要 PNG 规范化 + DDS 编码 + 真跑 SQL）。分开是为了让 app_visibility.c
 * 只为一个 custom_icon 字段就把编解码器拖进每一个测试。 */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef APP_TILE_DIR
#define APP_TILE_DIR "/data/nayeplmtools/apptiles"
#endif

/* 落盘用的三个文件名（见头注释）。 */
#define APP_TILE_ICON_LEAF   "icon0.png"
#define APP_TILE_BG_DDS_LEAF "pic0.dds"
#define APP_TILE_BG_PNG_LEAF "bg.png"

/* icon0Info 那一列（与 tile_db.h 的 TILE_DB_ICON_COLUMN 同一个值，
 * 两处各写一份宏是因为各自只 include 自己需要的头，与仓库里别处的口径一致）。 */
#define APP_TILE_ICON_COLUMN "icon0Info"

/* 上传图片的上限。与游戏那条链同一个量级（实测几 MB），64MB 是防呆天花板。 */
#define APP_TILE_MAX_PNG (64u * 1024u * 1024u)

/* tid 是不是「主机内置软件」：NPXS 前缀 + 6..16 位 A-Z0-9。
 * 与 app_visibility.c 的 av_tid_ok 同规格（那边也复用了这一条，不再各写一份）。 */
int app_tile_is_builtin(const char *tid);

/* 目录与三个文件的落点。dir 只算路径（0 / -1 非法）；三个 *_file 只在**盘上有**
 * 时回 0，否则回 -1（调用方据此决定回 404 还是回主机自带那张）。 */
int app_tile_dir(const char *tid, char *out, size_t osz);
int app_tile_icon_file(const char *tid, char *out, size_t osz);
int app_tile_bg_dds_file(const char *tid, char *out, size_t osz);
int app_tile_bg_png_file(const char *tid, char *out, size_t osz);

/* 自定义图的版本号（0 = 没设过）。前端拿它当图片 URL 的 ?v=，也是列表里
 * 「已自定义」那个标记；只有数字变了，浏览器才会重新取图。 */
long long app_tile_icon_ver(const char *tid);
long long app_tile_bg_ver(const char *tid);

/* 面板这一份的落盘 / 删除（纯文件）。0 = 成功。store 会先建目录再写，
 * 写完才 bump 版本号；drop 删文件并 bump（让 ?v= 立刻失效，别留在缓存里）。 */
int app_tile_store_icon(const char *tid, const unsigned char *png, size_t len);
int app_tile_store_bg(const char *tid, const unsigned char *dds, size_t dds_len,
                      const unsigned char *png, size_t png_len);
int app_tile_drop_icon(const char *tid);
int app_tile_drop_bg(const char *tid);

/* 原始列值的底片。is_bg：0 = icon0Info、1 = pic0Info。
 * save_once 只在**还没有底片**时写（第一次动手前那一份才是原始值，之后再写就是
 * 我们自己改过的值了）；value == NULL 表示原本是 SQL NULL。
 * read：0 = 有底片（*is_null 告诉是不是 NULL），-1 = 没留过底片。 */
int app_tile_orig_save_once(const char *tid, int is_bg, const char *value);
int app_tile_orig_read(const char *tid, int is_bg, char *out, size_t osz, int *is_null);

/* ── 写主机那四条（实现落在 app_tile_write.c）───────────────────────────────
 *
 * 返回约定与游戏那两条一致：
 *   0 = 面板这一份已经存好（**主机那一列有没有写进去看 out_host_ok / message**）；
 *  -1 = 参数/图片不合法（msg 给人话，接口回 400）；
 *  -2 = 面板这一份都没写下去（磁盘问题，接口回 500）。
 * 之所以把「主机没写进去」当成成功：面板这一份是接口的事实源（列表里的图标、
 * /app_tile_bg 预览都读它），主机那一层是 best-effort；但 message 里一定写明，
 * 前端据此提示，绝不出现「界面说好了、电视上没变、还没有一句解释」。
 *
 * out_host_ok 是给前端判「该报成功还是该报一句警告」用的 —— 光看 message 去嗅
 * 字符串太脆。与 /game_rename 的 host_ok / host_note 是同一套口径：
 *   1 = 主机那一列已经指着我们这份文件；
 *   0 = 面板存好了、主机没写（没留成底片 / 那一行读不到 / 写库失败，原因在 msg 里）。 */
int app_tile_set_icon(const char *tid, const unsigned char *png, size_t len,
                      long long *out_bytes, int *out_host_ok,
                      char *msg, size_t msgsz);
int app_tile_set_bg(const char *tid, const unsigned char *png, size_t len,
                    long long *out_bytes, long long *out_dds, int *out_host_ok,
                    char *msg, size_t msgsz);

/* 还原：把底片里那一串原样写回主机，再删掉面板这一份。
 * 0 = 还原完（或本来就没设过）；-1 = 参数不合法；-2 = 主机那一列没还原成。 */
int app_tile_reset_icon(const char *tid, char *msg, size_t msgsz);
int app_tile_reset_bg(const char *tid, char *msg, size_t msgsz);

/* 主机自带图标的路径：读 tbl_contentinfo.icon0Info，把 `?ts=` 尾巴剥掉。
 * 0 = 拿到了（out 已填），-1 = 没有这一行 / 这一列是 NULL / 读不了。
 * /app_icon 在没有自定义图标时用它，列表里显示的才是主机上真实那张图。 */
int app_tile_host_icon_path(const char *tid, char *out, size_t osz);

/* 开机对账：遍历我们自己的 apptiles 目录，把**盘上还在**的那几张图重新登记一遍。
 * 主机重扫（SMP / 重装）会把 tbl_contentinfo 那一行按源重建，两列一起丢；
 * 已经一致的会先比一遍，一致就一次写都不发，所以可以每次开机无条件跑。 */
int app_tile_sync_all(char *msg, size_t msgsz, int *checked, int *applied, int *failed);

#ifdef __cplusplus
}
#endif

#endif /* APP_TILE_H */
