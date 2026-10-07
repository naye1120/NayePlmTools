/* [CN-FORK] v0.5.2.132 tt_param_rename 定点替换的实测
 *
 * 为什么必须单独测：这一段是唯一会**写进用户游戏包**的逻辑。改坏 param.json
 * 是不可逆的（游戏起不来），而它在真机上要到「改完名 + SMP 重扫」才暴露，
 * 反馈链太长了。这里拿真机 FTP 拉下来的三份 param.json（CRLF + 2 空格缩进的美化
 * 格式，1 个 / 3 个 / 10 个语言键）在主机上直接跑，断言：
 *   ① 每个 titleName 的值都被换成新名字，处数正确；
 *   ② 缩进、CRLF、其他字段一字节不改 —— 逐字节比对「除 titleName 值之外的
 *      全部字节」；
 *   ③ 新名字里的 " 与 \ 被正确转义，改出来的文件仍是合法 JSON；
 *   ④ 中文（UTF-8 多字节）不被截断；
 *   ⑤ 改写结果经真 cJSON 解析器验证仍是合法 JSON，且每个语言键的 titleName
 *      读回来都等于新名字（这条同时验证了 " 与 \ 的转义）；
 *   ⑥ 没有 titleName 的文件返回 0（不误判成功）。
 *
 * 顺带钉住一个 schema 事实：localizedParameters 里除了 <lang>: {...} 之外还有
 * defaultLanguage: "..." 这类**字符串**子节点（四份夹具都有）。它不携带
 * titleName，所以「全文只替换 "titleName" 的值」不会误伤它 —— 但也别在解析
 * 校验时把它当成缺字段的语言键。
 *
 * 运行：见 test/run_host_tests.sh 里的 param_rename 那条。夹具路径由
 * PARAM_FIXTURE_DIR 指定，指向 _ftp_probe/evidence/recon/。 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <sys/stat.h>

#include "stb_image.h"
#include "stb_image_write.h"

/* ── 桩：game_title.c 用到的外部符号（本测试只碰改名路径）────────────── */
void pldmgr_log(const char *fmt, ...) { (void)fmt; }
void game_mgr_invalidate(void) {}
int game_mgr_sce_sys_dir(const char *tid, char *out, size_t osz) { (void)tid; (void)out; (void)osz; return 0; }

static char g_kind[16] = "folder";
static char g_srcpath[600] = "";
int game_mgr_source_paths(const char *tid, char *kind, size_t ksz,
                          char *path, size_t psz) {
    (void)tid;
    snprintf(kind, ksz, "%s", g_kind);
    snprintf(path, psz, "%s", g_srcpath);
    return 1;
}
int tile_db_set_name(const char *tid, const char *new_name,
                     char *msg, size_t msgsz) {
    (void)tid; (void)new_name; (void)msg; (void)msgsz;
    return 0;
}
int tile_db_set_bg(const char *tid, const char *dds_path,
                   char *msg, size_t msgsz) {
    (void)tid; (void)dds_path; (void)msg; (void)msgsz;
    return 0;
}
/* stb 图像 API 用不到（本测试不碰换图路径），给个桩把链接补上。
 * 签名照 stb_image.h / stb_image_write.h 的原样，别自己编。 */
unsigned char *stbi_load_from_memory(const unsigned char *b, int n,
                                     int *x, int *y, int *c, int d) {
    (void)b; (void)n; (void)x; (void)y; (void)c; (void)d; return NULL;
}
void stbi_image_free(void *p) { free(p); }
#include "stb_image_write.h"
int stbi_write_png_to_func(stbi_write_func *func, void *ctx,
                           int w, int h, int c,
                           const void *d, int s) {
    (void)func; (void)ctx; (void)w; (void)h; (void)c; (void)d; (void)s;
    return 0;
}

/* cJSON：断言「改完仍是合法 JSON」要用真解析器，只 include 头拿 API 声明。
 * 实现走 src/psaccounts/third_party/cJSON.c（它 #include 了 cJSON_real.c）。 */
#include "cJSON.h"

/* json_helpers 用真实现（它只依赖标准库），game_title.c 本体静态函数对我们可见 */
#include "../src/json_helpers.c"
#include "../src/game_title.c"

/* ── 断言与文件工具 ── */
static int g_fail = 0;
#define CHECK(cond, ...) do { \
    if (cond) { printf("  ok   " __VA_ARGS__); printf("\n"); } \
    else { printf("  FAIL " __VA_ARGS__); printf("\n"); g_fail = 1; } \
} while (0)

static int read_whole(const char *path, unsigned char **out, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return 0; }
    long n = ftell(f);
    if (n < 0) { fclose(f); return 0; }
    rewind(f);
    unsigned char *b = (unsigned char *)malloc((size_t)n);
    if (!b) { fclose(f); return 0; }
    if (fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); fclose(f); return 0; }
    fclose(f);
    *out = b;
    *out_len = (size_t)n;
    return 1;
}

