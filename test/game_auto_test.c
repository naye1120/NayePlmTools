/* [CN-FORK] v0.5.2.142 test/game_auto_test.c — src/game_auto.c 的主机回归测试。
 *
 * 覆盖：
 *   ① game_auto_plan 的**全部组合**（这是整条链上唯一有分支逻辑的地方，
 *      而它跑在真机上 —— 宿主上穷举比在真机上试靠谱得多）
 *   ② 手动推 / 拉端到端（走本机 HTTP 桩，真的搬字节）
 *   ③ 远端还没有存档时不算失败（第一次用必然遇到，不能给用户一条红报错）
 *   ④ 不支持的协议（smb）要说清原因
 *   ⑤ 运行期状态：running 跟着探测走、last 记下最后一次动作
 *
 * 探测（sceLncUtilGetAppIdOfRunningBigApp）在宿主上没有那个符号，
 * 用 -DGA_NO_LNCUTIL 关掉默认实现，测试自己装探针（game_auto_set_probe）。
 *
 * 用法：由 test/run_host_tests.sh 拨起，也可单独跑。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "game_auto.h"
#include "game_saves.h"
#include "stub_servers.h"

#include <stdarg.h>
void pldmgr_log(const char *fmt, ...) {
    if (!getenv("SAVES_TEST_VERBOSE")) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

#define ROOT ROOT_DIR
#define HOME ROOT "/home"
#define TID  "PPSA012345"
#define TID2 "PPSA067890"
#define UID  "00000002"
#define SAVE_DIR HOME "/" UID "/savedata_prospero/" TID
#define SAVE_FILE SAVE_DIR "/sdimg_TEST"

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
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", p);
    if (system(cmd) != 0) { /* 清不掉也不影响断言 */ }
}

static char g_http_root[600];
static int g_http_port;

static void remote_path(char *out, size_t osz, const char *tid) {
    snprintf(out, osz, "%s/ps5save/%s/%s.zip", g_http_root, tid, UID);
}

/* 配置就写在 GAME_SAVES_AUTO_PATH 指的那个文件里 —— 引擎读的就是它。 */
static void write_cfg(const char *mode, const char *url,
                      int enabled, int on_launch, int on_exit) {
    char p[800];
    snprintf(p, sizeof(p), "%s", AUTO_PATH);
    char parent[800];
    snprintf(parent, sizeof(parent), "%s", p);
    char *sl = strrchr(parent, '/');
    if (sl) { *sl = 0; mkdir_p(parent); }

    FILE *f = fopen(p, "wb");
    if (!f) return;
    fprintf(f,
            "{\"%s\":{\"enabled\":%s,\"mode\":\"%s\",\"on_launch\":%s,\"on_exit\":%s,"
            "\"ftp\":{\"host\":\"\",\"port\":21,\"path\":\"\",\"user\":\"\",\"password\":\"\"},"
            "\"http\":{\"url\":\"%s\",\"token\":\"\"},"
            "\"smb\":{\"host\":\"\",\"share\":\"\",\"sub\":\"\",\"user\":\"\",\"password\":\"\"}}}",
            TID, enabled ? "true" : "false", mode, on_launch ? "true" : "false",
            on_exit ? "true" : "false", url);
    fclose(f);
}

static void fixture_reset(void) {
    rm_rf(ROOT);
    put(SAVE_FILE, 2000, 'X');
    put(SAVE_DIR "/sdimg_sce_bu_TEST", 300, 'Y');
    mkdir_p(g_http_root);
}

