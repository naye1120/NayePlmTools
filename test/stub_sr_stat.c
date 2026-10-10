/* [CN-FORK] v0.5.2.147 宿主测试里 sr_stat 的替身。
 *
 * 为什么需要它：`.146` 给 game_saves.c 加了 `game_saves_remote_info()`，它调
 * `sr_stat()`（真身住在 src/save_remote.c，要 curl / libsmb2）。而 `game_saves`
 * 这个关卡是**纯文件系统**那一路 —— 编译行刻意只带
 * game_job / json_helpers / utils / inflate / zip_io / cJSON，不带 save_remote.c，
 * 于是链接时 `undefined reference to 'sr_stat'`，整个关卡直接编不出来。
 *
 * 为什么是「补替身」而不是「把 save_remote.c + -lcurl 加进这一关」：
 *   ① 这一关现在**只依赖 python3**（造夹具），链上 curl 之后就会变成
 *      「没有 libcurl 头/库的机器整段 SKIPPED」—— 白白丢掉一层覆盖；
 *   ② 本关**没有任何用例碰远程元数据**（它测的是本地存档的枚举 / 打包 / 快照），
 *      真身进来一行也跑不到。
 *   ③ 同族的 `test/stub_archive_global.c` 就是这么处理 config_read_archive_global 的，
 *      口径一致。
 *
 * ⚠ 这个替身**故意不模拟任何成功路径**：一律回 -1 + 一句人话，并往 stderr 打一行提示。
 *   本关不调它 → 平时一行输出都没有；哪天有人在本关加了远程用例，会立刻在 stderr 看到
 *   「替身被调用了」，而不是拿到一份看起来正常的假数据。 */

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "save_remote.h"

int sr_stat(const sr_cfg_t *c, const char *tid, const char *uid,
            long long *out_size, time_t *out_mtime, long long *out_files,
            long long *out_src_mtime, char *err, size_t errsz) {
    (void)c;
    (void)tid;
    (void)uid;
    if (out_size) *out_size = 0;
    if (out_mtime) *out_mtime = 0;
    if (out_files) *out_files = -1;
    /* [CN-FORK] v0.5.2.162 「说不清」的那个值就留在 0，与真身缺字段时同一口径。 */
    if (out_src_mtime) *out_src_mtime = 0;

    static int warned = 0;
    if (!warned) {
        warned = 1;
        fprintf(stderr,
                "[stub_sr_stat] sr_stat() 被调用了 —— 本关卡的替身不实现远程元数据。\n"
                "               若你刚给 game_saves 关卡加了远程用例，请把真正的\n"
                "               src/save_remote.c src/curl_init.c + -lcurl 加进它的编译行。\n");
    }

    if (err && errsz) {
        const char *m = "宿主测试替身：game_saves 关卡不实现远程元数据";
        size_t n = strlen(m);
        if (n > errsz - 1) n = errsz - 1;
        memcpy(err, m, n);
        err[n] = 0;
    }
    return -1;
}

/* [CN-FORK] v0.5.2.159 同上：`game_saves_remote_delete()` 调 `sr_delete()`，
 * 这一关的编译行没有 src/save_remote.c，不补替身就链接不过。
 * 口径与 sr_stat 一致：**绝不回 0、绝不回 SR_ENOTFOUND**（那两条在调用方都算
 * 「删掉了」，替身返回它们等于帮测试造假）。 */
int sr_delete(const sr_cfg_t *c, const char *tid, const char *uid,
              char *err, size_t errsz) {
    (void)c;
    (void)tid;
    (void)uid;

    static int warned = 0;
    if (!warned) {
        warned = 1;
        fprintf(stderr,
                "[stub_sr_stat] sr_delete() 被调用了 —— 本关卡的替身不删除任何东西。\n"
                "               若你刚给 game_saves 关卡加了远程删除用例，请把真正的\n"
                "               src/save_remote.c src/curl_init.c + -lcurl 加进它的编译行。\n");
    }

    if (err && errsz) {
        const char *m = "宿主测试替身：game_saves 关卡不实现远程删除";
        size_t n = strlen(m);
        if (n > errsz - 1) n = errsz - 1;
        memcpy(err, m, n);
        err[n] = 0;
    }
    return -1;
}
