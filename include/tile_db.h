/* [CN-FORK] v0.5.2.118 磁贴「写回主机」的两条 SQLite UPDATE 链。
 *
 * 面板这一层（name.txt / 我们自己那份 icon.png、bg.png）改完只对**我们自己**可见：
 * 主屏磁贴的名字根本不是文件，背景也还要 app.db 点一句才画 —— 具体见 src/tile_db.c
 * 头注释里抄录的真机结论。这里只暴露两个入口，SQL 全在 .c 里。
 *
 * 读侧（game_mgr.c 的 st_* 只读解析器）一个字节都没动；写侧必须真跑 SQL。 */
#ifndef TILE_DB_H
#define TILE_DB_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 与 game_mgr.h 的 GAME_MGR_APPDB_PATH 是**同一个文件**（读写各持一个宏，
 * 因为两边都只 include 自己需要的头）。用 #ifndef 包住是为了让主机测试照旧
 * 能用 -DTILE_DB_APPDB_PATH 把它指到临时目录 —— 手法与 game_mgr.h 那条一致。 */
#ifndef TILE_DB_APPDB_PATH
#define TILE_DB_APPDB_PATH "/system_data/priv/mms/app.db"
#endif
#ifndef TILE_DB_APPINFO_PATH
#define TILE_DB_APPINFO_PATH "/system_data/priv/mms/appinfo.db"
#endif

/* 主屏背景只认这一列（列名写死是参考工程真机消融出来的结论，不是猜的）。 */
#define TILE_DB_BG_COLUMN "pic0Info"

/* [CN-FORK] v0.5.2.158 首次动手前给两个库各留一份**不可覆盖**的原始底片
 * （已有就绝不重写，见 tile_db.c 的 td_backup_once）。
 * 原来只有本文件的两条写链调它；app_visibility.c 写 visible 之前同样要先备一份，
 * 就把它提成了公开入口 —— 备份目录与判据都沿用同一份，不另起一套。 */
void tile_db_backup_dbs(void);

/* 改名：把 app.db 的 titleName / conceptName / primaryTitleName 与 appinfo.db 的
 * TITLE(**含全部 TITLE_00..30 语言槽**) / concept_name(**含全部 concept_name_00..30**)
 * 一起改成 new_name，一个库一个事务，只按 titleId（或读回来的 localConceptId）定位，
 * 绝不动别人的行。
 *
 * ⚠ [CN-FORK] v0.5.2.170 TITLE_NN / concept_name_NN 是**语言维度**（简中=11、繁中=10、
 * 韩=09、泰=27…）。早年只写 TITLE + TITLE_01，于是简中系统读 TITLE_11 读到的是旧名，
 * 表现为「改名后主屏/SMP 里还是旧名」。现在按 `key='TITLE' OR key GLOB 'TITLE_[0-9]*'`
 * 全写，且用 GLOB 排掉 TITLE_ID（那是标题号，绝不能被改成游戏名）。
 *
 * 返回 0 = 名字现在就是 new_name（刚写进去，或本来就一致）；
 *      -1 = 参数不合法 / 库打不开 / 一行都没匹配到（游戏还没注册进 app.db）；
 *      -2 = 尝试写了但失败。
 * 永不致命，msg 里给一句中文人话（成功路径也可能留空）。 */
int tile_db_set_name(const char *tid, const char *new_name,
                     char *msg, size_t msgsz);

/* 换背景：把 app.db 里所有带 pic0Info 列的表、属于这个 title 的行改成
 * "<dds_path>?ts=<mtime>"。dds_path 必须是**我们刚写进主机的那个文件** ——
 * 调用方在 tt_apply 之后传，保证 ts 与盘上那份同步。
 *
 * 返回 0 = 至少写了一行（Shell 实时重绘，不用重启）；
 *      -1 = 参数不合法 / 文件不在 / 库打不开 / 没匹配到行；
 *      -2 = 尝试写了但失败。 */
int tile_db_set_bg(const char *tid, const char *dds_path,
                   char *msg, size_t msgsz);

/* ── [CN-FORK] v0.6.0.179 「图片列」的通用三件套 ────────────────────────────
 *
 * 为什么要有这一层：内置软件（NPXS）的图标/背景**不能**照游戏那条链写。
 * 真机取证（`_ftp_probe/vis_evidence/app_current.db`，19 个内置软件：
 * `AppInfoJson._org_path` / `metaDataPath` 全在 **`/system_ex/rnps/apps/<TID>`** 下，
 * 而 `icon0Info` 就是那条链上的一张只读的 `/system_ex/rnps/.../icon0.png`），
 * 可游戏的写链（game_mgr_sce_sys_dir）**只认 /user/app 那几个前缀**，
 * 对 NPXS 一律返回 0 —— 于是「上传成功、主屏没变」会原样复发（.164 置顶那个病）。
 *
 * 换背景那一列（pic0Info）当年就是「Shell 只看这一列」推出来的（见 .118 的消融），
 * 图标那一列（icon0Info）形状完全一样：`<文件路径>?ts=<秒>`。所以对内置软件，
 * 我们把图放在**我们自己的目录**里，只把这两列指过去 —— 一个字节都不写进系统只读区。
 *
 * 三件事分开给，是因为「写」有两个语义完全不同的调用方（上传 = 指向我们刚写的文件，
 * 还原 = 把**原始那一串**原样写回去），而 read 是给还原留底片用的。 */
#define TILE_DB_ICON_COLUMN "icon0Info"

/* 把 app.db 里所有带该列的表、属于这个 title 的行改成 "<path>?ts=<mtime>"。
 * 返回 0 = 至少写了一行；-1 = 参数不合法 / 文件不在 / 没匹配到行；-2 = 写了但失败。 */
int tile_db_set_image(const char *tid, const char *column, const char *path,
                      char *msg, size_t msgsz);

/* 把该列原样写成 value；value == NULL 表示**写回 SQL NULL**
 * （内置软件的 pic0Info 出厂就是 NULL，还原必须还原成 NULL 而不是空串）。 */
int tile_db_set_image_raw(const char *tid, const char *column, const char *value,
                          char *msg, size_t msgsz);

/* 读 tbl_contentinfo 里这一行的这一列。0 = 有这一行（*is_null 告诉是不是 NULL，
 * 非 NULL 时 out 已填），-1 = 没有这一行 / 库打不开 / 列不存在。 */
int tile_db_get_image(const char *tid, const char *column,
                      char *out, size_t osz, int *is_null);

#ifdef __cplusplus
}
#endif

#endif /* TILE_DB_H */