/* ══ ① 状态机（穷举）═══════════════════════════════════════════════════ */
static void t_plan(void) {
    printf("-- game_auto_plan（穷举）\n");
    ga_step_t s[2];
    int n;

    n = game_auto_plan("", "", 0, 0, 0, 0, s);
    CHECK(n == 0, "都没在跑 → 不该有动作，实得 %d", n);

    /* 启动：把远程那份拉下来 */
    n = game_auto_plan("", TID, 0, 0, 1, 1, s);
    CHECK(n == 1 && s[0].act == GA_ACT_PULL && !strcmp(s[0].tid, TID),
          "启动该拉取，实得 n=%d act=%d", n, n ? s[0].act : -1);
    n = game_auto_plan("", TID, 0, 0, 1, 0, s);
    CHECK(n == 0, "没开 on_launch → 不该拉，实得 %d", n);
    n = game_auto_plan("", TID, 0, 0, 0, 1, s);
    CHECK(n == 0, "总开关没开 → 不该拉，实得 %d", n);

    /* 退出：把主机这份推上去 */
    n = game_auto_plan(TID, "", 1, 1, 0, 0, s);
    CHECK(n == 1 && s[0].act == GA_ACT_PUSH && !strcmp(s[0].tid, TID),
          "退出该推送，实得 n=%d act=%d", n, n ? s[0].act : -1);
    n = game_auto_plan(TID, "", 1, 0, 0, 0, s);
    CHECK(n == 0, "没开 on_exit → 不该推，实得 %d", n);
    n = game_auto_plan(TID, "", 0, 1, 0, 0, s);
    CHECK(n == 0, "总开关没开 → 不该推，实得 %d", n);

    /* 换游戏：**先推旧的、再拉新的**，顺序写死才有一份可预期的日志 */
    n = game_auto_plan(TID, TID2, 1, 1, 1, 1, s);
    CHECK(n == 2, "换游戏该有两步，实得 %d", n);
    CHECK(n == 2 && s[0].act == GA_ACT_PUSH && !strcmp(s[0].tid, TID),
          "换游戏第一步该是推旧的");
    CHECK(n == 2 && s[1].act == GA_ACT_PULL && !strcmp(s[1].tid, TID2),
          "换游戏第二步该是拉新的");

    n = game_auto_plan(TID, TID2, 0, 0, 1, 1, s);
    CHECK(n == 1 && s[0].act == GA_ACT_PULL, "旧的没开 → 只拉新的，实得 %d", n);
    n = game_auto_plan(TID, TID2, 1, 1, 0, 0, s);
    CHECK(n == 1 && s[0].act == GA_ACT_PUSH, "新的没开 → 只推旧的，实得 %d", n);

    /* 同一款连着两轮 —— 绝大多数轮次走这里 */
    n = game_auto_plan(TID, TID, 1, 1, 1, 1, s);
    CHECK(n == 0, "同一款连着跑不该有动作，实得 %d", n);

    /* NULL 与空串等价 */
    n = game_auto_plan(NULL, NULL, 0, 0, 0, 0, s);
    CHECK(n == 0, "NULL 该安全，实得 %d", n);
    n = game_auto_plan(TID, NULL, 1, 1, 0, 0, s);
    CHECK(n == 1 && s[0].act == GA_ACT_PUSH, "cur=NULL 该当没在跑（推送）");
    n = game_auto_plan(NULL, TID, 0, 0, 1, 1, s);
    CHECK(n == 1 && s[0].act == GA_ACT_PULL, "prev=NULL 该当之前没在跑（拉取）");
}

/* ══ 探针（宿主上没有 sceLncUtil）══════════════════════════════════════ */
static const char *g_fake_running = NULL;
static int probe_fn(char *out, size_t osz) {
    if (!g_fake_running) {
        if (out && osz) out[0] = 0;
        return 0;
    }
    snprintf(out, osz, "%s", g_fake_running);
    return 1;
}

