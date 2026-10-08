/* [CN-FORK] v0.5.2.148 test/zip_io_test.c — src/zip_io.c 的 DOS 时间戳回归。
 *
 * 为什么单独给这个文件写一条：`.148` 之前 `zx_dos_time()` 把 `struct tm` 的
 * `tm_mon`（**0-based**）直接塞进 DOS 日期的月份字段（**1-based**），而
 * `zip_extract()` 读侧按标准做 `- 1` —— 写读**不互逆**，每过一轮
 * 「打包 → 解包」日期就往前退一个月，而且累积。
 *
 * 它造成的真机故障：存档拉取的落盘路径是「远端 <uid>.zip → zip_extract →
 * gs_up_cb 把包里的时间 utimes() 到存档文件上」。于是**每跑一次「推送→拉取」，
 * 主机上那份存档的 mtime 就往回退一个月**（10 月 → 9 月 → 8 月 → …）。
 * 存档内容 sha256 全程一字不差，只有日期在倒退 —— 用户看到的就是
 * 「拉取显示成功，但存档不对」。
 *
 * ⚠ 真机上验过，别把它当成理论推演。受控单轮（内容刻意保持不变）：
 *     基线   MDTM=20260908134158  包内月份字段=9
 *     推送后 MDTM=20260908134158  包内月份字段=8   ← 写侧把 tm_mon(8) 当月份写
 *     拉取后 MDTM=20260808134158                    ← 读侧再 -1，又掉一个月
 *   内容 sha256 始终 5b531b74。
 *
 * 三道断言，缺一不可：
 *   ① 写→读 mtime **逐秒一致**（往返自洽）；
 *   ② 原始 DOS 月份字段必须 **1-based**（Windows 资源管理器 / 7-Zip / python
 *      zipfile 都按这个读，写 0-based 就是给别人一个错月份的包）；
 *   ③ 1 月与 12 月的边界 —— 旧代码在 1 月会写出**「0 月」**这种非法值，
 *      读回来 tm_mon = -1，mktime 把它规整成上一年 12 月（年份也一起错）。
 *
 * 依赖刻意压到最小：zip_io.c + inflate.c + game_job.c(CRC) + json_helpers.c +
 * utils.c，**不需要 python3**，所以这一条永远不会被 SKIP —— 数据完整性守卫
 * 不该因为环境缺个解释器就不跑。 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "zip_io.h"

/* log_server.c 的头要 microhttpd.h，主机上没有 —— 给个等价实现，默认静音。 */
void pldmgr_log(const char *fmt, ...) {
    if (!getenv("ZIP_TEST_VERBOSE")) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

#define ROOT "/tmp/pldmgr_zip_test"
#define MAXENT 4

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

/* 按**本地时间**造一个 time_t（zip 的 DOS 时间是本地时间，读写两侧走
 * localtime_r / mktime，所以必须拿同一套口径去比）。 */
static time_t lt(int y, int mo, int d, int h, int mi, int s) {
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year = y - 1900;
    t.tm_mon = mo - 1;
    t.tm_mday = d;
    t.tm_hour = h;
    t.tm_min = mi;
    t.tm_sec = s;
    t.tm_isdst = -1;
    return mktime(&t);
}

static void put_file(const char *path, const void *data, size_t n, time_t mt) {
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "  cannot create %s\n", path); exit(2); }
    if (n) fwrite(data, 1, n, f);
    fclose(f);
    struct timeval tv[2];
    tv[0].tv_sec = mt;
    tv[0].tv_usec = 0;
    tv[1] = tv[0];
    utimes(path, tv);
}

/* 直接读本地头（偏移 12）里的 DOS 日期原值 —— 不解包，看的就是写进包里的位。
 * 本地头布局：0 sig / 4 ver / 6 flags / 8 method / 10 time / 12 date。 */
static uint16_t lh_dos_date(const char *path) {
    unsigned char h[30];
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "  cannot read %s\n", path); exit(2); }
    size_t got = fread(h, 1, sizeof(h), f);
    fclose(f);
    if (got < 30 || h[0] != 'P' || h[1] != 'K' || h[2] != 3 || h[3] != 4) return 0;
    return (uint16_t)(h[12] | (h[13] << 8));
}

/* 解包回调：把全部条目收下来（条目名 / 内容 / mtime）。 */
typedef struct {
    char name[MAXENT][256];
    time_t mt[MAXENT];
    size_t len[MAXENT];
    int n;
} collect_t;

static int collect_cb(void *user, const char *name, const unsigned char *data,
                      size_t len, time_t mt) {
    collect_t *c = (collect_t *)user;
    if (c->n >= MAXENT) return 0;
    snprintf(c->name[c->n], sizeof(c->name[0]), "%s", name);
    c->mt[c->n] = mt;
    c->len[c->n] = len;
    (void)data;
    c->n++;
    return 0;
}

/* 打包 <src> → <zip>，单条目。 */
static int pack1(const char *zip, const char *src, const char *zname) {
    zip_ent_t e;
    memset(&e, 0, sizeof(e));
    snprintf(e.abs, sizeof(e.abs), "%s", src);
    snprintf(e.zname, sizeof(e.zname), "%s", zname);
    const char *err = NULL;
    return zip_write(zip, &e, 1, NULL, &err);
}

