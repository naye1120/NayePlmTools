#pragma once
/* [CN-FORK] v0.5.2.164 游戏磁贴的「收藏 / 排序」—— 面板侧的列表偏好。
 *
 * 用户口径（两条）：
 *   「每个磁贴增加 收藏 和 排序 和 置顶 按钮，收藏的游戏自动排到列表前面」
 *   「置顶按钮是指 ps5 中固定在最前边」
 *
 * ── [CN-FORK] v0.5.2.166 置顶搬走了 ────────────────────────────────────────
 * `.164` 这一版把置顶也落在这里（`pin` 字段），理由是「写主机 tbl_iconinfo 的
 * pinnedStatus 未必有用」。用户的反馈很直接：**「置顶按钮在 PS5 里没有生效」** ——
 * 因为写我们自己的 JSON 主机根本不知道，这不是「未必有用」，是**一定没用**。
 *
 * `.166` 找到了真正的载体：`app.db` 的 `tbl_group_<uid>`（groupName='MyHome'，
 * idx = 主屏位置），真机取证见 include/tile_group.h。置顶/排序的主机那一半现在由
 * `src/tile_group.c` 负责，本文件**只留面板侧的两件事**：
 *   ① `fav`（收藏）—— 面板自己的排序权重，主机上没有对应概念；
 *   ② `order`（手工顺序）—— 拖拽排序在面板里排出来的先后。
 *
 * `pin` 字段**保留但已废弃**（不再有调用方写它，前端也从 /tile_group 读真身）：
 * 删掉它会牵动 `.164` 的宿主测试与 mock 契约，而留着它不会有第二个真相源 ——
 * 前端根本不读这个键了。下次大改时一并清掉。
 *
 * ── 存储 ──────────────────────────────────────────────────────────────────
 * /data/nayeplmtools/game_prefs.json
 *   { "PPSA01234": {"fav":true,"pin":false}, ...,
 *     "order": ["PPSA01234","CUSA00002", ...] }
 * 只有 fav / pin 至少一个为 true 的条目才留在文件里（关掉就删条目，文件不会无限长）。
 *
 * fav / pin 入参是**三态**：-1 = 不动这一项，0/1 = 写成这个值。
 * 前端每次只拨一颗按钮，另一项必须原样保留 —— 两态入参会把另一项抹掉。 */

#include <stddef.h>

#ifndef GAME_PREFS_PATH
#define GAME_PREFS_PATH "/data/nayeplmtools/game_prefs.json"
#endif

/* 写出 {"prefs":{"<TID>":{"fav":bool,"pin":bool}, ...},"order":["<TID>", ...]}
 * （空文件 → {"prefs":{},"order":[]}）。返回写入字节数，<0 = 缓冲不够 / 读失败。 */
int game_prefs_list_json(char *out, size_t size);

/* 改一个 tid 的 fav / pin（各为 -1/0/1，见文件头）。0 成功，-1 失败（msg 写人话）。
 * out_fav / out_pin（可 NULL）回**改完之后**的最终状态 —— 只拨一颗按钮时另一项是
 * 「不动」，把入参原样回给前端会谎报（-1 会被当成 false）。 */
int game_prefs_set(const char *tid, int fav, int pin,
                   int *out_fav, int *out_pin, char *msg, size_t msgsz);

/* [CN-FORK] v0.5.2.166 覆盖面板侧的手工顺序（拖拽排序落地的第一半）。
 * 传进来的就是列表的完整先后（前端按当前渲染顺序整份发），**逐条校验** tid，
 * 一条不合法整份拒收 —— 半个顺序比没有顺序更难查。空数组 = 清掉手工顺序
 * （回到「置顶 → 收藏 → 档位」的默认排序）。
 * 0 = 成功；-1 = 参数不合法或写失败。 */
int game_prefs_set_order(const char *const *tids, int n);