/* ══ ②③④⑤ 手动推拉 + 状态 ═══════════════════════════════════════════ */
static void t_sync(void) {
    printf("-- 手动推 / 拉 + 运行期状态\n");
    char url[600], body[300], out[2048], msg[300], st[600], rp[700];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/ps5save", g_http_port);

    fixture_reset();
    write_cfg("http", url, 1, 1, 1);
    remote_path(rp, sizeof(rp), TID);

    char want[8192];
    long wn = rd(SAVE_FILE, want, sizeof(want));
    CHECK(wn > 0, "夹具没造出来");

    /* 推送 */
    snprintf(body, sizeof(body), "{\"tid\":\"%s\",\"action\":\"push\"}", TID);
    out[0] = msg[0] = 0;
    CHECK(game_auto_sync_action(body, strlen(body), out, sizeof(out), msg, sizeof(msg)) == 0,
          "手动推送失败：%s", msg);
    CHECK(strstr(out, "\"ok\":true") != NULL, "推送响应该是 ok:true：%s", out);
    CHECK(strstr(out, "\"action\":\"push\"") != NULL, "推送响应该带 action：%s", out);
    CHECK(exists(rp), "远端没收到文件：%s", rp);
    long rn = rd(rp, st, sizeof(st));
    CHECK(rn > 4 && !memcmp(st, "PK\x03\x04", 4), "远端收到的不像 zip");

    /* 拉取：先删本地，验内容逐字节一样 */
    rm_rf(SAVE_DIR);
    CHECK(!exists(SAVE_FILE), "本地没删干净");
    snprintf(body, sizeof(body), "{\"tid\":\"%s\",\"action\":\"pull\"}", TID);
    out[0] = msg[0] = 0;
    CHECK(game_auto_sync_action(body, strlen(body), out, sizeof(out), msg, sizeof(msg)) == 0,
          "手动拉取失败：%s", msg);
    CHECK(strstr(out, "\"ok\":true") != NULL, "拉取响应该是 ok:true：%s", out);
    char got[8192];
    long gn = rd(SAVE_FILE, got, sizeof(got));
    CHECK(gn == wn && !memcmp(got, want, (size_t)wn), "拉回来的内容跟推上去的不一样");
    CHECK(exists(SAVE_DIR "/sdimg_sce_bu_TEST"), "拉回来少了 _bu_ 备份");

    /* ⑤ 运行期状态 */
    CHECK(game_auto_state_json(TID, st, sizeof(st)) > 0, "状态 JSON 生成失败");
    CHECK(strstr(st, "\"action\":\"pull\"") != NULL,
          "状态该记下最后一次是 pull（最近一次动作）：%s", st);
    CHECK(strstr(st, "\"ok\":true") != NULL, "状态该记 ok:true：%s", st);
    CHECK(strstr(st, "\"running\":false") != NULL, "宿主上没装探针时该是 false：%s", st);

    g_fake_running = TID;
    game_auto_set_probe(probe_fn);
    CHECK(game_auto_state_json(TID, st, sizeof(st)) > 0, "状态 JSON 生成失败");
    CHECK(strstr(st, "\"running\":true") != NULL, "探针报这款在跑，running 该是 true：%s", st);
    g_fake_running = TID2;
    CHECK(game_auto_state_json(TID, st, sizeof(st)) > 0, "状态 JSON 生成失败");
    CHECK(strstr(st, "\"running\":false") != NULL,
          "在跑的是另一款，这款该是 false：%s", st);
    g_fake_running = NULL;
    game_auto_set_probe(NULL);

    /* ③ 远端还没有这个文件 —— 不是失败 */
    unlink(rp);
    snprintf(body, sizeof(body), "{\"tid\":\"%s\",\"action\":\"pull\"}", TID);
    out[0] = msg[0] = 0;
    CHECK(game_auto_sync_action(body, strlen(body), out, sizeof(out), msg, sizeof(msg)) == 0,
          "远端没存档时拉取不该算失败：%s", msg);
    CHECK(strstr(out, "\"ok\":true") != NULL, "远端没存档该回 ok:true：%s", out);
    CHECK(strstr(out, "远程还没有") != NULL, "提示要说清是远程还没有：%s", out);
    /* 而且**不能动本地的存档** —— 拉失败把用户的存档弄没是最坏的 */
    CHECK(exists(SAVE_FILE), "拉取失败不该动本地存档");

    /* ④ 不认识的协议（smb 在 .143 起已支持，这里改用真·不认识的协议名测「不支持」） */
    write_cfg("webdav", "192.168.1.10", 1, 1, 1);
    snprintf(body, sizeof(body), "{\"tid\":\"%s\",\"action\":\"push\"}", TID);
    out[0] = msg[0] = 0;
    CHECK(game_auto_sync_action(body, strlen(body), out, sizeof(out), msg, sizeof(msg)) == -1,
          "不认识的协议必须失败");
    CHECK(strstr(msg, "不支持") != NULL, "不认识的协议报错要说清不支持：%s", msg);

    /* ④b smb 已支持，但缺主机要在碰网络前就拦下来、且说清是缺主机 */
    write_cfg("smb", "", 1, 1, 1);
    snprintf(body, sizeof(body), "{\"tid\":\"%s\",\"action\":\"push\"}", TID);
    out[0] = msg[0] = 0;
    CHECK(game_auto_sync_action(body, strlen(body), out, sizeof(out), msg, sizeof(msg)) == -1,
          "smb 缺主机必须失败");
    CHECK(strstr(msg, "主机") != NULL, "smb 缺主机报错要说清是主机：%s", msg);

    /* 参数不合法 */
    write_cfg("http", url, 1, 1, 1);
    snprintf(body, sizeof(body), "{\"tid\":\"%s\",\"action\":\"bogus\"}", TID);
    out[0] = msg[0] = 0;
    CHECK(game_auto_sync_action(body, strlen(body), out, sizeof(out), msg, sizeof(msg)) == -1,
          "未知 action 必须失败");
    snprintf(body, sizeof(body), "{\"tid\":\"../etc\",\"action\":\"push\"}");
    out[0] = msg[0] = 0;
    CHECK(game_auto_sync_action(body, strlen(body), out, sizeof(out), msg, sizeof(msg)) == -1,
          "带 .. 的 tid 必须失败");
    snprintf(body, sizeof(body), "{\"tid\":\"%s\",\"action\":\"push\",\"uid\":\"a/b\"}", TID);
    out[0] = msg[0] = 0;
    CHECK(game_auto_sync_action(body, strlen(body), out, sizeof(out), msg, sizeof(msg)) == -1,
          "带 / 的 uid 必须失败");
    out[0] = msg[0] = 0;
    CHECK(game_auto_sync_action("{}", 2, out, sizeof(out), msg, sizeof(msg)) == -1,
          "空请求必须失败");
}

int main(void) {
    snprintf(g_http_root, sizeof(g_http_root), "%s/http", ROOT);
    if (stub_http_start(g_http_root) != 0) {
        fprintf(stderr, "HTTP 桩起不来\n");
        return 1;
    }
    g_http_port = stub_http_port();

    t_plan();
    t_sync();

    stub_http_stop();
    printf("game_auto: %d checks, %d failures\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
