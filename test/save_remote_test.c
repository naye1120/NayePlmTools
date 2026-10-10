/* [CN-FORK] v0.5.2.142 test/save_remote_test.c — src/save_remote.c 的主机回归测试。
 *
 * 覆盖：
 *   ① 协议白名单：只有 ftp / http，smb 一律拒（SDK 的 curl 没编 SMB）
 *   ② URL 拼装：前后斜杠归一、空路径、tid/uid 的路径穿越一律拒
 *   ③ HTTP 端到端：推上去的文件真的落在对面，删掉本地再拉回来内容一致
 *   ④ FTP 端到端：同上，且**远端目录一开始不存在**（考验 create-missing-dirs）
 *   ⑤ 远端还没有这个文件时回 SR_ENOTFOUND（不是失败 —— 第一次用必然遇到）
 *   ⑥ 配置不全 / 主机没填时的报错是人话
 *
 * 为什么值得写两个桩服务（test/stub_servers.c）：这条链上最容易错的地方
 * （目录不存在要不要自己建、EPSV 还是 PASV、USER/PASS 有没有真带上）全在
 * 协议交互里 —— 只看函数返回值是测不出来的。
 *
 * 用法：由 test/run_host_tests.sh 拨起，也可单独跑。
 */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "game_saves.h"
#include "save_remote.h"
#include "stub_servers.h"

/* log_server.c 的头要 microhttpd.h，主机上没有 —— 给个等价实现，默认静音。 */
#include <stdarg.h>
void pldmgr_log(const char *fmt, ...) {
    if (!getenv("SAVES_TEST_VERBOSE")) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

#define ROOT "/tmp/pldmgr_sr_test"
#define HOME ROOT "/home"
#define TID  "PPSA012345"
#define UID  "00000002"

static int g_checks, g_fails;

#define CHECK(cond, ...)                                                     \
    do {                                                                     \
        g_checks++;                                                          \
        if (!(cond)) {                                                       \
            g_fails++;                                                       \
            fprintf(stderr, "  FAIL L%d: ", __LINE__);                       \
            fprintf(stderr, __VA_ARGS__);                                    \
            fprintf(stderr, "\n");                                           \
        }                                                                    \
    } while (0)

static void mkdir_p(const char *p) {
    char b[1200];
    snprintf(b, sizeof(b), "%s", p);
    for (char *s = b + 1; *s; s++) {
        if (*s != '/') continue;
        *s = 0;
        mkdir(b, 0755);
        *s = '/';
    }
    mkdir(b, 0755);
}

static void put(const char *path, int n, char fill) {
    char parent[1200];
    snprintf(parent, sizeof(parent), "%s", path);
    char *sl = strrchr(parent, '/');
    if (sl) { *sl = 0; mkdir_p(parent); }
    FILE *f = fopen(path, "wb");
    if (!f) return;
    for (int i = 0; i < n; i++) fputc(fill, f);
    fclose(f);
}

static long rd(const char *path, char *out, size_t cap) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    size_t n = fread(out, 1, cap - 1, f);
    fclose(f);
    out[n] = 0;
    return (long)n;
}

static int exists(const char *p) {
    struct stat st;
    return stat(p, &st) == 0;
}

static void rm_rf(const char *p) {
    char cmd[1400];
    /* 只删本测试自己在 /tmp 下造的目录，路径是编译期常量拼出来的。 */
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", p);
    if (system(cmd) != 0) { /* 清不掉也不影响断言，下面会自己覆盖 */ }
}

#define SAVE_DIR HOME "/" UID "/savedata_prospero/" TID
#define SAVE_FILE SAVE_DIR "/sdimg_TEST"

static void fixture_reset(void) {
    rm_rf(ROOT);
    put(SAVE_FILE, 3000, 'A');
    put(SAVE_DIR "/sdimg_sce_bu_TEST", 500, 'B');
}

