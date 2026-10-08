#ifndef SAVE_REMOTE_H
#define SAVE_REMOTE_H

#include <stddef.h>
#include <time.h>   /* sr_stat 的 out_mtime 用 time_t；libsmb2 也要求先包含 */

/* [CN-FORK] v0.5.2.142 存档的远程传输层（FTP / HTTP）。
 *
 * ── 三条协议线的来历（**别再把「libcurl 没有 smb」当成「PS5 做不了 SMB」**）──
 * SDK 里那份 libcurl 是 **8.18.0，编了
 *   http https ftp file dict imap ldap pop3 smtp
 * 而**没有 smb**。探测方法的坑：`strings` 默认只输出 ≥4 字符，而 `ftp` / `smb`
 * 只有 3 字符，必须 `strings -n 3`，否则连 `ftp` 都会被判成「不存在」。
 *
 * `.142` 当时据此把 SMB 一档标成「SDK 不支持」—— **那条推论错了**，它把
 * 「libcurl 这条路不通」当成了「PS5 做不了」。2026-10-07 复核后 `.143` 补上：
 *   - **`libsmb2`**（SMB2/3 用户态客户端，sahlberg/libsmb2 **6.2**）是 PS5 SDK
 *     生态的**正式包**：`ps5-payload-dev/pacbrew-repo` 里的 `ps5-payload-libsmb2`；
 *     上游 `ps5-payload-dev/websrv` 就用它（`prospero-pkg-config libsmb2 --libs`）。
 *   - 预编译产物在 `third_party/libsmb2/`（`.a` + 头 + 源码 + `build_ps5.sh`）。
 *     实测代价 **+约 241 KB**（约 +2.2%）。协议覆盖 SMB 2.0.2–3.1.1（含签名与加密）。
 *   - 于是本文件现在是 **ftp / http 走 libcurl，smb 走 libsmb2**。
 *     远端布局三种协议**完全一致**：`<base>/<TID>/<uid>.zip`。
 *
 * 两条 libsmb2 的坑（写进 README 一样的注释里，改代码时会撞上）：
 *   - `libsmb2.h` **不是自包含的**：要先 `#include <time.h>` 与 `<smb2/smb2.h>`，
 *     否则报 `unknown type name 'time_t'` / `'SMB2_GUID_SIZE'` / `'smb2_lease_key'`。
 *   - 它**没有** `smb2_creat`，创建文件是 `smb2_open(path, O_CREAT|O_WRONLY)`；
 *     也**没有** `O_TRUNC`，要覆盖得先写 `.tmp` 再 `smb2_rename`（正好也就是
 *     下面「整份替换可回退」那条在远端侧的实现）。
 *
 * ── 远端布局（推与拉必须逐字一致，否则推上去的东西拉不回来）────────────
 *   <base>/<TID>/<uid>.zip
 * base 由协议决定：
 *   ftp  → ftp://<host>:<port><path>
 *   http → <url>
 *   smb  → smb://<host>[:<port>]/<share>[/<sub>]
 *          （`sub` 与 ftp 的 `path` 同义，都是可选的；空就是共享根 ——
 *           于是三种协议在盘上都是 <…>/<TID>/<uid>.zip，不会叠出两层 TID）
 * 一个 tid 一个目录、一个用户一个文件。**刻意不做「一个游戏一个文件」** ——
 * 真机上同一个游戏常有两个用户各存一份（实测 PPSA01650 就是），挤在一个文件里
 * 必然互相覆盖。这一条与本地那一侧的「一个用户一条」是同一个口径。
 *
 * ── 远端文件名不带时间戳 ─────────────────────────────────────────────────
 * 恒定叫 `<uid>.zip`，每次推送**整份覆盖**。用户的心智是「远程那份就是这个游戏
 * 现在的存档」，不是「一堆带时间戳的归档」。要历史版本有本地快照（`.140`）兜着
 * —— 那正是它存在的理由：远程解决「多机之间搬」，本地解决「手滑了回退」。
 *
 * ── 推与拉的落点都是「整份替换，可回退」 ─────────────────────────────────
 * 拉取走 `game_saves_import_file`，与「上传存档」是**同一条**实现：旧的先改名
 * 留一手，换成功才删。半路失败不会把用户的存档弄没。 */

