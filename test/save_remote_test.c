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
    CHECK(sr_mode_supported("smb") == 0, "smb 必须不支持（SDK 的 curl 没编）");
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

    memset(&c, 0, sizeof(c));
    snprintf(c.mode, sizeof(c.mode), "smb");
    snprintf(c.host, sizeof(c.host), "192.168.1.10");
    CHECK(sr_build_url(&c, TID, UID, url, sizeof(url), err, sizeof(err)) != 0,
          "smb 必须被拒");
    CHECK(strstr(err, "SMB") != NULL || strstr(err, "不支持") != NULL,
          "smb 的拒绝理由要说清是不支持：%s", err);

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
    memset(&c, 0, sizeof(c));
    snprintf(c.mode, sizeof(c.mode), "smb");
    snprintf(c.host, sizeof(c.host), "192.168.1.10");

    err[0] = 0;
    CHECK(sr_push(TID, UID, &c, NULL, err, sizeof(err)) == -1, "smb 推送必须失败");
    CHECK(strstr(err, "不支持") != NULL, "smb 的报错要说清是不支持：%s", err);
    /* 主机上没有这个游戏的存档 → 打包那一步就该报人话，不是去连网络 */
    err[0] = 0;
    memset(&c, 0, sizeof(c));
    snprintf(c.mode, sizeof(c.mode), "http");
    snprintf(c.url, sizeof(c.url), "http://127.0.0.1:%d/x", stub_http_port());
    CHECK(sr_push("PPSA99999", UID, &c, NULL, err, sizeof(err)) == -1,
          "没有存档时推送必须失败");
    CHECK(err[0] != 0, "没有存档时要给人话");
}

int main(void) {
    /* 桩服务必须在 fixture 之前起：两个端口是随机分配的，配置里要用。 */
    char http_root[600], ftp_root[600];
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

    stub_http_stop();
    stub_ftp_stop();

    printf("save_remote: %d checks, %d failures\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