/* ══ ① 协议白名单 ═══════════════════════════════════════════════════════ */
static void t_mode(void) {
    printf("-- 协议白名单\n");
    CHECK(sr_mode_supported("ftp") == 1, "ftp 应该支持");
    CHECK(sr_mode_supported("http") == 1, "http 应该支持");
    /* [CN-FORK] v0.5.2.143 smb 从「不支持」翻成「支持」：走 libsmb2，不是 libcurl。
     * .142 那条断言（== 0）是把「libcurl 没编 smb」当成了「PS5 做不了 SMB」。 */
    CHECK(sr_mode_supported("smb") == 1, "smb 应该支持（libsmb2）");
    CHECK(sr_mode_supported("") == 0, "空串不支持");
    CHECK(sr_mode_supported(NULL) == 0, "NULL 不支持");
    CHECK(sr_mode_supported("nfs") == 0, "没实现的协议不支持");
}

/* ══ ② URL 拼装 ═════════════════════════════════════════════════════════ */
static void t_url(void) {
    printf("-- URL 拼装\n");
    char url[1200], err[256];

    sr_cfg_t c;
    memset(&c, 0, sizeof(c));
    snprintf(c.mode, sizeof(c.mode), "ftp");
    snprintf(c.host, sizeof(c.host), "192.168.1.10");
    c.port = 2121;
    snprintf(c.path, sizeof(c.path), "/ps5save");

    CHECK(sr_build_url(&c, TID, UID, url, sizeof(url), err, sizeof(err)) == 0,
          "ftp 基础拼装失败：%s", err);
    CHECK(!strcmp(url, "ftp://192.168.1.10:2121/ps5save/" TID "/" UID ".zip"),
          "ftp URL 不对：%s", url);

    /* 前后斜杠归一：不带前导、带尾随斜杠必须拼出同一个 URL。
     * 归一漏了的话，推上去的位置和拉下来的位置就不是同一个，而且两边都不报错。 */
    snprintf(c.path, sizeof(c.path), "ps5save/");
    CHECK(sr_build_url(&c, TID, UID, url, sizeof(url), err, sizeof(err)) == 0,
          "ftp 归一拼装失败：%s", err);
    CHECK(!strcmp(url, "ftp://192.168.1.10:2121/ps5save/" TID "/" UID ".zip"),
          "前后斜杠没归一：%s", url);

    snprintf(c.path, sizeof(c.path), "///ps5save//");
    CHECK(sr_build_url(&c, TID, UID, url, sizeof(url), err, sizeof(err)) == 0,
          "ftp 多斜杠拼装失败：%s", err);
    CHECK(!strcmp(url, "ftp://192.168.1.10:2121/ps5save/" TID "/" UID ".zip"),
          "多斜杠没归一：%s", url);

    c.path[0] = 0;
    c.port = 0; /* 顺便验「没填端口时回落 21」 */
    CHECK(sr_build_url(&c, TID, UID, url, sizeof(url), err, sizeof(err)) == 0,
          "ftp 空路径拼装失败：%s", err);
    CHECK(!strcmp(url, "ftp://192.168.1.10:21/" TID "/" UID ".zip"),
          "空路径应该是根 + 默认端口 21：%s", url);

    snprintf(c.path, sizeof(c.path), "/");
    CHECK(sr_build_url(&c, TID, UID, url, sizeof(url), err, sizeof(err)) == 0,
          "ftp 单斜杠路径拼装失败：%s", err);
    CHECK(!strcmp(url, "ftp://192.168.1.10:21/" TID "/" UID ".zip"),
          "单斜杠路径应该等于根：%s", url);

    /* http：尾随斜杠要去掉，否则会拼出 // 两层 */
    memset(&c, 0, sizeof(c));
    snprintf(c.mode, sizeof(c.mode), "http");
    snprintf(c.url, sizeof(c.url), "http://192.168.1.10:8099/save/");
    CHECK(sr_build_url(&c, TID, UID, url, sizeof(url), err, sizeof(err)) == 0,
          "http 拼装失败：%s", err);
    CHECK(!strcmp(url, "http://192.168.1.10:8099/save/" TID "/" UID ".zip"),
          "http URL 不对：%s", url);

    /* 拒绝的几类 */
    snprintf(c.url, sizeof(c.url), "192.168.1.10:8099/save");
    CHECK(sr_build_url(&c, TID, UID, url, sizeof(url), err, sizeof(err)) != 0,
          "没写 scheme 的 http 地址应该被拒");
    snprintf(c.url, sizeof(c.url), "");
    CHECK(sr_build_url(&c, TID, UID, url, sizeof(url), err, sizeof(err)) != 0,
          "空 http 地址应该被拒");

    /* [CN-FORK] v0.5.2.143 smb 走 libsmb2，与其它两档同构：<base>/<TID>/<uid>.zip,
     * base = smb://<host>[:<port>]/<share>[/<sub>]。sub 空 = 共享根（**不会**叠两层 TID）。 */
    memset(&c, 0, sizeof(c));
    snprintf(c.mode, sizeof(c.mode), "smb");
    snprintf(c.host, sizeof(c.host), "192.168.1.10");
    snprintf(c.share, sizeof(c.share), "ps5save");
    CHECK(sr_build_url(&c, TID, UID, url, sizeof(url), err, sizeof(err)) == 0,
          "smb 基础拼装失败：%s", err);
    CHECK(!strcmp(url, "smb://192.168.1.10:445/ps5save/" TID "/" UID ".zip"),
          "smb URL 不对（默认端口 445）：%s", url);

    /* 共享名同样要归一：'/ps5save/' 与 'ps5save' 必须拼出同一个 URL */
    snprintf(c.share, sizeof(c.share), "/ps5save/");
    CHECK(sr_build_url(&c, TID, UID, url, sizeof(url), err, sizeof(err)) == 0,
          "smb 归一拼装失败：%s", err);
    CHECK(!strcmp(url, "smb://192.168.1.10:445/ps5save/" TID "/" UID ".zip"),
          "smb 共享名没归一：%s", url);

    /* sub 是可选子目录，与 ftp 的 path 同义 */
    snprintf(c.sub, sizeof(c.sub), "saves/ps5/");
    CHECK(sr_build_url(&c, TID, UID, url, sizeof(url), err, sizeof(err)) == 0,
          "smb 带 sub 拼装失败：%s", err);
    CHECK(!strcmp(url, "smb://192.168.1.10:445/ps5save/saves/ps5/" TID "/" UID ".zip"),
          "smb 带 sub 的 URL 不对：%s", url);
    /* 反斜杠也要认（Windows 用户会照着资源管理器敲）*/
    snprintf(c.sub, sizeof(c.sub), "\\saves\\");
    CHECK(sr_build_url(&c, TID, UID, url, sizeof(url), err, sizeof(err)) == 0,
          "smb 反斜杠 sub 拼装失败：%s", err);
    CHECK(!strcmp(url, "smb://192.168.1.10:445/ps5save/saves/" TID "/" UID ".zip"),
          "smb 反斜杠 sub 没归一：%s", url);

    c.sub[0] = 0;
    c.port = 1445;
    CHECK(sr_build_url(&c, TID, UID, url, sizeof(url), err, sizeof(err)) == 0,
          "smb 自定义端口拼装失败：%s", err);
    CHECK(!strcmp(url, "smb://192.168.1.10:1445/ps5save/" TID "/" UID ".zip"),
          "smb 自定义端口没进去：%s", url);

    /* 拒绝的两类：没填共享名、没填主机 */
    c.port = 0;
    c.share[0] = 0;
    CHECK(sr_build_url(&c, TID, UID, url, sizeof(url), err, sizeof(err)) != 0,
          "smb 没填共享名必须被拒");
    CHECK(strstr(err, "共享名") != NULL, "smb 的拒绝理由要说清是共享名：%s", err);
    snprintf(c.share, sizeof(c.share), "ps5save");
    c.host[0] = 0;
    CHECK(sr_build_url(&c, TID, UID, url, sizeof(url), err, sizeof(err)) != 0,
          "smb 没填主机必须被拒");

    /* 路径穿越：远端路径是拼出来的，这两个是唯一的闸门 */
    memset(&c, 0, sizeof(c));
    snprintf(c.mode, sizeof(c.mode), "ftp");
    snprintf(c.host, sizeof(c.host), "h");
    CHECK(sr_build_url(&c, "../etc", UID, url, sizeof(url), err, sizeof(err)) != 0,
          "带 .. 的 tid 必须被拒");
    CHECK(sr_build_url(&c, TID, "a/b", url, sizeof(url), err, sizeof(err)) != 0,
          "带 / 的 uid 必须被拒");
    CHECK(sr_build_url(&c, "", UID, url, sizeof(url), err, sizeof(err)) != 0,
          "空 tid 必须被拒");
    CHECK(sr_build_url(&c, TID, "", url, sizeof(url), err, sizeof(err)) != 0,
          "空 uid 必须被拒");

    snprintf(c.host, sizeof(c.host), "");
    CHECK(sr_build_url(&c, TID, UID, url, sizeof(url), err, sizeof(err)) != 0,
          "空 ftp 主机必须被拒");
}