/* 统计 buf 里 "titleName" : "<...>" 的处数（独立实现，用来交叉验证 hits） */
static int count_title_keys(const unsigned char *b, size_t n) {
    int hits = 0;
    for (size_t i = 0; i + 11 <= n; i++) {
        if (memcmp(b + i, "\"titleName\"", 11) != 0) continue;
        size_t j = i + 11;
        while (j < n && (b[j] == ' ' || b[j] == '\t' || b[j] == '\r' || b[j] == '\n')) j++;
        if (j < n && b[j] == ':') j++;
        while (j < n && (b[j] == ' ' || b[j] == '\t' || b[j] == '\r' || b[j] == '\n')) j++;
        if (j < n && b[j] == '"') hits++;
    }
    return hits;
}

/* 把 buf 里每个 titleName 的值抽出来，用 \x01 分隔拼进 out。
 * 用来比对「除了 titleName 的值之外，其他字节是否完全一致」。 */
static void strip_title_values(const unsigned char *b, size_t n,
                               char *out, size_t osz) {
    size_t k = 0;
    for (size_t i = 0; i < n && k + 1 < osz; ) {
        if (i + 11 <= n && memcmp(b + i, "\"titleName\"", 11) == 0) {
            size_t j = i + 11;
            while (j < n && (b[j] == ' ' || b[j] == '\t' || b[j] == '\r' || b[j] == '\n')) j++;
            if (j < n && b[j] == ':') j++;
            while (j < n && (b[j] == ' ' || b[j] == '\t' || b[j] == '\r' || b[j] == '\n')) j++;
            if (j < n && b[j] == '"') {
                size_t s = j + 1;                 /* 值开头 */
                while (s < n) {
                    if (b[s] == '\\' && s + 1 < n) { s += 2; continue; }
                    if (b[s] == '"') break;
                    s++;
                }
                out[k++] = 1;                     /* 占位分隔符 */
                i = (s < n) ? s + 1 : n;
                continue;
            }
        }
        out[k++] = (char)b[i++];
    }
    out[k] = 0;
}

/* 用真解析器验证：① 改写结果仍是合法 JSON；② 从解析树里把 titleName 读回来，
 * 每个语言键都得等于新名字。这是「不会把游戏弄坏」这条不变量的最终检验 ——
 * 字节级比对只能证明我们没动别的，解析器能证明 param.json 本身没坏。 */
static void check_json_valid(const unsigned char *b, size_t n, const char *newname) {
    char *js = (char *)malloc(n + 1);
    if (!js) { printf("  FAIL malloc\n"); g_fail = 1; return; }
    memcpy(js, b, n);
    js[n] = 0;

    cJSON *root = cJSON_ParseWithLength(js, n);
    CHECK(root != NULL, "改写后仍是合法 JSON（%zu B）", n);
    free(js);
    if (!root) return;

    cJSON *loc = cJSON_GetObjectItem(root, "localizedParameters");
    CHECK(loc != NULL && loc->type == cJSON_Object, "localizedParameters 仍在且是对象");

    /* schema 里 localizedParameters 除了 <lang>: {...} 还有 defaultLanguage: "..." 这种
     * 字符串子节点。它们不携带 titleName，不算失败 —— 只检查对象子节点。 */
    int checked = 0, bad = 0, nonobj = 0;
    for (cJSON *lang = loc ? loc->child : NULL; lang; lang = lang->next) {
        if (lang->type != cJSON_Object) { nonobj++; continue; }
        cJSON *t = cJSON_GetObjectItem(lang, "titleName");
        if (!t || t->type != cJSON_String) { bad++; continue; }
        checked++;
        if (strcmp(t->valuestring, newname) != 0) bad++;
    }
    CHECK(checked > 0, "解析到 %d 个语言键的 titleName（另有 %d 个非对象键）",
          checked, nonobj);
    CHECK(bad == 0, "解析出来的每个 titleName 都 == 新名字（不符 %d 个）", bad);

    cJSON_Delete(root);
}

