#pragma once

/* [CN-FORK] v0.5.2.163 背景音乐接口契约。落点 = /data/nayeplmtools/music/<TID>.<ext>。 */

/* ⚠ 头文件必须自包含：`size_t` 来自这里，别指望包含者先引过。
 * （`.165` 出包时踩到 —— `http_server.c` 先于本头用到 `size_t`，编译期直接
 *  「call to undeclared function」，而且报错点全在调用方，很容易看成别的问题。） */
#include <stddef.h>

/* 把 (tid) 对应的当前生效音乐路径写到 tp，mime 写到 mime。
 * 0 = 找到了；1 = 没设过（tp / mime 会被清空成空串）。 */
int game_music_path(const char *tid, char *tp, size_t tpsz,
                    char *mime, size_t mimesz);

/* 写入 / 替换音乐。bytes_out = 实际写入字节数（成功才有意义）。
 * 0 = 成功；-1 = 校验（mib 上限 / 空文件）；1 = IO 失败。msg 写人话。 */
int game_music_set(const char *tid, const char *name,
                   const unsigned char *data, size_t len,
                   long long *bytes_out, char *msg, size_t msgsz);

/* 删掉主机上已有的（不存在也算成功，调用方不需要区分）。 */
void game_music_reset(const char *tid);