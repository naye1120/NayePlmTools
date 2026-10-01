#ifndef GAME_MGR_H
#define GAME_MGR_H

#include <stddef.h>

/* [CN-FORK] 游戏管理模块。参考 ShadowMountPlus 的「游戏识别」思路，并移植 v0.5.1.63
 * stats.c 里那套成熟的 app.db(SQLite) 只读解析器与 param.sfo 解析，实现：
 *   ① 已安装   —— 枚举 /system_data/priv/mms/app.db 的 tbl_contentinfo（PPSA/CUSA/… 游戏本体）
 *   ② 已存在   —— 扫描 ShadowMount 式 scan paths（/data、/mnt/ext*、/mnt/usb*、/mnt/shadowmnt）
 *                  里含 sce_sys/param.sfo 的 <TITLE_ID> 目录
 * 合并去重（按 TITLE_ID，已安装优先）。
 *
 * 重要：进页面一次性拉取 + mutex 串行 + 60s 缓存，绝不轮询。app.db 全库读取 + 目录扫描
 * 是有成本的 IO，频繁调用同样有风险（参考 /system_stats 死机教训）。 */

/* 输出 {"count":N,"games":[{"tid":"PPSAxxxxx","name":"…","src":"installed|dump"},...]}。
 * name 经 JSON 转义（含中文）。返回写入长度。 */
int game_mgr_list_json(char *buf, size_t size);

/* 解析某 titleId 的图标文件绝对路径写入 out（icon0.png）。成功返回 1，找不到返回 0。
 * 候选顺序：/user/app/<TID>/sce_sys → 该 TID 沙盒 app0/sce_sys → /user/appmeta/<TID> →
 * /system_data/priv/appmeta/<TID>。供 /game_icon?tid= 端点读取。tid 必须是 9 位 alnum。 */
int game_mgr_icon_path(const char *tid, char *out, size_t osz);

#endif
