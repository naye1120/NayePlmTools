/* [CN-FORK] v0.5.2.117 test/game_mgr_test.c — src/game_mgr.c 的主机回归测试。
 *
 * 起因：真机反馈「游戏管理的列表变空了」（缺陷本身是 .116 引入的）—— /game_list
 * 照样 200，回的却是 {"games":[],"count":0}。不崩、不报错，三道门（make / 主机测试 /
 * lint）全绿，所以是**静默**空掉的，只看接口状态码根本看不出来。
 *
 * 根因：build_game_json 逐字段赋值 enum_arg_t，漏了同一版（v0.5.2.116）才加的
 * want 字段 —— 而 gm_append_item 第一行就是 `if (a->want)`，非空即**每个条目
 * 都早退**，count 恒 0。want 是栈上垃圾：恰好为 0 时列表正常，恰好非 0 时列表
 * 全空，「先好后坏、时好时坏」就是这么来的。
 *
 * 所以本测试的核心手法是**主动播种**（seed_stack）：把接下来那次调用要占的那段栈
 * 显式写成一个合法可读的非空指针，让 bug 必现。不播种的话，指望进程刚起来那点干净
 * 栈碰巧复现，等于没有测试。指针指向 "SEEDSEED1" 而不是 0xAAAA…，是为了让 strcmp
 * 读到合法内存 —— 表现为「列表空」而不是 segfault，断言的失败信息才有意义。
 *
 * 顺带覆盖（这一层以前一条断言都没有）：
 *   ① 列表项字段 tid/name/src/fmt/size/platform/icon/bg，且与 count 一致
 *   ② 60s 快照缓存：invalidate 之后必须真重建，不 invalidate 则回同一份字节
 *   ③ 详情 23 个契约字段；不在枚举里的 tid 回 -1（路由据此 404）
 *   ④ game_mgr_source 与列表共用同一套枚举（gm_lookup 内部 memset，本来就没错）
 *   ⑤ game_mgr_is_scan_path：根本身不算、前缀后必须紧跟 /
 *   ⑥ game_mgr_icon_path 真的走到 test/host_mnt_compat.h 顶出来的 getmntinfo 分支
 *
 * 主机上走不到的（先写清楚，免得当成漏了）：
 *   - game_enum_dump 扫的 GM_SCAN_PATHS 是写死的 PS5 路径，主机上一个都不存在，
 *     所以「dump 来源」那一半枚举测不到，只测 app.db 这一半。
 *   - 沙盒 app0 那段 getmntinfo 由 test/host_mnt_compat.h 顶成「永远 0 条」，
 *     icon 因此恒回落到 /user/app/<TID>/sce_sys（主机上也没有）→ 断言它回 0。
 *   - HTTP 那一层在 http_server.c，主机上没有 microhttpd，只能靠把 gameApi.js
 *     与 mock-server.js 并排读的对照审计（HANDOVER §2.1.2）。
 *
 * 用法：由 test/run_host_tests.sh 拨起，也可单独跑（先跑 make_appdb_fixture.py）。
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

#include "game_mgr.h"
#include "game_title.h"

static int g_checks, g_fails;

#define CHECK(cond, ...)                                                     \
    do {                                                                     \
        g_checks++;                                                          \
        if (!(cond)) {                                                       \
            g_fails++;                                                       \
            printf("  FAIL L%d: ", __LINE__);                                \
            printf(__VA_ARGS__);                                             \
            putchar('\n');                                                   \
        }                                                                    \
    } while (0)

/* log_server.c 的头要 microhttpd.h，主机上没有 —— 这里给个等价实现。
 * 默认静音，要看就 export GM_TEST_VERBOSE=1。 */