/* 读整个 zip 进内存。调用方 free。 */
static unsigned char *slurp(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return NULL; }
    unsigned char *b = (unsigned char *)malloc((size_t)n);
    size_t rd = b ? fread(b, 1, (size_t)n, f) : 0;
    fclose(f);
    if (!b || rd != (size_t)n) { free(b); return NULL; }
    *out_len = (size_t)n;
    return b;
}

int main(void) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "rm -rf %s && mkdir -p %s", ROOT, ROOT);
    if (system(cmd) != 0) { fprintf(stderr, "cannot prepare %s\n", ROOT); return 2; }

    static const unsigned char payload[] = "pldmgr zip dos-time regression payload";
    const size_t plen = sizeof(payload) - 1;

    /* ── ① 主用例：真机上观测到的那个月份（10 月）────────────────────────
     * 旧代码在这里写出字段 9（= 9 月），解回来 mtime 少一个月。 */
    {
        char src[512], zip[512];
        snprintf(src, sizeof(src), "%s/oct.bin", ROOT);
        snprintf(zip, sizeof(zip), "%s/oct.zip", ROOT);
        time_t mt = lt(2026, 10, 8, 21, 41, 58);
        put_file(src, payload, plen, mt);

        struct tm want;
        localtime_r(&mt, &want);
        CHECK(pack1(zip, src, "PPSA27836/sdimg_SAVESERVICE-LINE-0-1Slot") == 0,
              "oct: zip_write 失败");

        uint16_t dd = lh_dos_date(zip);
        int field = (dd >> 5) & 0x0F;
        CHECK(field == 10,
              "oct: 包内 DOS 月份字段 = %d，应为 10（1-based）；"
              "写成 %d 就是旧的 off-by-one（tm_mon）", field, want.tm_mon);
        CHECK(field >= 1 && field <= 12, "oct: 月份字段越界 = %d", field);

        size_t zl = 0;
        unsigned char *buf = slurp(zip, &zl);
        CHECK(buf != NULL, "oct: 读不回 zip");
        if (buf) {
            collect_t c;
            memset(&c, 0, sizeof(c));
            char eb[256] = "";
            int rc = zip_extract(buf, zl, collect_cb, &c, eb, sizeof(eb));
            free(buf);
            CHECK(rc == 0, "oct: zip_extract rc=%d %s", rc, eb);
            CHECK(c.n == 1, "oct: 条目数 = %d，应为 1", c.n);
            if (c.n == 1) {
                CHECK(strcmp(c.name[0], "PPSA27836/sdimg_SAVESERVICE-LINE-0-1Slot") == 0,
                      "oct: 条目名 = %s", c.name[0]);
                CHECK(c.len[0] == plen, "oct: 条目长度 = %zu，应为 %zu", c.len[0], plen);
                struct tm got;
                localtime_r(&c.mt[0], &got);
                CHECK(c.mt[0] == mt,
                      "oct: 往返后 mtime = %lld，应为 %lld（差 %lld 秒）——"
                      "写读不互逆，存档日期会一路倒退",
                      (long long)c.mt[0], (long long)mt, (long long)(c.mt[0] - mt));
                CHECK(got.tm_year == want.tm_year && got.tm_mon == want.tm_mon &&
                          got.tm_mday == want.tm_mday,
                      "oct: 年月日 = %04d-%02d-%02d，应为 %04d-%02d-%02d",
                      got.tm_year + 1900, got.tm_mon + 1, got.tm_mday,
                      want.tm_year + 1900, want.tm_mon + 1, want.tm_mday);
            }
        }
    }

    /* ── ② 月份边界：1 月与 12 月 ────────────────────────────────────────
     * 1 月是旧代码最难看的一处：tm_mon == 0 → 字段 0（**非法**，DOS 月份 1~12），
     * 读回来 tm_mon = -1，mktime 规整成上一年 12 月 —— 连年份都错。 */
    {
        static const int months[] = {1, 2, 11, 12};
        for (size_t i = 0; i < sizeof(months) / sizeof(months[0]); i++) {
            char tag[32], src[512], zip[512];
            snprintf(tag, sizeof(tag), "m%02d", months[i]);
            snprintf(src, sizeof(src), "%s/%s.bin", ROOT, tag);
            snprintf(zip, sizeof(zip), "%s/%s.zip", ROOT, tag);
            time_t mt = lt(2026, months[i], 15, 9, 30, 0);
            put_file(src, payload, plen, mt);
            CHECK(pack1(zip, src, "x/y.bin") == 0, "%s: zip_write 失败", tag);

            int field = (lh_dos_date(zip) >> 5) & 0x0F;
            CHECK(field == months[i], "%s: 月份字段 = %d，应为 %d",
                  tag, field, months[i]);

            size_t zl = 0;
            unsigned char *buf = slurp(zip, &zl);
            if (!buf) { CHECK(0, "%s: 读不回 zip", tag); continue; }
            collect_t c;
            memset(&c, 0, sizeof(c));
            char eb[256] = "";
            int rc = zip_extract(buf, zl, collect_cb, &c, eb, sizeof(eb));
            free(buf);
            CHECK(rc == 0, "%s: zip_extract rc=%d %s", tag, rc, eb);
            if (c.n == 1) {
                struct tm a, b;
                localtime_r(&c.mt[0], &a);
                localtime_r(&mt, &b);
                CHECK(c.mt[0] == mt,
                      "%s: 往返 mtime 差 %lld 秒（%04d-%02d vs %04d-%02d）",
                      tag, (long long)(c.mt[0] - mt),
                      a.tm_year + 1900, a.tm_mon + 1, b.tm_year + 1900, b.tm_mon + 1);
            }
        }
    }

    /* ── ③ 负向对照：字段**必须不等于** tm_mon ───────────────────────────
     * 给「有人觉得那个 +1 多余又改回去」准备的：10 月的 tm_mon 就是 9，
     * 字段一旦回到 9 这里立刻红，而且报错里直接点名。 */
    {
        char src[512], zip[512];
        snprintf(src, sizeof(src), "%s/neg.bin", ROOT);
        snprintf(zip, sizeof(zip), "%s/neg.zip", ROOT);
        time_t mt = lt(2026, 10, 8, 21, 41, 58);
        put_file(src, payload, plen, mt);
        CHECK(pack1(zip, src, "n.bin") == 0, "neg: zip_write 失败");
        struct tm want;
        localtime_r(&mt, &want);
        int field = (lh_dos_date(zip) >> 5) & 0x0F;
        CHECK(field != want.tm_mon,
              "neg: 月份字段又变回 tm_mon(%d) 了 —— 就是 `.148` 修掉的 off-by-one",
              want.tm_mon);
    }

    /* ── ④ 多条目：两条各自的日期都要对（防「只改了一处」）───────────── */
    {
        char s1[512], s2[512], zip[512];
        snprintf(s1, sizeof(s1), "%s/m1.bin", ROOT);
        snprintf(s2, sizeof(s2), "%s/m2.bin", ROOT);
        snprintf(zip, sizeof(zip), "%s/multi.zip", ROOT);
        time_t t1 = lt(2026, 3, 1, 8, 0, 0);
        time_t t2 = lt(2026, 11, 30, 23, 59, 58);
        put_file(s1, payload, plen, t1);
        put_file(s2, payload, plen, t2);

        zip_ent_t e[2];
        memset(e, 0, sizeof(e));
        snprintf(e[0].abs, sizeof(e[0].abs), "%s", s1);
        snprintf(e[0].zname, sizeof(e[0].zname), "d/one.bin");
        snprintf(e[1].abs, sizeof(e[1].abs), "%s", s2);
        snprintf(e[1].zname, sizeof(e[1].zname), "d/two.bin");
        const char *err = NULL;
        CHECK(zip_write(zip, e, 2, NULL, &err) == 0, "multi: zip_write 失败");

        size_t zl = 0;
        unsigned char *buf = slurp(zip, &zl);
        CHECK(buf != NULL, "multi: 读不回 zip");
        if (buf) {
            collect_t c;
            memset(&c, 0, sizeof(c));
            char eb[256] = "";
            int rc = zip_extract(buf, zl, collect_cb, &c, eb, sizeof(eb));
            free(buf);
            CHECK(rc == 0, "multi: zip_extract rc=%d %s", rc, eb);
            CHECK(c.n == 2, "multi: 条目数 = %d，应为 2", c.n);
            if (c.n == 2) {
                CHECK(c.mt[0] == t1, "multi: 第一条 mtime 差 %lld 秒",
                      (long long)(c.mt[0] - t1));
                CHECK(c.mt[1] == t2, "multi: 第二条 mtime 差 %lld 秒",
                      (long long)(c.mt[1] - t2));
            }
        }
    }

    /* ── ⑤ 年份下限：1980 之前一律钳到 1980（字段年份位为 0）────────────── */
    {
        char src[512], zip[512];
        snprintf(src, sizeof(src), "%s/old.bin", ROOT);
        snprintf(zip, sizeof(zip), "%s/old.zip", ROOT);
        put_file(src, payload, plen, lt(1975, 6, 15, 12, 0, 0));
        CHECK(pack1(zip, src, "o.bin") == 0, "old: zip_write 失败");
        uint16_t dd = lh_dos_date(zip);
        CHECK(((dd >> 9) & 0x7F) == 0, "old: 1980 下限没生效（年份位 = %d）",
              (dd >> 9) & 0x7F);
        CHECK(((dd >> 5) & 0x0F) == 6, "old: 6 月应写成 6，实际 %d", (dd >> 5) & 0x0F);
    }

    printf("zip_io: %d checks, %d failed\n", g_checks, g_fails);
    snprintf(cmd, sizeof(cmd), "rm -rf %s", ROOT);
    if (system(cmd) != 0) { /* 清理失败不影响结论 */ }
    return g_fails ? 1 : 0;
}
