/* [CN-FORK] v0.5.2.145 宿主测试里「全局存档配置」的替身。
 *
 * 为什么是替身而不是把 src/config.c 链进来：config.c 还要 autoload.c（pldmgr_autoload_abort）
 * 和 utils.c 的一串东西，为两个读函数把半个后端拖进每个用例，代价远大于收益。
 * 这里只提供 game_auto.c / game_saves.c 真正引用的那一个符号。
 *
 * 默认行为是「**没有全局配置**」（返回 -1）—— 也就是 `.144` 的老路径：一切以 per-game
 * 为准。这样既有的几十条用例一条都不用改，它们测的还是原来那件事。
 * 想测「全局生效」的用例先调 stub_archive_global_set() 塞一份 JSON 进去，
 * 跑完调 stub_archive_global_clear() 复位（用例之间不能互相串味）。
 *
 * ⚠ 这个替身只实现了 config_read_archive_global；config_write_archive_global 没有被
 * game_auto.c / game_saves.c 引用（写入走 /set_config，只有 http_server.c 那一层会调），
 * 所以这里不需要它 —— 真需要时编译器会立刻报 undefined reference，不会静默漏掉。 */

#include <stddef.h>
#include <string.h>

static char g_stub_ag[4096];
static int  g_stub_ag_on = 0;

int config_read_archive_global(char *out, size_t osz);

/* 塞一份全局配置（json 为 NULL 等于清掉）。返回 0 成功，-1 太长。 */
int stub_archive_global_set(const char *json) {
    if (!json || !json[0]) {
        g_stub_ag_on = 0;
        g_stub_ag[0] = 0;
        return 0;
    }
    size_t n = strlen(json);
    if (n >= sizeof(g_stub_ag)) return -1;
    memcpy(g_stub_ag, json, n + 1);
    g_stub_ag_on = 1;
    return 0;
}

void stub_archive_global_clear(void) {
    g_stub_ag_on = 0;
    g_stub_ag[0] = 0;
}

/* 与 config.c 里那份签名/语义一致：没配置返回 -1（out 置空串），有则返回字节数。 */
int config_read_archive_global(char *out, size_t osz) {
    if (!out || osz < 2) return -1;
    out[0] = 0;
    if (!g_stub_ag_on) return -1;
    size_t n = strlen(g_stub_ag);
    if (n > osz - 1) n = osz - 1;
    memcpy(out, g_stub_ag, n);
    out[n] = 0;
    return (int)n;
}
