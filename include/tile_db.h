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

#ifdef __cplusplus
}
#endif

#endif /* TILE_DB_H */
