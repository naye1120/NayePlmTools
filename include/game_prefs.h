#pragma once
/* [CN-FORK] v0.5.2.164 游戏磁贴的「收藏 / 置顶」—— 面板侧的列表排序偏好。
 *
 * 用户口径（两条）：
 *   「每个磁贴增加 收藏 和 排序 和 置顶 按钮，收藏的游戏自动排到列表前面」
 *   「置顶按钮是指 ps5 中固定在最前边」
 *
 * ── 为什么落本面板的 JSON，而不是写主机 app.db ─────────────────────────────
 * 置顶有两种可能的目标：① 面板自己的游戏列表排序；② PS5 主屏的磁贴顺序。
 * ② 要写 `tbl_iconinfo_<uid>` 的 `pinnedStatus` —— 而那个列**在真机上没被验证过**：
 * include/app_visibility.h 记录的那次取证（192.168.1.214，10-10）里，固定条目与
 * 普通条目在这一列上**没有一致差异**，也就是说写了也未必有用。写一个「未必有用、
 * 却回一句成功」的开关，正是这个仓库反复栽过的「谎报」（`.158` 的 visible 就是）。
 * 所以这一版**只做 ①**：置顶 = 在**本面板的列表里**固定到最前，收藏次之。
 * 要上 ②，先在真机上做一次「写了 pinnedStatus、看主屏」的实验，再谈。
 *
 * ── 存储 ──────────────────────────────────────────────────────────────────
 * /data/nayeplmtools/game_prefs.json
 *   { "PPSA01234": {"fav":true,"pin":false}, ... }
 * 只有 fav / pin 至少一个为 true 的条目才留在文件里（关掉就删条目，文件不会无限长）。
 *
 * fav / pin 入参是**三态**：-1 = 不动这一项，0/1 = 写成这个值。
 * 前端每次只拨一颗按钮，另一项必须原样保留 —— 两态入参会把另一项抹掉。 */

#include <stddef.h>

#ifndef GAME_PREFS_PATH
#define GAME_PREFS_PATH "/data/nayeplmtools/game_prefs.json"
#endif

/* 写出 {"prefs":{"<TID>":{"fav":bool,"pin":bool}, ...}}（空文件 → {"prefs":{}}）。
 * 返回写入字节数，<0 = 缓冲不够 / 读失败。 */
int game_prefs_list_json(char *out, size_t size);

/* 改一个 tid 的 fav / pin（各为 -1/0/1，见文件头）。0 成功，-1 失败（msg 写人话）。
 * out_fav / out_pin（可 NULL）回**改完之后**的最终状态 —— 只拨一颗按钮时另一项是
 * 「不动」，把入参原样回给前端会谎报（-1 会被当成 false）。 */
int game_prefs_set(const char *tid, int fav, int pin,
                   int *out_fav, int *out_pin, char *msg, size_t msgsz);