/* 一款游戏的远程配置。字段与 saves_auto.json 里那个 tid 对象一一对应。 */
typedef struct {
    char mode[8];   /* "ftp" / "http" / "smb" */
    /* ftp / smb 共用 */
    char host[256];
    int  port;      /* 缺省：ftp 21、smb 445 */
    char path[512]; /* ftp：远端目录，可为空（那就是根） */
    char user[128];
    char password[128];
    /* http */
    char url[512];
    char token[256];
    /* smb。刻意与 ftp 的 path 分开：共享名是 SMB 协议里 tree connect 的那一层，
     * 与「目录路径」不是一回事，混用一个字段迟早拼错。 */
    char share[256];
    char sub[256];  /* 共享名下的可选子目录（与 ftp 的 path 同义）；空即共享根 */
} sr_cfg_t;

/* 这一版支持哪些协议。ftp / http 走 libcurl，smb 走 libsmb2（见文件头）。 */
int sr_mode_supported(const char *mode);

/* sr_pull 的专用返回码：**远端还没有这个文件**。
 * 与「失败」分开是因为语义完全不同 —— 第一次用自动存档时远端当然是空的，
 * 那是「没得拉」而不是「拉失败」，混在一起会让用户第一次开游戏就看到一条红色报错。
 * （FTP 那边 curl 回 CURLE_REMOTE_FILE_NOT_FOUND、HTTP 那边回 404，两条都归到它。） */
#define SR_ENOTFOUND (-2)

/* 把配置解析成**实际要打的 URL**（不含密码，可以直接进日志）。
 * 0 成功；-1 = 协议不支持 / 配置不全（err 回一句人话）。
 * 单独抽出来是为了让宿主测试能把 URL 拼装逐条钉死 —— 拼错一个斜杠，
 * 推上去的文件和拉下来的路径就不是同一个，而且两边都不报错。 */
int sr_build_url(const sr_cfg_t *c, const char *tid, const char *uid,
                 char *out, size_t osz, char *err, size_t errsz);

/* 推送：把主机上这个用户的存档打包后传到远端。0 成功，-1 失败（err 人话）。
 * 自己打包（game_saves_pack）与清理临时文件，调用方只给 tid/uid。
 * *out_bytes 回实际传出去的字节数（可为 NULL）。 */
int sr_push(const char *tid, const char *uid, const sr_cfg_t *c,
            long long *out_bytes, char *err, size_t errsz);

/* 拉取：从远端取回整份存档并落到主机（整份替换、可回退）。
 * 0 成功，-1 失败（err 人话）。远端没有这个文件时 err 里是「远程还没有…」。
 * *out_files / *out_bytes 回写进主机的量（可为 NULL）。 */
int sr_pull(const char *tid, const char *uid, const sr_cfg_t *c,
            long long *out_files, long long *out_bytes, char *err, size_t errsz);

/* [CN-FORK] v0.5.2.145 **只读**地取一份远程存档的真实元数据，供前端「远程游戏」
 * 标签展示与本地存档列表同款的信息（大小 / 修改时间 / 文件数）。
 *   out_size  ← <uid>.zip 的字节数（smb2_stat / 协议 stat 直接拿，真实）
 *   out_mtime ← <uid>.zip 的 mtime（真实打包时间，smb2_stat 拿）
 *   out_files ← 推送时一并写的 <uid>.meta.json 里的 files；老存档没有 meta 时回 -1
 * 0 成功；SR_ENOTFOUND = 远端还没有这份（语义同 sr_pull，前端当「没得显示」）；
 * -1 = 配置 / 连接错误（err 人话）。FTP / HTTP 暂未实现，统一回 -1 + 说明。 */
int sr_stat(const sr_cfg_t *c, const char *tid, const char *uid,
            long long *out_size, time_t *out_mtime, long long *out_files,
            char *err, size_t errsz);

#endif
