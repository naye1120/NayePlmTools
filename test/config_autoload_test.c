/* [CN-FORK] v0.5.2.155 宿主测试：/set_config 的 AUTOLOAD_LIST 写回必须**幂等**。
 *
 * 用户口径：「自动加载页面，自己多次增加 1 秒延时」。这个坑有两半，两半都只能靠
 * 真机复现过（路径写死在 pldmgr.h 里，宿主原先测不到）：
 *
 *   ① `.154` 修的：`/get_config` 把 autoload.txt 的**每一行**（含 `!1000`）逗号拼接
 *      回传，旧写回逻辑给**每个** token 都补一行默认延时 → token `!1000` 自己被补成
 *      两行，一次保存行数翻倍。
 *   ② `.155` 修的（真机实测残留）：上一版只按「上一条是不是延时行」判断，于是
 *      **两个载荷名相邻**时仍会补一条。真机复现：写 `!1000,kstuff,shadowmountplus`
 *      读回 `!1000,kstuff,!1000,shadowmountplus`。用户把条目挪到一起、或手删掉中间
 *      那条延时行，存一次就被塞回一条 —— 体感正是「自己多次增加 1 秒延时」。
 *
 * 被测入口就是 HTTP 那条真路：config_handle_set_json()。
 * 路径宏靠 pldmgr.h 的 `#ifndef` 保护指到 /tmp（见 run_host_tests.sh）。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "config.h"
#include "pldmgr.h"

static int checks = 0, fails = 0;
#define ok(cond, ...)                                                          \
    do {                                                                       \
        checks++;                                                              \
        if (cond) {                                                            \
            printf("  ok   " __VA_ARGS__);                                     \
        } else {                                                               \
            fails++;                                                           \
            printf("  FAIL " __VA_ARGS__);                                     \
        }                                                                      \
        printf("\n");                                                          \
    } while (0)

/* ── 桩（config.c 依赖，但测试不关心） ────────────────────────────── */
void pldmgr_log(const char *fmt, ...) { (void)fmt; }
void pldmgr_autoload_abort(void) {}

/* ── 夹具 ──────────────────────────────────────────────────────── */
static void put(const char *content) {
    FILE *f = fopen(AUTOLOAD_CONFIG_PATH, "w");
    if (!f) {
        printf("!! 写不了夹具 %s\n", AUTOLOAD_CONFIG_PATH);
        exit(2);
    }
    if (content) fputs(content, f);
    fclose(f);
}

static const char *get(void) {
    static char buf[8192];
    buf[0] = '\0';
    FILE *f = fopen(AUTOLOAD_CONFIG_PATH, "r");
    if (!f) return buf;
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);
    return buf;
}

/* 走真实入口：/set_config 的 body */
static void set_list(const char *list) {
    char json[4096];
    snprintf(json, sizeof(json), "{\"AUTOLOAD_LIST\":\"%s\"}", list);
    config_handle_set_json(json);
}

static int count_sub(const char *s, const char *needle) {
    int n = 0;
    const char *p = s;
    while ((p = strstr(p, needle))) {
        n++;
        p += strlen(needle);
    }
    return n;
}

int main(void) {
    mkdir(BASE_DATA_DIR, 0777);

    printf("=== ② 相邻载荷名不再被凭空补延时行（.155 的核心） ===\n");
    put("!1000\na.elf\nb.elf\n"); /* 旧文件里 a、b 都已有 */
    set_list("!1000,a.elf,b.elf");
    ok(strcmp(get(), "!1000\na.elf\nb.elf\n") == 0,
       "两个载荷名相邻 → 写回不多任何行（实得 %d 行）", count_sub(get(), "\n"));
    ok(count_sub(get(), "!1000") == 1, "延时行仍是 1 条（实得 %d）",
       count_sub(get(), "!1000"));

    printf("\n=== 幂等：同一列表连存两次，文件逐字节一致 ===\n");
    put("!1000\na.elf\nb.elf\n");
    set_list("!1000,a.elf,b.elf");
    char first[4096];
    snprintf(first, sizeof(first), "%s", get());
    set_list("!1000,a.elf,b.elf");
    set_list("!1000,a.elf,b.elf");
    ok(strcmp(first, get()) == 0, "存三次 == 存一次（逐字节）");

    printf("\n=== ① token `!1000` 不会被自己补成两行（.154 的核心） ===\n");
    put(""); /* 空旧文件 */
    set_list("!1000,!1000,a.elf");
    ok(count_sub(get(), "!1000\n") == 2, "两个 `!1000` token → 还是两行（实得 %d）",
       count_sub(get(), "!1000\n"));
    ok(strcmp(get(), "!1000\n!1000\na.elf\n") == 0, "文件内容逐字节符合预期");

    printf("\n=== 用户新加的载荷才补延时行 ===\n");
    put("!1000\na.elf\n"); /* 旧文件只有 a */
    set_list("!1000,a.elf,NEW.elf");
    ok(strcmp(get(), "!1000\na.elf\n!1000\nNEW.elf\n") == 0,
       "NEW.elf 旧文件里没有 → 前面补一条 `!1000`");

    printf("\n=== 用户删掉中间的延时行后，不会被塞回来 ===\n");
    put("!1000\na.elf\n!1000\nb.elf\n"); /* 原本 a、b 之间有一条 */
    set_list("!1000,a.elf,b.elf");        /* 用户把它删了 */
    ok(strcmp(get(), "!1000\na.elf\nb.elf\n") == 0,
       "删了就是删了（实得：%s）", get());

    printf("\n=== 老配置兜底：没有延时行的裸载荷名仍补一条 ===\n");
    put(""); /* 空旧文件 */
    set_list("a.elf");
    ok(strcmp(get(), "!1000\na.elf\n") == 0,
       "旧文件里也没有 a.elf → 当新加的补 `!1000`（沿用 .60 的格式兜底）");

    printf("\n=== 空 token / 空白 / BOM 容错 ===\n");
    put("!1000\na.elf\n");
    set_list("!1000, ,a.elf,,"); /* 中间和末尾有空 token */
    ok(strcmp(get(), "!1000\na.elf\n") == 0, "空 token 被忽略，不产生空行");
    put("!1000\na.elf\n");
    set_list("\xEF\xBB\xBF!1000, a.elf "); /* BOM + 前后空格 */
    ok(strcmp(get(), "!1000\na.elf\n") == 0, "BOM 与前后空格被 strip 掉");

    printf("\n=== 换行结尾恒定（不多一个空行） ===\n");
    put("!1000\na.elf\n");
    set_list("!1000,a.elf");
    ok(get()[strlen(get()) - 1] == '\n' && count_sub(get(), "\n\n") == 0,
       "每条一行、结尾单换行，没有多余空行");

    printf("\n");
    if (fails == 0) {
        printf("RESULT: PASS (%d checks)\n", checks);
        return 0;
    }
    printf("RESULT: FAIL (%d/%d failed)\n", fails, checks);
    return 1;
}