/* ══ ③④⑤ 端到端（HTTP / FTP 各跑一遍同一套）═══════════════════════════ */
/* 远端根目录下那条路径：<root>/ps5save/<TID>/<uid>.zip
 * （配置里的 path/url 尾部都带 /ps5save，所以那一段也要算进来 —— 漏了它
 *  断言就会去一个永远不存在的位置找文件，看起来像「推送没生效」）。 */
#define REMOTE_SUB "/ps5save"
static void remote_path(char *out, size_t osz, const char *root) {
    snprintf(out, osz, "%s" REMOTE_SUB "/%s/%s.zip", root, TID, UID);
}

static void t_roundtrip(const char *what, int use_ftp, const char *root) {
    printf("-- %s 端到端（推 → 删本地 → 拉回来）\n", what);
    fixture_reset();
    rm_rf(root);
    mkdir_p(root);

    sr_cfg_t c;
    memset(&c, 0, sizeof(c));
    snprintf(c.mode, sizeof(c.mode), "%s", use_ftp ? "ftp" : "http");
    snprintf(c.host, sizeof(c.host), "127.0.0.1");
    c.port = use_ftp ? stub_ftp_port() : stub_http_port();
    if (use_ftp) snprintf(c.path, sizeof(c.path), "/ps5save");
    else snprintf(c.url, sizeof(c.url), "http://127.0.0.1:%d/ps5save", stub_http_port());

    /* 夹具在推之前的状态（内容 + 体积），拉回来之后要逐字节一样 */
    char want[8192], got[8192];
    long wn = rd(SAVE_FILE, want, sizeof(want));
    long wbn = rd(SAVE_DIR "/sdimg_sce_bu_TEST", want + wn + 1, sizeof(want) - (size_t)wn - 1);
    (void)wbn;
    CHECK(wn > 0, "夹具没造出来");

    char err[256];
    long long bytes = 0;
    err[0] = 0;
    CHECK(sr_push(TID, UID, &c, &bytes, err, sizeof(err)) == 0,
          "%s 推送失败：%s", what, err);
    CHECK(bytes > 0, "%s 推送回来的字节数是 0", what);

    char rp[1200];
    remote_path(rp, sizeof(rp), root);
    CHECK(exists(rp), "%s 远端没收到文件：%s", what, rp);
    long rn = rd(rp, got, sizeof(got));
    CHECK(rn > 0, "%s 远端文件是空的", what);
    /* 自研 zip 是 STORE 的，本地头就在开头 */
    CHECK(rn >= 4 && !memcmp(got, "PK\x03\x04", 4),
          "%s 远端收到的不像 zip（头 %02x %02x）", what,
          (unsigned char)got[0], (unsigned char)got[1]);
    CHECK(!strcmp(rp + strlen(rp) - 4, ".zip"), "%s 远端文件名该是 .zip", what);

    /* ── [CN-FORK] v0.5.2.154 sr_stat 在 FTP / HTTP 上也必须读得到 ─────────
     * 它不再只是「远程游戏」标签的展示数据，而是**拉取前新旧判定**的唯一输入：
     * 读不到就等于那条判定永远「说不清」，用户要的「本地存档更新就不拉」直接失效。
     * 三个协议上的取数方式不同（SMB 直接 stat、FTP/HTTP 读推送时写的 meta.json），
     * 但对外语义必须一致，所以这三条断言两个协议各跑一遍。 */
    {
        long long sz = -1, mfiles = -1, rsrc = -1, lmt = -1;
        time_t mt = 0;
        err[0] = 0;
        CHECK(sr_stat(&c, TID, UID, &sz, &mt, &mfiles, &rsrc, err, sizeof(err)) == 0,
              "%s sr_stat 该能读到刚推上去的元数据：%s", what, err);
        CHECK(sz > 0, "%s sr_stat 的字节数不对：%lld", what, sz);
        CHECK(mt > 0, "%s sr_stat 的 mtime 没读到（%lld）", what, (long long)mt);
        CHECK(mfiles >= 2, "%s sr_stat 的文件数不对：%lld（夹具是 2 个）", what, mfiles);
        /* 时间戳得真能拿来比较：刚推的必须落在「现在」附近 —— 单位（秒 / 毫秒）
         * 或时区错一位，判定就会得出「本地更新」而**永远不拉**，真机上极难看出来。 */
        long long d = (long long)time(NULL) - (long long)mt;
        CHECK(d >= -300 && d <= 300, "%s sr_stat 的 mtime 偏离当前时间 %lld 秒", what, d);

        /* ── [CN-FORK] v0.5.2.162 src_mtime：拉取判定的真正输入 ────────────────
         * 上面那个 `mt` 是**传输落盘的时刻**，拿它比新旧必然说谎：存档是游戏早先
         * 写的，包是刚才推的，于是「刚推上去的这份」永远算「远程更新」，自动拉取
         * 会原样把它拉回来（用户看到的「推完又拉」）。src_mtime 记的是打包那一刻
         * 本地存档的最大 mtime —— 与 game_saves_local_mtime() 同一种量，可直接比。
         * 这里断言的是**两者相等**，比「落在 now 附近」严得多：它证明这个字段真的
         * 来自本地存档，而不是顺手又写了一遍 time(NULL)。 */
        CHECK(game_saves_local_mtime(TID, UID, &lmt) == 1, "%s 本地存档时刻读不到", what);
        CHECK(lmt > 0, "%s 本地存档 mtime 是 0（夹具没造出来？）", what);
        CHECK(rsrc == lmt, "%s src_mtime 该等于本地存档的最大 mtime：远端 %lld / 本地 %lld",
              what, rsrc, lmt);
        /* 单位同一个「秒」：差出几个数量级就是打包时写错了量（毫秒 / 目录 mtime）。 */
        CHECK(rsrc >= lmt - 2 && rsrc <= lmt + 2,
              "%s src_mtime 与本地不同量（%lld vs %lld）", what, rsrc, lmt);

        /* 远端还没有这份 → **回 -1（说不清），不是 SR_ENOTFOUND**。
         *
         * 这是 FTP / HTTP 与 SMB 的**故意差异**，不是漏了：这三档的探测手段不同 ——
         * SMB 能直接 stat <uid>.zip，文件不在就是不在；FTP / HTTP 只能读 meta.json，
         * 而「meta.json 不在」有两种可能：① 远端确实没有这份存档；② 远端有存档但
         * 是 `.145` 之前推的、当时还没有 meta。**从这一侧分不开。**
         *
         * 既然分不开，就必须按**后果更轻**的那边倒：回 SR_ENOTFOUND 会被拉取前的
         * 判定当成「远端没有存档 → 跳过拉取」，于是②那些用户的自动拉取整个失效
         * （明明有存档却再也不拉）；回 -1 只是「说不清 → 照旧拉」，最坏是白拉一次。
         * 代价是 FTP / HTTP 下「远端没有」不会在「远程游戏」标签上显示成「没得显示」，
         * 而是显示成读取失败 —— 两害相权，取这个。 */
        err[0] = 0;
        CHECK(sr_stat(&c, "PPSA99999", UID, &sz, &mt, &mfiles, &rsrc, err, sizeof(err)) == -1,
              "%s 远端没有时 sr_stat 该回 -1（说不清），实得 err=%s", what, err);
        CHECK(err[0] != 0, "%s 读不到时要给人话：%s", what, err);

        /* **meta.json 被删（`.145` 之前推的老存档）→ 必须回 -1（说不清）**，
         * 绝不能回 SR_ENOTFOUND —— 那会被拉取前判定当成「远端没有存档」而跳过拉取，
         * 老存档用户的自动拉取会整个失效，而他们恰恰是最需要拉取的人。 */
        char mp[1200];
        snprintf(mp, sizeof(mp), "%.*s.meta.json", (int)(strlen(rp) - 4), rp);
        CHECK(exists(mp), "%s 推送该在远端一并写下 meta.json：%s", what, mp);
        unlink(mp);
        err[0] = 0;
        int st2 = sr_stat(&c, TID, UID, &sz, &mt, &mfiles, &rsrc, err, sizeof(err));
        CHECK(st2 == -1, "%s 没有 meta.json 时必须回 -1（说不清），实得 %d", what, st2);

        /* [CN-FORK] v0.5.2.162 老包（meta 里**没有** src_mtime）→ sr_stat 回 0，
         * 但把 src_mtime 留在 0。判定侧据此退回 `.154` 口径；这里要是把它读成
         * 「和某个真实时刻相等」，老存档就会被判成「一模一样」而再也不拉。 */
        {
            FILE *mf = fopen(mp, "wb");
            if (mf) {
                fprintf(mf, "{\"files\":2,\"size\":%lld,\"mtime\":%lld}", sz, (long long)mt);
                fclose(mf);
            }
            long long rsrc2 = -1;
            err[0] = 0;
            int st3 = sr_stat(&c, TID, UID, &sz, &mt, &mfiles, &rsrc2, err, sizeof(err));
            CHECK(st3 == 0, "%s 老格式 meta（没有 src_mtime）该照常读到：%s", what, err);
            CHECK(rsrc2 == 0, "%s 缺 src_mtime 字段该回 0（说不清），实得 %lld", what, rsrc2);
        }
    }

    /* 删掉本地整份，再拉回来 —— 这才是「推拉真的搬了字节」的证明 */
    rm_rf(SAVE_DIR);
    CHECK(!exists(SAVE_FILE), "本地存档没删干净");

    long long files = 0, rbytes = 0;
    err[0] = 0;
    CHECK(sr_pull(TID, UID, &c, &files, &rbytes, err, sizeof(err)) == 0,
          "%s 拉取失败：%s", what, err);
    CHECK(files >= 2, "%s 拉回来的文件数不对：%lld（夹具是 2 个）", what, files);
    CHECK(rbytes > 0, "%s 拉回来的字节数是 0", what);

    got[0] = 0;
    long gn = rd(SAVE_FILE, got, sizeof(got));
    CHECK(gn == wn, "%s 拉回来主文件长度不一致：%ld vs %ld", what, gn, wn);
    CHECK(gn > 0 && !memcmp(got, want, (size_t)wn), "%s 拉回来主文件内容不一致", what);
    CHECK(exists(SAVE_DIR "/sdimg_sce_bu_TEST"), "%s 拉回来少了 _bu_ 备份文件", what);

    /* ⑤ 远端没有时必须是 SR_ENOTFOUND，而不是 -1（第一次用必然遇到） */
    err[0] = 0;
    CHECK(sr_pull("PPSA99999", UID, &c, NULL, NULL, err, sizeof(err)) == SR_ENOTFOUND,
          "%s 远端没文件时该回 SR_ENOTFOUND，实得 err=%s", what, err);
    CHECK(strstr(err, "远程") != NULL, "%s 的提示要说清是远程没有：%s", what, err);
}

