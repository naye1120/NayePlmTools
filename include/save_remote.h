#ifndef SAVE_REMOTE_H
#define SAVE_REMOTE_H

#include <stddef.h>

/* [CN-FORK] v0.5.2.142 存档的远程传输层（FTP / HTTP）。
 *
 * ── 为什么只有这两种协议 ─────────────────────────────────────────────────
 * SDK 里的 libcurl 是 **8.18.0，编了
 *   http https ftp file dict imap ldap pop3 smtp
 * 而**没有 smb**（`lib/smb.c` 是空壳、没有 libsmb2；`target/include/` 下只有
 * `fs/smbfs`、`netsmb` 那套**内核侧**头文件，没有用户态库）。
 * 所以「自动存档」的远程只能 FTP + HTTP —— UI 上那个 SMB 选项标成
 * 「当前 SDK 不支持」，别让用户填完一整套共享名再发现它永远连不上。
 * 探测方法的坑记在这里：`strings` 默认只输出 ≥4 字符，而 `ftp` / `smb` 只有
 * 3 字符 —— 必须 `strings -n 3`，否则连 `ftp` 都会被判成「不存在」。
 *
 * ── 远端布局（推与拉必须逐字一致，否则推上去的东西拉不回来）────────────
 *   <base>/<TID>/<uid>.zip
 * base 由协议决定：
 *   ftp  → ftp://<host>:<port><path>
 *   http → <url>
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
    /* ftp */
    char host[256];
    int  port;      /* 缺省 21 */
    char path[512]; /* 远端目录，可为空（那就是根） */
    char user[128];
    char password[128];
    /* http */
    char url[512];
    char token[256];
} sr_cfg_t;

/* 这一版支持哪些协议。现在只有 ftp / http —— smb 一律 0。 */
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

#endif