void pldmgr_log(const char *fmt, ...) {
    if (!getenv("GM_TEST_VERBOSE")) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

/* 这两个只被 game_mgr_detail_json 拿来做个数，真身要把整个媒体/存档模块连进来，
 * 对本测试没有信息量（那两个模块各自有各自的测试）。返回固定值正好用来断言
 * 「详情确实把这两个数字透出来了」。 */
int game_saves_count(const char *tid) { (void)tid; return 3; }
int game_media_count(const char *tid) { (void)tid; return 7; }

/* [CN-FORK] v0.5.2.118 game_title.c 改名 / 换背景时会把名字与背景登记进主机的
 * app.db / appinfo.db（tile_db_*）。那两条链要真跑 SQL，真身是 src/tile_db.c +
 * third_party/sqlite3 —— 9MB 的合并源码，为「列表为什么变空」这条测试连进来不值。
 * 本测试也压根不走改名（只读列表 / 详情），所以照 pldmgr_log 的老办法给桩：
 * 返回 -1 = 「主机端没写进去」，恰好是 game_title.c 里 best-effort 那条分支，
 * 面板那一层照常算成功 —— 那正是它承诺的行为。
 * 别把这里改成返回 0：那会让「写主机库」看起来成功了，而它一个字节都没写。 */
int tile_db_set_name(const char *tid, const char *new_name, char *msg, size_t msgsz) {
    (void)tid; (void)new_name;
    if (msg && msgsz) snprintf(msg, msgsz, "主机测试桩：未接 app.db");
    return -1;
}
int tile_db_set_bg(const char *tid, const char *dds_path, char *msg, size_t msgsz) {
    (void)tid; (void)dds_path;
    if (msg && msgsz) snprintf(msg, msgsz, "主机测试桩：未接 app.db");
    return -1;
}

/* ══════════ 栈播种：本测试存在的全部理由，见文件头 ══════════ */
static const char g_seed[] = "SEEDSEED1";
static volatile unsigned long g_sink;

static void seed_stack(int depth) {
    volatile unsigned long pad[1024];   /* 8 KB/层 */
    for (int i = 0; i < 1024; i++) pad[i] = (unsigned long)(size_t)g_seed;
    if (depth > 0) seed_stack(depth - 1);
    g_sink += pad[0];                   /* 有读就有写，编译器删不掉 */
}

/* ── 夹具常量：与 test/make_appdb_fixture.py 一一对应，改一个就得改两个 ── */
#define TID_PS5    "PPSA00001"    /* 镜像游戏：mount_img.lnk → .ffpfsc */
#define TID_PS4    "CUSA00002"    /* 真·内置安装 PKG：两个 lnk 都没有 */
#define TID_FOLDER "PPSA00003"    /* 裸目录游戏：mount.lnk → 外置目录，且此刻挂载中 */
#define TID_ABSENT "PPSA99999"   /* 格式合法但不在库里：走 gm_lookup 的「真没找到」 */
#define NAME_PS5   "Test Game One"
#define NAME_FOLDER "Test Game Three"
/* 夹具里 app.db 的行数。写成宏，加一款游戏只改这一处（下面 7 处 count 断言都用它）。 */
#define N_GAMES 3
/* 镜像 / 裸目录游戏的两个路径，与夹具里的字面量一致 —— 断言就是拿它们比字符串。 */
#define IMG_PATH    "/mnt/ext1/" TID_PS5 ".ffpfsc"
#define IMG_RUNTIME "/mnt/shadowmnt/" TID_PS5 "_abcd1234"
#define FOLDER_PATH "/mnt/ext1/" TID_FOLDER "-appFixture"

static char g_buf[262144];
static char g_snap[262144];

static int has(const char *s, const char *sub) { return strstr(s, sub) != NULL; }

/* count 字段（列表尾部那个）。找不到回 -1。 */
static int json_count(const char *s) {
    const char *p = strstr(s, "\"count\":");
    return p ? atoi(p + 8) : -1;
}

/* 数组里实际出现的 tid 条数 —— 与 count 对得上才算数：
 * count 写对了但 JSON 缺条，同样是「前端看着少了一半」。 */
static int json_items(const char *s) {
    const char *g = strstr(s, "\"games\":[");
    if (!g) return -1;
    int n = 0;
    for (const char *p = g; (p = strstr(p, "\"tid\":")) != NULL; p += 6) n++;
    return n;
}

/* 作废 60s 快照 → 播种 → 真重建一次。缺一不可：不作废就恒读缓存，
 * 不播种就复现不出 bug。 */
static int list_fresh(void) {
    game_mgr_invalidate();
    seed_stack(4);
    return game_mgr_list_json(g_buf, sizeof g_buf, 0);
}

int main(void) {
    int n;

    /* ── ① 播种后的列表：不播种就必过的那条断言 ───────────────────────── */
    n = list_fresh();
    CHECK(n > 0, "game_mgr_list_json 返回 %d", n);
    CHECK(has(g_buf, "{\"games\":["), "不是从 {\"games\":[ 开头: %.60s", g_buf);
    CHECK(json_count(g_buf) == N_GAMES, "count=%d（该 %d）", json_count(g_buf), N_GAMES);
    CHECK(json_items(g_buf) == N_GAMES, "数组里实际 %d 个 tid（该 %d）", json_items(g_buf), N_GAMES);
    CHECK(json_count(g_buf) == json_items(g_buf),
          "count=%d 与数组里 %d 个条对不上", json_count(g_buf), json_items(g_buf));

    /* ── ② 单条字段 ───────────────────────────────────────────────────── */
    CHECK(has(g_buf, "{\"tid\":\"" TID_PS5 "\",\"name\":\""), "列表里没有 " TID_PS5);
    CHECK(has(g_buf, "{\"tid\":\"" TID_PS4 "\",\"name\":\""), "列表里没有 " TID_PS4);
    CHECK(has(g_buf, "\"name\":\"" NAME_PS5 "\""), "改名接口盖掉的原名丢了: %.80s", g_buf);
    CHECK(has(g_buf, "\"src\":\"installed\""), "src 不是 installed");
    CHECK(has(g_buf, "\"fmt\":\""), "没有 fmt 字段");
    CHECK(has(g_buf, "\"size\":"), "没有 size 字段");
    /* gm_platform 的口径：CUSA/PPJB 是 PS4，其余按 PS5 —— 与 mock 同一个算法。 */
    CHECK(has(g_buf, "\"platform\":\"PS5\""), "PPSA 没标成 PS5");
    CHECK(has(g_buf, "\"platform\":\"PS4\""), "CUSA 没标成 PS4");
    /* 主机上没设过自定义图，版本号必须是 0 —— 前端据此不去取图，
     * 省掉每款游戏两次白跑的 404。 */
    CHECK(has(g_buf, "\"icon\":0"), "icon 版本号不是 0");
    CHECK(has(g_buf, "\"bg\":0"), "bg 版本号不是 0");

    /* ── ③ 反复播种：bug 是「时好时坏」，一次过了不算过 ───────────────── */
    for (int i = 0; i < 5; i++) {
        n = list_fresh();
        CHECK(n > 0 && json_count(g_buf) == N_GAMES && json_items(g_buf) == N_GAMES,
              "第 %d 次播种后 count=%d items=%d（该 %d/%d）",
              i + 1, json_count(g_buf), json_items(g_buf), N_GAMES, N_GAMES);
    }

    /* ── ④ 真实时序：先开详情、再回列表（这正是真机上「越用越空」的顺序） ── */
    n = game_mgr_detail_json(TID_PS5, g_buf, sizeof g_buf);
    CHECK(n > 0, "detail(%s) 返回 %d", TID_PS5, n);
    n = list_fresh();
    CHECK(n > 0 && json_count(g_buf) == N_GAMES && json_items(g_buf) == N_GAMES,
          "跑过详情之后列表空了：count=%d items=%d",
          json_count(g_buf), json_items(g_buf));

    /* ── ⑤ 详情：23 个契约字段（与 frontend/src/utils/gameApi.js 文件头对齐） ──
     * TID_PS5 在夹具里是**镜像游戏**（mount_img.lnk → .ffpfsc）。 */
    n = game_mgr_detail_json(TID_PS5, g_buf, sizeof g_buf);
    CHECK(n > 0, "detail(%s) 返回 %d", TID_PS5, n);
    CHECK(has(g_buf, "\"tid\":\"" TID_PS5 "\""), "详情没回这个 tid");
    CHECK(has(g_buf, "\"name\":\"" NAME_PS5 "\""), "详情里的名字不对: %.80s", g_buf);
    CHECK(has(g_buf, "\"src\":\"installed\""), "详情 src 不是 installed");
    CHECK(has(g_buf, "\"fmt\":\"ffpfsc\""), "镜像游戏的 fmt 不是 ffpfsc: %.120s", g_buf);
    CHECK(has(g_buf, "\"platform\":\"PS5\""), "详情没有 platform=PS5");
    /* ── 来源类别：**只看物理源**，不看 src ──────────────────────────────
     * 这条断言就是 v0.5.2.127 修的那个 bug 的看门：真机上 ShadowMount 的游戏同样
     * 注册在 app.db 里（src 也是 installed），老实现拿 src 当来源类型，于是 14 款
     * 游戏全被标成「已安装 PKG」。夹具这款 src=installed 但物理源是镜像文件，
     * 必须判成 image —— 只看 src 的实现会在这里红。 */
    CHECK(has(g_buf, "\"source_type\":\"image\""), "镜像游戏的 source_type 不是 image: %.160s", g_buf);
    CHECK(has(g_buf, "\"image_type\":\"ffpfsc\""), "镜像游戏的 image_type 不是 ffpfsc: %.200s", g_buf);
    CHECK(has(g_buf, "\"path\":\"" IMG_PATH "\""), "镜像游戏的物理路径不是那个镜像文件: %.320s", g_buf);
    CHECK(has(g_buf, "\"runtime_path\":\"" IMG_RUNTIME "\""), "镜像游戏的运行时路径不对: %.400s", g_buf);
    /* 镜像游戏不是「已安装 PKG」：installed_pkg 必须 false，且它的物理源可删。 */
    CHECK(has(g_buf, "\"installed_pkg\":false"), "镜像游戏被当成了已安装 PKG: %.480s", g_buf);
    CHECK(has(g_buf, "\"can_manage_source\":true"), "镜像游戏的物理源被判成不可管: %.560s", g_buf);
    /* 这 4 条是**列序**的看门：光断言字段存在是抓不住错位的（错位了字段照样在，
     * 只是值来自另一列）。值全部照 test/make_appdb_fixture.py 的夹具写死。 */
    CHECK(has(g_buf, "\"content_id\":\"up0000-TEST_0000000000000-SAVE00001\""),
          "content_id 读错列了: %.200s", g_buf);
    CHECK(has(g_buf, "\"install_time\":\"2026/01/02 10:11:12\""),
          "install_time 读错列了: %.240s", g_buf);
    CHECK(has(g_buf, "\"last_launch\":\"2026/10/05 16:54:54\""),
          "last_launch 读错列了: %.300s", g_buf);
    CHECK(has(g_buf, "\"appdb_size\":42949672960"), "appdb_size 读错列了");
    CHECK(has(g_buf, "\"can_uninstall\":true"), "uninstallable=1 该给 true");
    /* version 读不到时必须是**空串**：没有 param.sfo（dump 缺文件、权限不足）时
     * char version[40] 从来没被写过，直接进 JSON 就是一段栈上垃圾 —— 乱码还好，
     * 混进控制字节就是非法 JSON，前端 .json() 一抛整个详情弹窗白掉。 */
    CHECK(has(g_buf, "\"version\":\"\""), "version 不是空串（读不到就该是空）: %.240s", g_buf);
    /* mounted = /system_ex/app/<TID>/sce_sys/param.json 存在（照 ShadowMountPlus
     * is_data_mounted）。夹具这款镜像游戏没造那个挂载点 → false。
     * **不能**用「runtime_path 存在」当判据：裸目录游戏的源目录一直都在。 */
    CHECK(has(g_buf, "\"mounted\":false"), "镜像游戏不该是 mounted");
    CHECK(has(g_buf, "\"saves_count\":3"), "saves_count 不是桩值 3");
    CHECK(has(g_buf, "\"media_count\":7"), "media_count 不是桩值 7");
    CHECK(has(g_buf, "\"title_icon_custom\":false"), "没自定义图时该是 false");
    CHECK(has(g_buf, "\"title_bg_custom\":false"), "没自定义图时该是 false");

    /* ── ⑤b 真·内置 PKG（TID_PS4，夹具里两个 lnk 都没有）────────────────── */
    n = game_mgr_detail_json(TID_PS4, g_buf, sizeof g_buf);
    CHECK(n > 0, "detail(%s) 返回 %d", TID_PS4, n);
    CHECK(has(g_buf, "\"source_type\":\"pkg\""), "内置 PKG 的 source_type 不是 pkg: %.160s", g_buf);
    CHECK(has(g_buf, "\"image_type\":\"\""), "内置 PKG 的 image_type 该是空串: %.200s", g_buf);
    CHECK(has(g_buf, "\"installed_pkg\":true"), "内置 PKG 的 installed_pkg 不是 true: %.400s", g_buf);
    CHECK(has(g_buf, "\"can_manage_source\":false"), "内置 PKG 不该有可管的物理源: %.480s", g_buf);
    CHECK(has(g_buf, "\"mounted\":false"), "内置 PKG 不该是 mounted");
    CHECK(has(g_buf, "\"platform\":\"PS4\""), "CUSA 没标成 PS4");
    CHECK(has(g_buf, "\"appdb_size\":17179869184"), "TID_PS4 的 appdb_size 读错列了");

    /* ── ⑤c 裸目录游戏（TID_FOLDER，只有 mount.lnk；且夹具造了 /system_ex 挂载点）── */
    n = game_mgr_detail_json(TID_FOLDER, g_buf, sizeof g_buf);
    CHECK(n > 0, "detail(%s) 返回 %d", TID_FOLDER, n);
    CHECK(has(g_buf, "\"source_type\":\"folder\""), "裸目录游戏的 source_type 不是 folder: %.160s", g_buf);
    CHECK(has(g_buf, "\"image_type\":\"\""), "裸目录游戏的 image_type 该是空串: %.200s", g_buf);
    CHECK(has(g_buf, "\"path\":\"" FOLDER_PATH "\""), "裸目录游戏的物理路径不是源目录: %.320s", g_buf);
    CHECK(has(g_buf, "\"runtime_path\":\"" FOLDER_PATH "\""), "裸目录游戏的运行时路径不对: %.400s", g_buf);
    CHECK(has(g_buf, "\"installed_pkg\":false"), "裸目录游戏被当成了已安装 PKG: %.480s", g_buf);
    CHECK(has(g_buf, "\"can_manage_source\":true"), "裸目录游戏的物理源被判成不可管: %.560s", g_buf);
    /* 这一款夹具**造了** /system_ex/app/<TID>/sce_sys/param.json → 唯一 mounted=true 的样本。
     * 没有它，「已挂载」这条判据写反了也没人发现。 */
    CHECK(has(g_buf, "\"mounted\":true"), "已挂载的游戏没判成 mounted: %.640s", g_buf);
    CHECK(has(g_buf, "\"name\":\"" NAME_FOLDER "\""), "TID_FOLDER 的名字不对: %.80s", g_buf);

    /* not-found：格式合法但不在任何一份枚举里 → -1，路由据此 404。
     * 宁可 404 也不回一份凭空拼的详情。 */
    CHECK(game_mgr_detail_json(TID_ABSENT, g_buf, sizeof g_buf) < 0,
          "查不存在的 tid 居然回了详情: %.80s", g_buf);
    /* 根本不是游戏号 → 也得拒（防路径穿越那道闸在 gm_tid_is_game） */
    CHECK(game_mgr_detail_json("../../etc/passwd", g_buf, sizeof g_buf) < 0,
          "路径穿越式的 tid 居然回了详情");

    /* ── ⑥ game_mgr_source：与列表同一套枚举，gm_lookup 内部 memset ─────
     * src 回的是**来源类别**（pkg / image / folder），不是枚举出处 ——
     * /game_delete 拿它挡 403 EPERM，判错就会把 ShadowMount 游戏的删源全挡死。 */
    {
        char src[16], path[600], runtime[600];
        CHECK(game_mgr_source(TID_PS5, src, sizeof src, path, sizeof path,
                              runtime, sizeof runtime) == 1,
              "source(%s) 没找到", TID_PS5);
        CHECK(!strcmp(src, "image"), "镜像游戏 source 的 src=%s（该 image）", src);
        CHECK(!strcmp(path, IMG_PATH), "镜像游戏 source 的 path=%s（该 %s）", path, IMG_PATH);
        CHECK(!strcmp(runtime, IMG_RUNTIME), "镜像游戏 source 的 runtime=%s（该 %s）",
              runtime, IMG_RUNTIME);
        /* 内置 PKG 才是 pkg —— 这是 /game_delete 唯一该回 403 EPERM 的一类。 */
        CHECK(game_mgr_source(TID_PS4, src, sizeof src, path, sizeof path,
                              runtime, sizeof runtime) == 1,
              "source(%s) 没找到", TID_PS4);
        CHECK(!strcmp(src, "pkg"), "内置 PKG source 的 src=%s（该 pkg）", src);
        CHECK(game_mgr_source(TID_FOLDER, src, sizeof src, path, sizeof path,
                              runtime, sizeof runtime) == 1,
              "source(%s) 没找到", TID_FOLDER);
        CHECK(!strcmp(src, "folder"), "裸目录游戏 source 的 src=%s（该 folder）", src);
        CHECK(!strcmp(path, FOLDER_PATH), "裸目录游戏 source 的 path=%s（该 %s）",
              path, FOLDER_PATH);
        CHECK(game_mgr_source(TID_ABSENT, src, sizeof src, path, sizeof path,
                              runtime, sizeof runtime) == 0,
              "source 查不存在的 tid 居然找到了");
        CHECK(game_mgr_source("../../etc/passwd", src, sizeof src, path, sizeof path,
                              runtime, sizeof runtime) == 0,
              "source 放过了路径穿越式的 tid");
        /* 走完 gm_lookup（它 memset 完把 want 指向 tid）再取列表 —— 这一步
         * 就是真机上把栈留下非空 want 的那个动作。 */
        n = list_fresh();
        CHECK(n > 0 && json_count(g_buf) == N_GAMES,
              "跑过 source 之后列表空了：count=%d", json_count(g_buf));
    }

    /* ── ⑦ game_mgr_is_scan_path：根本身不算、前缀后必须紧跟 / ────────── */
    CHECK(game_mgr_is_scan_path("/mnt/usb0/PPSA00001") == 1, "扫描根之下的子项没认出来");
    CHECK(game_mgr_is_scan_path("/data/homebrew/" TID_PS5) == 1, "没认出 /data/homebrew 之下的子项");
    CHECK(game_mgr_is_scan_path("/mnt/usb0") == 0, "扫描根本身被当成了子项");
    CHECK(game_mgr_is_scan_path("/data/homebrew") == 0, "扫描根本身被当成了子项");
    /* 前缀陷阱：/data/homebrewX 与 /data/homebrew 是两个地方，
     * 前缀后不跟 / 就直接放过的话，删除白名单会放走不该删的目录。 */
    CHECK(game_mgr_is_scan_path("/data/homebrewX/PPSA00001") == 0, "前缀陷阱：没要求跟 /");
    CHECK(game_mgr_is_scan_path("/data/homebrewx") == 0, "前缀陷阱：没要求跟 /");
    CHECK(game_mgr_is_scan_path("/nope/nope") == 0, "完全不相干的路径认成了扫描根");
    CHECK(game_mgr_is_scan_path(NULL) == 0, "NULL 路径没拒绝");

    /* ── ⑧ getmntinfo 那条主机兼容分支（test/host_mnt_compat.h）───────── */
    {
        char out[768];
        out[0] = 'X';
        /* 主机上 /user/app 不存在、沙盒挂载恒 0 条 → 两条候选都落空 → 回 0。
         * 走到这里说明 getmntinfo 分支被走到且没读到野指针。 */
        CHECK(game_mgr_icon_path(TID_PS5, out, sizeof out) == 0,
              "主机上 icon_path 居然找到了");
        CHECK(game_mgr_icon_path("../../etc/passwd", out, sizeof out) == 0,
              "icon_path 放过了路径穿越式的 tid");
    }

    /* ── ⑨ SFO：读不到必须回非 0 且把 out 置空 ───────────────────────── */
    {
        char out[64];
        strcpy(out, "dirty");
        CHECK(game_mgr_sfo_text("/no/such/file.sfo", "TITLE", out, sizeof out) != 0,
              "读不存在的 SFO 居然成功了");
        CHECK(out[0] == '\0', "读失败时 out 没被置空: %s", out);
    }

    /* ── ⑩ 60s 快照：不作废就该回一模一样的字节 ───────────────────────── */
    n = list_fresh();
    CHECK(n > 0 && json_count(g_buf) == N_GAMES, "快照前一次 count=%d", json_count(g_buf));
    memcpy(g_snap, g_buf, (size_t)n + 1);
    seed_stack(4);   /* 再播一次种：若这次没走缓存而是重建，字节该变 */
    {
        int n2 = game_mgr_list_json(g_buf, sizeof g_buf, 0);
        CHECK(n2 == n && memcmp(g_snap, g_buf, (size_t)n) == 0,
              "没作废缓存却回了不一样的字节（n=%d n2=%d）", n, n2);
    }

    /* ── ⑪ .133 force=1 必须跳过 60s 快照直接重建 ────────────────────────
     * 判据：时间戳递进。seed_stack 播种的字节每次重建可能一模一样，所以
     * memcmp != 0 不能当判据（两次播种的重建字节可能恒同）。改用
     * game_mgr_cache_ts()：invalidate 后 ts=0，force=1 必须把它刷成非 0；
     * 紧接着 force=0 必须走缓存（ts 不动、字节恒同），不然改了个形参就静默通过。 */
    {
        game_mgr_invalidate();      /* ts=0, len=0 */
        seed_stack(4);              /* 播种栈，rebuild 会读到垃圾 */
        int nf = game_mgr_list_json(g_buf, sizeof g_buf, 1);
        CHECK(nf > 0 && json_count(g_buf) == N_GAMES,
              "force=1 之后 count 不对（n=%d）", nf);
        time_t ts_after_force = game_mgr_cache_ts();
        CHECK(ts_after_force > 0,
              "force=1 后时间戳仍为 0（没走重建路）");

        /* force=0 紧跟在 force=1 后面：ts 刚刷新，60s 窗口内，必须走缓存。
         * 走缓存 = 字节恒同 + ts 不动。 */
        int nc = game_mgr_list_json(g_buf, sizeof g_buf, 0);
        CHECK(nc == nf, "force=0 在 TTL 内没回刚重建的快照（nc=%d nf=%d）", nc, nf);
        CHECK(game_mgr_cache_ts() == ts_after_force,
              "force=0 缓存命中不该动时间戳");
    }

    printf("game_mgr: %d checks, %d failures\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
