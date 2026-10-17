/* [CN-FORK] v0.6.0.179 内置软件磁贴图（src/app_tile.c，**纯文件那一层**）的宿主测试。
 *
 * 为什么值得单独一条：换图标 / 换背景写的是**主机的应用数据库**，而内置软件
 * 没有可写的 sce_sys（资产在只读的 /system_ex/rnps/apps/<TID> 里）。这条链的
 * 判据是「路径对不对、版本号是不是严格递增、底片能不能原样还原」——
 * 三样都只在这个层里看得见，真机上插上电视也看不出「版本号没变所以浏览器吃了缓存」
 * 这种问题（用户只会说「我换了一张，还是老图」）。
 *
 * 只测这一层：把图登记进 app.db 的那部分在 app_tile_write.c，需要真跑 SQL，
 * 由 test/run_host_tests.sh 里 app_visibility 那一关（连表一起造）覆盖。
 * 分层正是为了这里能这么轻：只链 app_tile.c + json_helpers.c + utils.c。
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "app_tile.h"

/* log_server.c 不进来，给一个写 stderr 的桩（与别的宿主测试同手法）。 */
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
            printf("   [%s:%d]\n", __FILE__, __LINE__);                    \
            fails++;                                                       \
        } else {                                                           \
            printf("  ok   ");                                             \
            printf(__VA_ARGS__);                                           \
            printf("\n");                                                  \
        }                                                                  \
    } while (0)

#define ROOT "/tmp/pldmgr_apptile_test"
#define TID "NPXS40037"

static const char PNG[] = "\x89PNG\r\n\x1a\nFAKE-TEST-BYTES";

static void rm_file(const char *p) { unlink(p); }