/* ══ ⑥ 配置不全 ═════════════════════════════════════════════════════════ */
static void t_bad_cfg(void) {
    printf("-- 配置不全 / 没有存档\n");
    char err[256];
    sr_cfg_t c;
    /* [CN-FORK] v0.5.2.143 smb 已经不是「不支持」了 —— 这里改成验「配置不全」：
     * 没填共享名时必须在**碰网络之前**就失败，并且说清缺的是共享名。 */
    memset(&c, 0, sizeof(c));
    snprintf(c.mode, sizeof(c.mode), "smb");
    snprintf(c.host, sizeof(c.host), "192.168.1.10");

    err[0] = 0;
    CHECK(sr_push(TID, UID, &c, NULL, err, sizeof(err)) == -1, "smb 没填共享名时推送必须失败");
    CHECK(strstr(err, "共享名") != NULL, "smb 的报错要说清缺的是共享名：%s", err);
    err[0] = 0;
    CHECK(sr_pull(TID, UID, &c, NULL, NULL, err, sizeof(err)) == -1, "smb 没填共享名时拉取必须失败");
    CHECK(strstr(err, "共享名") != NULL, "smb 的报错要说清缺的是共享名：%s", err);
    /* 主机上没有这个游戏的存档 → 打包那一步就该报人话，不是去连网络 */
    err[0] = 0;
    memset(&c, 0, sizeof(c));
    snprintf(c.mode, sizeof(c.mode), "http");
    snprintf(c.url, sizeof(c.url), "http://127.0.0.1:%d/x", stub_http_port());
    CHECK(sr_push("PPSA99999", UID, &c, NULL, err, sizeof(err)) == -1,
          "没有存档时推送必须失败");
    CHECK(err[0] != 0, "没有存档时要给人话");
}

