#pragma once
/* [CN-FORK] v0.5.2.158 「个性化软件」—— PS5 内置软件的显示 / 隐藏。
 *
 * 真机 app.db 取证（_ftp_probe/app.db 那份原件）：主屏每张磁贴在**每个账号自己的**
 * tbl_iconinfo_<uid> 里有一行，其中 `visible` 列就是「这张磁贴画不画出来」——
 *   游戏（PPSA 前缀）与自制软件（NAYE、WKAL 前缀）恒 1；
 *   索尼内置应用里 媒体相册 / PlayStation Store / 所有应用程序 / 两个内容保存库 = 1，
 *   而 PlayStation Plus / 探索 / Music Home / Remote Play / 分享游玩 / Game Streaming /
 *   Disc Player / 欢迎 / 编辑 / 光碟类 = 0（系统自己就没把它们摆上主屏）。
 * 所以本模块只做一件事：按 titleId 把这一列写成 0 或 1。
 *
 * 刻意**不**新增配置键：状态就是 app.db 里那一列，再抄一份到
 * nayeplmtools_config.txt 只会多出「两边不一致时听谁的」这个问题。
 *
 * 范围只认 NPXS 前缀（索尼内置应用）。自制软件与游戏一律拒写 —— 否则一个构造
 * 出来的请求能把 NayePlmTools 自己的磁贴藏掉，而恢复入口就在那块磁贴上。 */

#ifndef APP_VISIBILITY_H
#define APP_VISIBILITY_H

#include <stddef.h>

/* 与 game_mgr.h / tile_db.h 一样是**同一个文件**，各自持宏是为了让宿主测试能用
 * -DAPP_VIS_APPDB_PATH 指到临时副本上（真机原件不能被测试写坏）。 */
#ifndef APP_VIS_APPDB_PATH
#define APP_VIS_APPDB_PATH "/system_data/priv/mms/app.db"
#endif

/* 内置软件的 titleId 前缀，也是本模块唯一的写入白名单。 */
#define APP_VIS_PREFIX "NPXS"

/* GET /system_apps 的响应体：
 *   {"apps":[{"tid":"NPXS40037","name":"PlayStation Plus","hidden":true}, ...],
 *    "count":19,"accounts":1,"error":""}
 * 按 name 的拼音/字典序排不好做（中文标题），所以按 titleId 升序 —— 稳定且免排序。
 * 库打不开时回 {"apps":[],"count":0,"error":"…"}（不是空数组冒充成功）。
 * 返回写入 out 的字节数（不含 '\0'），缓冲不够回 -1。 */
int app_vis_list_json(char *out, size_t size);

/* 写一行的 visible。hidden=1 隐藏、0 显示。
 * 返回 0 = 至少改了一张账号表（或本来就一致）；
 *      -1 = 参数不合法 / tid 不是内置软件 / 库打不开 / 一行都没匹配到；
 *      -2 = 尝试写了但 SQL 失败。
 * msg 给人话（成功也可能留空）。首次写入前会走 tile_db 那份「原始备份只留一次」。 */
int app_vis_set(const char *tid, int hidden, char *msg, size_t msgsz);

#endif /* APP_VISIBILITY_H */