static void run_case(const char *path, const char *newname) {
    printf("=== %s  ->  \"%s\"\n", path, newname);
    unsigned char *buf = NULL;
    size_t n = 0;
    if (!read_whole(path, &buf, &n)) { printf("  FAIL 读不到夹具\n"); g_fail = 1; return; }
    size_t crlf = 0;
    for (size_t i = 0; i + 1 < n; i++) if (buf[i] == '\r' && buf[i + 1] == '\n') crlf++;
    int expect = count_title_keys(buf, n);

    char before[8192];
    strip_title_values(buf, n, before, sizeof(before));
    size_t bsize = strlen(before);

    char esc[GAME_TITLE_MAX_NAME * 3 + 16];
    int hits = tt_param_rename(&buf, &n, newname, esc, sizeof(esc));

    CHECK(hits == expect, "替换处数 %d == 独立统计 %d", hits, expect);
    if (expect > 0) {
        /* ② 除 titleName 的值之外，字节完全一致 */
        char after[8192];
        strip_title_values(buf, n, after, sizeof(after));
        size_t asize = strlen(after);
        CHECK(asize == bsize, "非值字节数一致 %zu == %zu", asize, bsize);
        CHECK(asize == bsize && memcmp(before, after, asize) == 0,
              "非值字节逐字节一致（缩进/CRLF/其他字段未动）");
        /* ③ 新名字确实写进去了，且出现次数与语言数一致 */
        int got = 0;
        size_t el = strlen(esc);
        for (size_t i = 0; i + el <= n; i++)
            if (memcmp(buf + i, esc, el) == 0) got++;
        CHECK(got == expect, "转义后的新名字出现 %d 次 == 语言数 %d", got, expect);
        /* ④ 名字里若没有需要转义的字符，原文应该能直接搜到。
         * 含 " 或 \ 时原文必然搜不到 —— 那才是**正确**的行为（JSON 必须转义），
         * 所以这条断言只对无特殊字符的名字生效。上面那条用的是 esc，才是真正的不变量。 */
        if (!strchr(newname, '"') && !strchr(newname, '\\'))
            CHECK(strstr((const char *)buf, newname) != NULL,
                  "无特殊字符时新名字原文可在文件中直接搜到");
        /* ⑤ 真解析器校验：JSON 仍然合法，且值读得回来（含 " 与 \ 的转义正确） */
        check_json_valid(buf, n, newname);
    }
    if (crlf > 0) {
        size_t crlf2 = 0;
        for (size_t i = 0; i + 1 < n; i++) if (buf[i] == '\r' && buf[i + 1] == '\n') crlf2++;
        CHECK(crlf2 == crlf, "CRLF 行数不变 %zu == %zu", crlf2, crlf);
    }
    free(buf);
}

int main(int argc, char **argv) {
    const char *dir = (argc > 1) ? argv[1] : "evidence/recon";
    char p[1024];
    printf("# param_rename_test  夹具目录 = %s\n\n", dir);

    snprintf(p, sizeof(p), "%s/SRC_param_PPSA32786-app_AMPR.json", dir);
    run_case(p, "仁王３");                       /* 中文，10 个语言键 */

    snprintf(p, sizeof(p), "%s/SRC_param_PPSA27836-鬼武者.json", dir);
    run_case(p, "鬼武者 我的名字");               /* 3 个语言键，含空格 */

    snprintf(p, sizeof(p), "%s/param_PPSA08666.json", dir);
    run_case(p, "FF7 重生 \"引号\" 测试");        /* 1 个语言键 + 需要转义的引号 */

    snprintf(p, sizeof(p), "%s/param_NAYE00001.json", dir);
    run_case(p, "NayePlmTools \\ 反斜杠");       /* 需要转义的反斜杠 */

    /* ⑤ 没有 titleName 的文件：必须返回 0，不能误判成功，且内容一字节不改 */
    printf("=== 无 titleName 的文件（应返回 0）\n");
    snprintf(p, sizeof(p), "%s/smp_config.ini", dir);
    unsigned char *b = NULL; size_t n = 0;
    if (read_whole(p, &b, &n)) {
        unsigned char *copy = (unsigned char *)malloc(n);
        size_t before_len = n;
        memcpy(copy, b, n);
        char esc[GAME_TITLE_MAX_NAME * 3 + 16];
        int h = tt_param_rename(&b, &n, "whatever", esc, sizeof(esc));
        CHECK(h == 0, "无 titleName → hits=0（实得 %d）", h);
        CHECK(n == before_len && memcmp(b, copy, n) == 0,
              "内容一字节未改（%zu B）", n);
        free(copy);
        free(b);
    } else {
        printf("  SKIP 夹具缺失\n");
    }

    /* 越界防护：空文件、只有一段残缺的 titleName */
    printf("=== 边界输入\n");
    {
        unsigned char tinybuf[] = { '"', 't', 'i', 't', 'l', 'e', 'N', 'a', 'm', 'e', '"', ':', ' ' };
        unsigned char *tiny = tinybuf;
        size_t tn = sizeof(tinybuf);
        char esc[GAME_TITLE_MAX_NAME * 3 + 16];
        int h = tt_param_rename(&tiny, &tn, "X", esc, sizeof(esc));
        CHECK(h == 0, "残缺（缺值引号）→ hits=0（实得 %d），不越界", h);
    }
    {
        unsigned char *b2 = NULL;
        size_t n2 = 0;
        CHECK(read_whole(__FILE__, &b2, &n2), "读自己当夹具");
        if (b2) {
            char esc[GAME_TITLE_MAX_NAME * 3 + 16];
            int h = tt_param_rename(&b2, &n2, "替换不了因为不合法", esc, sizeof(esc));
            CHECK(h >= 0, "源码文件不崩溃，hits=%d", h);
            free(b2);
        }
    }

    printf("\n%s\n", g_fail ? "RESULT: FAIL" : "RESULT: PASS");
    return g_fail ? 1 : 0;
}