/* [CN-FORK] v0.5.2.159 删除远程存档。
 *
 * SMB 真删那一条在这里测不了（主机上没有 SMB 服务可桩，libsmb2 也起不了 server），
 * 所以这里钉的是**剩下那一面** —— 三件事：
 *   ① 参数 / 配置不合法时在碰网络之前就拒；
 *   ② FTP / HTTP **绝不静默成功**：必须回 -1、报错里必须带着协议名
 *      （不带协议名就会被当成「网络坏了」去瞎排查）、**远端那份必须原样还在**；
 *   ③ 空 / 未知 mode 不许被翻成「HTTP 不支持」—— 那是对着一个没填协议的配置撒谎。
 *
 * ② 是这批里最值钱的一条：删除这种东西只要静默「成功」过一次（回 0 却什么都没删），
 * 这个按钮就再也信不过了，而且用户完全看不出。 */
static void t_delete(const char *ftp_root, const char *http_root) {
    printf("-- 删除远程存档（只有 SMB 真删）\n");
    char err[256], rp[1200];
    sr_cfg_t c;
    long long bytes = 0;

    err[0] = 0;
    CHECK(sr_delete(NULL, TID, UID, err, sizeof(err)) == -1, "没配置远程位置时必须失败");
    CHECK(strstr(err, "没有配置远程位置") != NULL, "要说清是没配置：%s", err);

    /* ① 非法 tid：路径穿越一律在本地拒掉，不许发到对面去 */
    memset(&c, 0, sizeof(c));
    snprintf(c.mode, sizeof(c.mode), "smb");
    snprintf(c.host, sizeof(c.host), "127.0.0.1");
    snprintf(c.share, sizeof(c.share), "ps5save");
    err[0] = 0;
    CHECK(sr_delete(&c, "PPSA/../etc", UID, err, sizeof(err)) == -1, "非法 tid 必须拒");
    CHECK(strstr(err, "不合法") != NULL, "非法 tid 的报错要说清：%s", err);

    /* ② FTP：先真推一份上去，再点删除 —— 删不掉是预期，**文件还在**才是要钉的 */
    fixture_reset();
    rm_rf(ftp_root);
    mkdir_p(ftp_root);
    memset(&c, 0, sizeof(c));
    snprintf(c.mode, sizeof(c.mode), "ftp");
    snprintf(c.host, sizeof(c.host), "127.0.0.1");
    c.port = stub_ftp_port();
    snprintf(c.path, sizeof(c.path), "/ps5save");
    err[0] = 0;
    CHECK(sr_push(TID, UID, &c, &bytes, err, sizeof(err)) == 0, "FTP 前置推送失败：%s", err);
    remote_path(rp, sizeof(rp), ftp_root);
    CHECK(exists(rp), "FTP 远端该有刚推上去的那份：%s", rp);
    err[0] = 0;
    CHECK(sr_delete(&c, TID, UID, err, sizeof(err)) == -1, "FTP 这一档不许回成功");
    CHECK(strstr(err, "FTP") != NULL, "报错要带协议名，别让人以为网络坏了：%s", err);
    CHECK(exists(rp), "说不支持，却把远端那份删了（或谎称删了）");

    /* HTTP 同上 */
    rm_rf(http_root);
    mkdir_p(http_root);
    memset(&c, 0, sizeof(c));
    snprintf(c.mode, sizeof(c.mode), "http");
    snprintf(c.host, sizeof(c.host), "127.0.0.1");
    c.port = stub_http_port();
    snprintf(c.url, sizeof(c.url), "http://127.0.0.1:%d/ps5save", stub_http_port());
    err[0] = 0;
    CHECK(sr_push(TID, UID, &c, &bytes, err, sizeof(err)) == 0, "HTTP 前置推送失败：%s", err);
    remote_path(rp, sizeof(rp), http_root);
    CHECK(exists(rp), "HTTP 远端该有刚推上去的那份：%s", rp);
    err[0] = 0;
    CHECK(sr_delete(&c, TID, UID, err, sizeof(err)) == -1, "HTTP 这一档不许回成功");
    CHECK(strstr(err, "HTTP") != NULL, "报错要带协议名：%s", err);
    CHECK(exists(rp), "说不支持，却把远端那份删了（或谎称删了）");

    /* ③ mode 没填 / 填了不认识的：不许顺着 else 落到「HTTP 不支持」上 */
    memset(&c, 0, sizeof(c));
    snprintf(c.host, sizeof(c.host), "127.0.0.1");
    err[0] = 0;
    CHECK(sr_delete(&c, TID, UID, err, sizeof(err)) == -1, "mode 为空必须失败");
    CHECK(strstr(err, "HTTP") == NULL, "没填协议不许谎报成 HTTP 不支持：%s", err);
    CHECK(strstr(err, "不支持的传输方式") != NULL, "没填协议要说清是协议不认识：%s", err);

    /* SMB 连不上：必须 -1，绝不许回 0（端口 1 上没人听，立刻被拒，不会挂住） */
    memset(&c, 0, sizeof(c));
    snprintf(c.mode, sizeof(c.mode), "smb");
    snprintf(c.host, sizeof(c.host), "127.0.0.1");
    c.port = 1;
    snprintf(c.share, sizeof(c.share), "nosuchshare");
    err[0] = 0;
    CHECK(sr_delete(&c, TID, UID, err, sizeof(err)) == -1, "SMB 连不上时必须失败，不许谎报成功");
    CHECK(err[0] != 0, "SMB 连不上时要给人话");
}

int main(void) {
    /* 桩服务必须在 fixture 之前起：两个端口是随机分配的，配置里要用。 */    char http_root[600], ftp_root[600];
    snprintf(http_root, sizeof(http_root), ROOT "/http");
    snprintf(ftp_root, sizeof(ftp_root), ROOT "/ftp");
    if (stub_http_start(http_root) != 0) {
        fprintf(stderr, "HTTP 桩起不来\n");
        return 1;
    }
    if (stub_ftp_start(ftp_root) != 0) {
        fprintf(stderr, "FTP 桩起不来\n");
        return 1;
    }

    t_mode();
    t_url();
    t_roundtrip("HTTP", 0, http_root);
    /* FTP 的远端根目录**故意先清掉**：真机上第一次推送时
     * <path>/<TID>/ 必然还不存在，靠 curl 的 create-missing-dirs 建出来。 */
    t_roundtrip("FTP", 1, ftp_root);
    t_bad_cfg();
    t_delete(ftp_root, http_root);

    stub_http_stop();
    stub_ftp_stop();

    printf("save_remote: %d checks, %d failures\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
