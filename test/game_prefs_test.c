/* [CN-FORK] v0.5.2.164 游戏磁贴「收藏 / 置顶」的宿主测试（src/game_prefs.c）。
 *
 * 为什么必须有这一关：fav / pin 是**三态**入参（-1 = 不动这一项），前端每次只拨
 * 一颗按钮 —— 写错成两态的话，用户点「收藏」会顺手把「置顶」抹掉，界面上表现为
 * 「另一个开关自己弹回去」。这类错在真机上极难复现（要同时盯两个开关），
 * 只能在宿主上把「只拨一颗、另一颗原样保留」钉死。
 *
 * 顺带钉住：tid 校验、两项都关掉时条目被删掉（文件不会越攒越长）、坏文件不崩。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>

#include "game_prefs.h"

/* pldmgr_log 的桩（与 test/app_visibility_test.c 同一手法：log_server.c 不进来）。 */
void pldmgr_log(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

static int fails = 0;
#define CHECK(cond, ...)                                                   \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("  FAIL ");                                             \
            printf(__VA_ARGS__);                                           \
            printf("\n");                                                  \
            fails++;                                                       \
        } else {                                                           \
            printf("  ok   ");                                             \
            printf(__VA_ARGS__);                                           \
            printf("\n");                                                  \
        }                                                                  \
    } while (0)

static char buf[8192];

/* 读整份列表（返回字符串，便于 strstr 断言）。 */
static const char *list(void) {
    int n = game_prefs_list_json(buf, sizeof(buf));
    if (n < 0) { snprintf(buf, sizeof(buf), "<ERR>"); }
    return buf;
}

int main(void) {
    const char *path = GAME_PREFS_PATH;
    printf("夹具 = %s\n", path);

    /* 0) 文件不存在 → 空表，不是错误 */
    unlink(path);
    CHECK(strcmp(list(), "{\"prefs\":{}}") == 0, "没有文件时回空表（实得 %s）", list());

    /* 1) 拨收藏：pin 应为 false（新条目） */
    char msg[192];
    int of = 0, op = 0;
    CHECK(game_prefs_set("PPSA01234", 1, -1, &of, &op, msg, sizeof(msg)) == 0,
          "收藏 PPSA01234 成功（%s）", msg);
    CHECK(of == 1 && op == 0, "回读 fav=1 pin=0（实得 %d/%d）", of, op);
    CHECK(strstr(list(), "\"PPSA01234\":{\"fav\":true,\"pin\":false}") != NULL,
          "列表里 fav=true pin=false（实得 %s）", list());

    /* 2) 只拨置顶：**收藏必须原样保留**（三态的核心） */
    CHECK(game_prefs_set("PPSA01234", -1, 1, &of, &op, msg, sizeof(msg)) == 0,
          "置顶 PPSA01234 成功（%s）", msg);
    CHECK(of == 1 && op == 1, "回读 fav=1 pin=1（实得 %d/%d）", of, op);
    CHECK(strstr(list(), "\"PPSA01234\":{\"fav\":true,\"pin\":true}") != NULL,
          "只拨置顶后收藏仍在（实得 %s）", list());

    /* 3) 只取消收藏：置顶必须原样保留 */
    CHECK(game_prefs_set("PPSA01234", 0, -1, &of, &op, msg, sizeof(msg)) == 0,
          "取消收藏成功（%s）", msg);
    CHECK(of == 0 && op == 1, "回读 fav=0 pin=1（实得 %d/%d）", of, op);

    /* 4) 再取消置顶：条目应被**整条删掉**（文件不会越攒越长） */
    CHECK(game_prefs_set("PPSA01234", -1, 0, &of, &op, msg, sizeof(msg)) == 0,
          "取消置顶成功（%s）", msg);
    CHECK(of == 0 && op == 0, "回读 fav=0 pin=0（实得 %d/%d）", of, op);
    CHECK(strstr(list(), "PPSA01234") == NULL, "两项都关掉后条目被删除（实得 %s）", list());

    /* 5) 多款游戏互不影响 */
    game_prefs_set("PPSA01234", 1, -1, NULL, NULL, msg, sizeof(msg));
    game_prefs_set("CUSA56789", -1, 1, NULL, NULL, msg, sizeof(msg));
    const char *l = list();
    CHECK(strstr(l, "\"PPSA01234\"") && strstr(l, "\"CUSA56789\""),
          "两条并存（实得 %s）", l);

    /* 6) 坏 tid 一律拒绝 */
    CHECK(game_prefs_set("nope", 1, -1, NULL, NULL, msg, sizeof(msg)) != 0,
          "太短/含小写的 tid 被拒（%s）", msg);
    CHECK(game_prefs_set("PPSA0123/", 1, -1, NULL, NULL, msg, sizeof(msg)) != 0,
          "含 '/' 的 tid 被拒（%s）", msg);
    CHECK(game_prefs_set("", 1, -1, NULL, NULL, msg, sizeof(msg)) != 0,
          "空 tid 被拒（%s）", msg);

    /* 7) 两个都不给 = 没有要改的项 */
    CHECK(game_prefs_set("PPSA01234", -1, -1, NULL, NULL, msg, sizeof(msg)) != 0,
          "fav/pin 都不给被拒（%s）", msg);

    /* 8) 文件被写坏 → 当成空表，不崩，之后照常写 */
    {
        FILE *f = fopen(path, "wb");
        if (f) { fputs("{ 这不是 JSON", f); fclose(f); }
        CHECK(strcmp(list(), "{\"prefs\":{}}") == 0, "坏文件当空表（实得 %s）", list());
        CHECK(game_prefs_set("PPSA02222", 1, -1, &of, &op, msg, sizeof(msg)) == 0,
              "坏文件之后仍能写（%s）", msg);
        CHECK(strstr(list(), "\"PPSA02222\":{\"fav\":true,\"pin\":false}") != NULL,
              "写进去的条目可读回（实得 %s）", list());
    }

    printf("\nRESULT: %s（%d 处失败）\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}
