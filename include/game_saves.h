#ifndef GAME_SAVES_H
#define GAME_SAVES_H

/* [CN-FORK] v0.5.2.116 游戏管理 → 存档后端
 * 契约定死在 frontend/src/utils/gameApi.js 文件头（那一轮「只做前端 + mock」）：
 *
 *   GET  /game_saves?tid=        → {tid, local_path, auto, saves:[...]}
 *   POST /game_save_auto         → {ok}      （存配置，密码永不回传）
 *   POST /game_save_test         → {ok, latency_ms} | 400 EHOSTUNREACH
 *   GET  /game_save_download?tid=&save= → zip 二进制
 *   POST /game_save_upload?tid=&save=   （裸 body：zip/tar/gzip/单文件）→ {ok, files, size}
 *   GET  /game_save_delete?tid=&save=   → {ok}
 *
 * ── 存档落在哪 ────────────────────────────────────────────────────────────
 * 真机路径是 /user/home/<uid>/savedata/<TID>/<槽>/，UID 没有公开的「当前用户」API，
 * 所以这里枚举 /user/home 下所有目录去找，**取总字节数最大的那个命中的** ——
 * 一台主机常有多个用户目录，其中一个空的先被扫到就全成了「这个存档 0 字节」。
 *
 * ── 槽是「顶层子项」而不是某种固定格式 ────────────────────────────────────
 * PS4 的存档是一个文件 sdimg_<TID>，PS5 的是一整个目录（里面带 sce_sys/param.sfo），
 * 中间还可能夹着别的形态。这里不去猜真机的内部布局：**一级子项即一个槽**，
 * size/files 递归统计、名字优先读 SFO 的 SAVEDATA_TITLE，读不到就用子项名。
 * 这样无论真机是哪种布局都能列出、能下、能传、能删，不会因为「没见过这种结构」直接空手。
 *
 * ── 传输格式 ──────────────────────────────────────────────────────────────
 * 下载 = 自研 STORE zip（src/zip_io.c）。上传的文件选择器 accept 是
 * ".zip,.tar,.gz,.tgz"，四种都得吃：PK 头走 zip、1f 8b 走 gunzip（之后若是 tar 再解 tar，
 * 否则当单文件收下）、ustar 头直接解 tar、都不是就整包存成一个文件。
 * 只认 zip 的话，用户在 Windows 上右键压的 tar.gz 就传不进来。
 */

#include <stddef.h>

/* 自动备份配置的持久化位置（和 autoload/quicklinks 一样放 /data 下的自有目录）。 */
#ifndef GAME_SAVES_AUTO_PATH
#define GAME_SAVES_AUTO_PATH "/data/nayeplmtools/saves_auto.json"
#endif

/* 下载时临时 zip 的落点。**不能放在存档目录里** —— 边下边把自己的源文件夹写脏了。 */
#ifndef GAME_SAVES_PACK_DIR
#define GAME_SAVES_PACK_DIR "/data/nayeplmtools/saves_packs"
#endif

/* 一次存档能装多少个文件 / 多大：防一个畸形包把主机拖死。 */
#define GAME_SAVES_MAX_ENTRIES 8192u
#define GAME_SAVES_MAX_BYTES (512ull << 20)
#define GAME_SAVES_MAX_TAR_FILE (256ull << 20)

/* 找这个 tid 的存档根目录。<uid>/savedata/<TID>，取体积最大的命中。
 * 返回 0 有（out 已填），-1 找不到（主机上一个存档都还没生成时是正常的）。 */
int game_saves_root(const char *tid, char *out, size_t osz);

/* GET /game_saves：整份响应体（含 auto 与 saves）写进 buf。返回长度，<0 失败。 */
int game_saves_list_json(const char *tid, char *buf, size_t size);

/* /game_detail 要的存档条数（没有这个 tid 的存档 → 0）。 */
int game_saves_count(const char *tid);

/* POST /game_save_auto：body 是 JSON。0 成功（msg 给人话），-1 参数非法。 */
int game_saves_auto_set(const char *tid, const char *body, size_t len,
                        char *msg, size_t msgsz);

/* POST /game_save_test：body 是 JSON，**真的去连一次**（TCP connect，
 * ftp 还看 220 横幅、http 再发一个 HEAD 看状态行）。
 * 0 → buf 写 {"ok":true,"latency_ms":N,"note":"…"}；-1 → 调用方回 400 EHOSTUNREACH。 */
int game_saves_test(const char *body, size_t len, char *buf, size_t size);

/* GET /game_save_download：把 <root>/<save> 打成 STORE zip 写到 out_path。
 * 0 成功（调用方负责流完之后 unlink）。 */
int game_saves_pack(const char *tid, const char *save, const char *out_path,
                    char *msg, size_t msgsz);

/* POST /game_save_upload：body 是用户挑的那个文件原样字节。
 * 成功 0，*out_files = 写入的文件数，*out_size = 写入的总字节。 */
int game_saves_upload(const char *tid, const char *save,
                      const unsigned char *body, size_t len,
                      long long *out_files, long long *out_size,
                      char *msg, size_t msgsz);

/* GET /game_save_delete：删掉整个槽。0 成功。 */
int game_saves_remove(const char *tid, const char *save, char *msg, size_t msgsz);

#endif