int main(void) {
    mkdir(ROOT, 0777);

    printf("=== tid 白名单：只认 NPXS + A-Z0-9 ===\n");
    CHECK(app_tile_is_builtin(TID) == 1, "NPXS40037 是内置软件");
    CHECK(app_tile_is_builtin("NPXS00001") == 1, "NPXS00001 也算");
    CHECK(app_tile_is_builtin("PPSA00001") == 0, "游戏编号不算");
    CHECK(app_tile_is_builtin("pps a00001") == 0, "小写/空格不算");
    CHECK(app_tile_is_builtin("NPXS4") == 0, "5 位以下不算");
    CHECK(app_tile_is_builtin("NPXS40037123456789012") == 0, "16 位以上不算");
    CHECK(app_tile_is_builtin("../../etc/passwd") == 0, "路径穿越必须被拒");
    CHECK(app_tile_is_builtin("NPXS4003/../x") == 0, "带斜杠必须被拒");
    CHECK(app_tile_is_builtin(NULL) == 0, "NULL 不算");
    /* 白名单是**路径穿越防护**：目录名直接由 tid 拼出来，放进来一个就能写到别处。 */
    char d[768];
    CHECK(app_tile_dir("../../evil", d, sizeof(d)) != 0, "非法 tid 连目录都算不出来");
    CHECK(app_tile_dir(TID, d, sizeof(d)) == 0 && strstr(d, TID) != NULL,
          "合法 tid 的目录含它自己（%s）", d);

    printf("=== 没设过的时候三张图都「不存在」 ===\n");
    char f[900];
    CHECK(app_tile_icon_file(TID, f, sizeof(f)) != 0, "没设过 → icon0.png 不存在");
    CHECK(app_tile_bg_png_file(TID, f, sizeof(f)) != 0, "没设过 → bg.png 不存在");
    CHECK(app_tile_bg_dds_file(TID, f, sizeof(f)) != 0, "没设过 → pic0.dds 不存在");
    CHECK(app_tile_icon_ver(TID) == 0 && app_tile_bg_ver(TID) == 0, "没设过时版本号是 0");

    printf("=== 落盘 + 严格递增的版本号 ===\n");
    /* 版本号必须是**严格递增的整数**，不能是 mtime：同一秒内连传两次（「我再挑一张」）
     * 会拿到同一个版本号，前端 ?v= 不刷新，用户看到的就是「换图没生效」。 */
    CHECK(app_tile_store_icon(TID, (const unsigned char *)PNG, sizeof(PNG) - 1) == 0,
          "写入图标应成功");
    long long v1 = app_tile_icon_ver(TID);
    CHECK(v1 > 0, "写完之后版本号应大于 0（%lld）", v1);
    CHECK(app_tile_icon_file(TID, f, sizeof(f)) == 0, "写完之后 icon0.png 应存在");
    /* 文件名必须就叫 icon0.png：写进主机那一列的是**我们这份文件的路径**，
     * 而主机原来的值结尾就是 /icon0.png（真机取证），名字对齐才两种解释都成立。 */
    CHECK(strlen(f) > strlen("icon0.png") && strcmp(f + strlen(f) - 9, "icon0.png") == 0,
          "落点文件名必须是 icon0.png（%s）", f);
    CHECK(app_tile_store_icon(TID, (const unsigned char *)PNG, sizeof(PNG) - 1) == 0,
          "同一秒内再写一次也应成功");
    long long v2 = app_tile_icon_ver(TID);
    CHECK(v2 > v1, "第二次的版本号必须更大（%lld > %lld）", v2, v1);

    printf("=== 背景：两份文件（dds 给主机、png 给面板预览）===\n");
    CHECK(app_tile_store_bg(TID, (const unsigned char *)"\0\0\0\0DDS", 7,
                            (const unsigned char *)PNG, sizeof(PNG) - 1) == 0,
          "写入背景应成功");
    CHECK(app_tile_bg_dds_file(TID, f, sizeof(f)) == 0, "pic0.dds 应存在");
    CHECK(strcmp(f + strlen(f) - 8, "pic0.dds") == 0, "落点文件名必须是 pic0.dds（%s）", f);
    CHECK(app_tile_bg_png_file(TID, f, sizeof(f)) == 0, "bg.png 应存在");
    CHECK(app_tile_bg_ver(TID) > 0, "背景版本号应大于 0");
    /* 先落 dds 再落 png：只写进 png 会让「面板里看得到、主机上没有」悄悄成立。 */
    CHECK(app_tile_store_bg(TID, NULL, 0, (const unsigned char *)PNG, sizeof(PNG) - 1) != 0,
          "dds 为空时必须整体失败，不许只留一份 png");

    printf("=== 底片：只留第一次，且能原样读回来 ===\n");
    CHECK(app_tile_orig_read(TID, 0, f, sizeof(f), NULL) != 0, "没留过底片时读应失败");
    CHECK(app_tile_orig_save_once(TID, 0, "/system_ex/rnps/apps/NPXS40037/appdb/default/icon0.png?ts=111")
          == 0, "第一次留底片应成功");
    int is_null = -1;
    CHECK(app_tile_orig_read(TID, 0, f, sizeof(f), &is_null) == 0 && is_null == 0 &&
          strstr(f, "?ts=111") != NULL, "读回原始那一串（%s）", f);
    /* 关键：第二次不许覆盖 —— 那已经不是「原始值」，是我们自己写进去的那串了。 */
    CHECK(app_tile_orig_save_once(TID, 0, "MINE") == 0, "第二次留底片（应被忽略）");
    CHECK(app_tile_orig_read(TID, 0, f, sizeof(f), &is_null) == 0 &&
          strstr(f, "MINE") == NULL && strstr(f, "?ts=111") != NULL,
          "底片必须还是最初那一串（%s）", f);
    /* 内置软件的 pic0Info 出厂是 SQL NULL：底片用一个 0 字节文件表示，
     * 还原时要写成 SQL NULL 而不是空串（写空串主屏会照着一张不存在的图去画）。 */
    CHECK(app_tile_orig_save_once(TID, 1, NULL) == 0, "NULL 也能留底片");
    CHECK(app_tile_orig_read(TID, 1, f, sizeof(f), &is_null) == 0 && is_null == 1,
          "读到 NULL 底片时 is_null 必须为 1");

    printf("=== 删除：文件没了、版本号还得往前走 ===\n");
    CHECK(app_tile_drop_icon(TID) == 0, "删图标应成功");
    CHECK(app_tile_icon_file(TID, f, sizeof(f)) != 0, "删完之后 icon0.png 不该还在");
    CHECK(app_tile_icon_ver(TID) > v2, "删完版本号也要更大（否则浏览器缓存里那张又回来了）");
    CHECK(app_tile_drop_bg(TID) == 0, "删背景应成功");
    CHECK(app_tile_bg_dds_file(TID, f, sizeof(f)) != 0, "pic0.dds 不该还在");
    CHECK(app_tile_bg_png_file(TID, f, sizeof(f)) != 0, "bg.png 不该还在");
    /* 重复删也要成功：调用方要的语义是「盘上不该再有它」，不是「本来就有才成功」。 */
    CHECK(app_tile_drop_icon(TID) == 0, "重复删图标也应成功");

    char p[900];
    /* 清理（脚本层还会 rm -rf 一次，这里只是不留脏文件给下一次跑）。 */
    snprintf(p, sizeof(p), "%s/%s/icon.ver", ROOT, TID);
    rm_file(p);
    snprintf(p, sizeof(p), "%s/%s/bg.ver", ROOT, TID);
    rm_file(p);
    snprintf(p, sizeof(p), "%s/%s/orig_icon0info.txt", ROOT, TID);
    rm_file(p);
    snprintf(p, sizeof(p), "%s/%s/orig_pic0info.txt", ROOT, TID);
    rm_file(p);

    printf("\n%s（%d 项失败）\n", fails ? "FAILED" : "ALL OK", fails);
    return fails ? 1 : 0;
}
