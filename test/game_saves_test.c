/* [CN-FORK] v0.5.2.139 test/game_saves_test.c — src/game_saves.c 的主机回归测试。
 *
 * 夹具由 test/make_game_fixture.py 现造，**按真机实测布局**
 * （/user/home/<uid>/savedata_prospero/<TID>/sdimg_*，PS4 走 savedata/）。
 * 三个路径宏用 -D 指到 /tmp/pldmgr_saves_test —— 与 -DSAFETY_DIR 同一个手法。
 *
 * ⚠ 这个文件曾经整体建立在**错的布局**上（<uid>/savedata/<TID>/<槽>）：测试
 * 全绿，真机上每个游戏的存档列表却永远是空的。夹具必须跟着真机走，不能跟着
 * 代码走 —— 谁改布局，先去真机 FTP 上看一眼 /user/home/<uid>/ 下有什么。
 *
 * 覆盖：
 *   ① 根目录：PS5 落在 savedata_prospero 下，不取只有空 sce_backup* 的用户
 *   ② 列表：一个用户一条；多用户各一条；两处都有时 PS5 优先；kind 由目录决定
 *   ③ PS4：走 savedata/，kind=PS4，.bin 密封密钥也计入
 *   ④ auto 默认值 + **明文密码/令牌永远不外泄**
 *   ⑤ 保存配置：逐字段合并，没传的保持原值
 *   ⑥ 端口既认 JSON 数字也认 "2121" 这种字符串 —— 前端 <input> 出来的是字符串
 *   ⑦ token_set 与 has_token 两个名都得给（前端按 `has_${f.key}` 拼提示）
 *   ⑧ 打包成 zip（源不动）/ 上传整份替换（旧的可回退）/ 删除
 *   ⑧b **整份替换不许换掉存档目录本身**（v0.5.2.149）：目录 st_ino 上传/还原前后
 *      必须一致，且父目录不留 .old_ / .tmp_ 半成品 —— 「游戏启动时拉取没效果」的
 *      根因就是原来那句 `rename(target, old)` 把目录 inode 换掉了
 *   ⑨ 本地快照：立即备份 / 内容没变不新增 / 只改了哪个用户就只新增哪个 /
 *      保留数裁剪 / 一键还原 / 删除 / uid 与 ts 的路径穿越一律拒 /
 *      间隔与保留数的归一化（86400 不能被当成越界回落成 30 分钟）
 *   ⑨c 崩溃留下的 .tmp_* / .rst_* 会被清掉，且**不误删正在写的那一份**
 *   ⑩ **整条快照链必须能在 32KB 线程栈上跑完**（真机连接线程只有 64KB 上下）
 *   ⑪ garlic 兼容导入（`.154`）：逐文件合并 / 裸镜像必须带文件名 /
 *      带目录层级的名字压平且绝不写到存档目录外 / 坏包**整包拒收、一个字节不落**
 *   ⑫ 快照只剩「游戏退出时」这一个触发点（`.154`）—— 定时那条链已删，
 *      这里钉住引擎做决定用的两个查询：快照开关、本地存档最晚写入时刻
 *
 * 测不了的部分（写在前面，免得有人以为是漏了）：
 *   - HTTP 那一层（tid 到底在 query 还是 body）在 http_server.c，主机上没有
 *     microhttpd。这一层靠的是把 gameApi.js 与 mock-server.js 并排读的
 *     对照审计，见 HANDOVER §2.1.2。
 *
 * 用法：由 test/run_host_tests.sh 拨起，也可单独跑（先跑 make_game_fixture.py）。
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h> /* v0.5.2.149：数 .old_* 残留 */
#include <pthread.h>
#include <sys/stat.h>
#include <sys/time.h> /* utimes：⑨c 要把 .tmp_* 的 mtime 拨回过去 */

#include "game_saves.h"
#include "cJSON.h"
#include "zip_io.h" /* ⑪ garlic 导入要现造一个真 zip（与 garlic 的 PS4 包同构） */

/* log_server.c 的头要 microhttpd.h，主机上没有 —— 这里给个等价实现。
 * 默认静音，要看就 export SAVES_TEST_VERBOSE=1。 */
void pldmgr_log(const char *fmt, ...) {
    if (!getenv("SAVES_TEST_VERBOSE")) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

#define ROOT     "/tmp/pldmgr_saves_test"
#define HOME     ROOT "/home"
#define TID_PS5  "PPSA012345"   /* PS5：savedata_prospero 下 */
#define TID_PS4  "CUSA12345"    /* PS4：savedata 下 */
#define TID_NONE "PPSA99999"    /* 谁都没存过 → 列表必须是空的 */

/* 与 test/make_game_fixture.py 里同名常量一一对应，改一个就得改两个。
 * U* 是用户目录名，同时也是 API 里的 save 参数（不再有「槽名」这回事）。 */
#define U2 "00000002"
#define U3 "00000003"
#define U4 "00000004"
#define U5 "00000005"
#define U2_BYTES 1100           /* 3 个 sdimg 之和（含一个 _bu_ 备份） */
#define U2_FILES 3
#define U3_BYTES 400
#define U3_FILES 1
#define U4_BYTES 310            /* sdimg_CUSA12345 + 它的 .bin 密封密钥 */
#define U4_FILES 2
#define U5_BYTES 150            /* savedata_prospero 那份；savedata 下的 777 不该算进来 */
#define U5_FILES 1
#define HIDDEN_USER ".upl_leftover"  /* <GS_HOME> 下的隐藏目录，不是用户 */

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

/* ── JSON 取值小工具（找不到一律给出与「有值」不同的哨兵，避免把 NULL 当字符串） */
static const char *jstr(const char *tag, cJSON *o, const char *k) {
    cJSON *f = o ? cJSON_GetObjectItem(o, k) : NULL;
    (void)tag;
    return (f && cJSON_IsString(f) && f->valuestring) ? f->valuestring : NULL;
}

static long long jnum(cJSON *o, const char *k) {
    cJSON *f = o ? cJSON_GetObjectItem(o, k) : NULL;
    return (f && cJSON_IsNumber(f)) ? (long long)f->valuedouble : -1;
}

/* -1 = 键不存在或不是布尔 */
static int jbool(cJSON *o, const char *k) {
    cJSON *f = o ? cJSON_GetObjectItem(o, k) : NULL;
    if (!f || !cJSON_IsBool(f)) return -1;
    return cJSON_IsTrue(f) ? 1 : 0;
}

static cJSON *find_slot(cJSON *saves, const char *id) {
    if (!saves || !cJSON_IsArray(saves)) return NULL;
    int n = cJSON_GetArraySize(saves);
    for (int i = 0; i < n; i++) {
        cJSON *it = cJSON_GetArrayItem(saves, i);
        const char *v = jstr(NULL, it, "id");
        if (v && !strcmp(v, id)) return it;
    }
    return NULL;
}

/* memmem 是 glibc 扩展（要 _GNU_SOURCE），自己来一个，免得换个平台就编不过。 */
static void *mem_find(const void *hay, size_t hn, const char *needle, size_t nn) {
    if (!hay || !needle || nn == 0 || nn > hn) return NULL;
    const unsigned char *h = (const unsigned char *)hay;
    for (size_t i = 0; i + nn <= hn; i++)
        if (memcmp(h + i, needle, nn) == 0) return (void *)(h + i);
    return NULL;
}

static char g_buf[64 * 1024];

/* 每次都重解析一份（cJSON_Parse 拷的是字节，不会拽着 g_buf 不放） */
static cJSON *get_list(const char *tid) {
    int n = game_saves_list_json(tid, g_buf, sizeof(g_buf) - 1);
    if (n < 0 || n >= (int)sizeof(g_buf) - 1) return NULL;
    g_buf[n] = '\0';
    return cJSON_Parse(g_buf);
}

static int exists_reg(const char *p) {
    struct stat st;
    return stat(p, &st) == 0 && S_ISREG(st.st_mode);
}

/* ── v0.5.2.149 的判据：**目录本身不许被换掉** ─────────────────────────────
 * gs_swap_dir 原来是 `rename(target, old)` 把整个存档目录换成一个新 inode。
 * 真机上「游戏启动 → 引擎 2 秒后拉取」那条路里，游戏已经把存档目录绑住了，
 * 换掉 inode = 正在跑的游戏永远看到旧目录（手动拉取好用、启动时拉取没效果）。
 * 所以这里盯的是**目录的 st_ino**，不是内容 —— 内容对不对是另外几条断言的事。
 * 真机同一判据走 FTP 的 MLSD unique（见 _ftp_probe/verify149_inode.py）。 */
static unsigned long long ino_of_dir(const char *p) {
    struct stat st;
    if (stat(p, &st) != 0 || !S_ISDIR(st.st_mode)) return 0;
    return (unsigned long long)st.st_ino;
}

/* 数一个目录下还剩几个 .old_* / .tmp_* / .rst_* 半成品（正常必须是 0） */
static int count_temp_in(const char *dir) {
    DIR *d = opendir(dir);
    if (!d) return -1;
    struct dirent *e;
    int n = 0;
    while ((e = readdir(d)) != NULL) {
        if (!strncmp(e->d_name, ".old_", 5) || !strncmp(e->d_name, ".tmp_", 5) ||
            !strncmp(e->d_name, ".rst_", 5))
            n++;
    }
    closedir(d);
    return n;
}

/* 造一个内容为 n 个 'x' 的文件（快照用例要「改一下源存档」） */
static void put_blob(const char *path, int n) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return;
    char b[64];
    memset(b, 'x', sizeof(b));
    while (n > 0) {
        int k = n > (int)sizeof(b) ? (int)sizeof(b) : n;
        if (write(fd, b, (size_t)k) != k) break;
        n -= k;
    }
    close(fd);
}

/* 把整个文件读进 out（返回字节数；-1 = 打不开 / 空）。
 * [CN-FORK] v0.5.2.154 ⑪ garlic 导入要往内存里造一个真 zip 再喂进去 ——
 * 那是二进制，不能用字符串那套。 */
static long read_all(const char *p, char *out, size_t cap) {
    int fd = open(p, O_RDONLY);
    if (fd < 0) return -1;
    ssize_t n = read(fd, out, cap);
    close(fd);
    return n > 0 ? (long)n : -1;
}

/* 在 snapshots[] 里找第 skip 个属于 uid 的条目（列表是新的在前，skip=0 即最新那份） */
static cJSON *find_snap(cJSON *snaps, const char *uid, int skip) {
    if (!snaps || !cJSON_IsArray(snaps)) return NULL;
    int n = cJSON_GetArraySize(snaps);
    for (int i = 0; i < n; i++) {
        cJSON *it = cJSON_GetArrayItem(snaps, i);
        const char *u = jstr(NULL, it, "uid");
        if (u && !strcmp(u, uid) && skip-- <= 0) return it;
    }
    return NULL;
}

static int count_snap(cJSON *snaps, const char *uid) {
    int n = 0;
    if (!snaps || !cJSON_IsArray(snaps)) return 0;
    for (int i = 0; i < cJSON_GetArraySize(snaps); i++) {
        const char *u = jstr(NULL, cJSON_GetArrayItem(snaps, i), "uid");
        if (u && !strcmp(u, uid)) n++;
    }
    return n;
}

/* ── ⑩ 小栈：整条快照链必须能在 32KB 线程栈上跑完 ───────────────────────────
 *
 * 真机 MHD 起的是 MHD_USE_THREAD_PER_CONNECTION —— 每条连接一条 pthread 线程，
 * 栈是 pthread 的默认值，而那个默认值只有 64KB 上下。`.141` 之前这条链要 ~55KB：
 *   http_on_request(13.1KB) → gr_dispatch_post(3.4KB) → game_saves_snap_action →
 *   game_saves_snap_run_now(11.9KB，gs_root_t[16]) → gs_snap_take(5.0KB) →
 *   gs_copy_tree(2.3KB) → gs_copy_file(32.9KB，char buf[32*1024])
 * 于是「点立即备份」直接把整个面板打死（curl exit 52、/version 变 000）。
 *
 * 宿主上量出来的临界点：这条链要 >56KB，**≤56KB 一律 SIGSEGV、≥64KB 才活**。
 * 这条用例取 32KB —— 比临界点还紧一档，只要有人再往这条路径上放一个大数组
 * （或者把哪个 gs_root_t 数组挪回栈上），这里立刻崩，不用等真机。
 *
 * ⚠ 宿主默认跑在主线程的 8MB 栈上，这条路径在那边**根本不构成压力** ——
 * 这就是为什么 `.141` 那个 bug 宿主 136 项全绿、真机一按就死。所以必须
 * 显式起一条小栈线程，不能只在 main 里调一遍。 */
#define SMALL_STACK (32u * 1024u)

static char g_ss_msg[200], g_ss_msg2[200];
static int  g_ss_made = -999, g_ss_restore = -999;

static void *small_stack_worker(void *arg) {
    (void)arg;
    g_ss_made = game_saves_snap_run_now(TID_PS5, g_ss_msg, sizeof(g_ss_msg));

    /* 还原那条链走的是同一个复制函数、方向相反，一并压一遍。 */
    if (game_saves_list_json(TID_PS5, g_buf, sizeof(g_buf)) > 0) {
        cJSON *o = cJSON_Parse(g_buf);
        cJSON *s = o ? find_snap(cJSON_GetObjectItem(o, "snapshots"), U2, 0) : NULL;
        const char *ts = s ? jstr(NULL, s, "ts") : NULL;
        if (ts) {
            char body[256];
            snprintf(body, sizeof(body),
                     "{\"tid\":\"" TID_PS5 "\",\"action\":\"restore\","
                     "\"uid\":\"" U2 "\",\"ts\":\"%s\"}", ts);
            g_ss_restore = game_saves_snap_action(body, strlen(body), g_buf,
                                                  sizeof(g_buf),
                                                  g_ss_msg2, sizeof(g_ss_msg2));
        }
        cJSON_Delete(o);
    }
    return NULL;
}

int main(void) {
    /* 夹具每次都会 rm -rf 重建，但**单独重跑测试**时上次的 auto 配置还在
     * —— 「默认值」那段断言就得从干净状态起，所以这里自己先清掉。
     * 快照目录同理：夹具不管它（它不在 home 下），攒着会让「该 2 条」的断言漂。 */
    unlink(GAME_SAVES_AUTO_PATH);
    system("rm -rf " GAME_SAVES_SNAP_DIR);

    char root[640];
    char msg[256];

    /* ── ① 根目录：PS5 存档在 savedata_prospero 下 ───────────────────────── *
     * 老代码只找 <uid>/savedata/<TID> —— 真机上那里只有空的 sce_backup*，
     * 于是每个游戏都 local_path="" / saves=[]，整个存档功能等于没接上。 */
    memset(root, 0, sizeof(root));
    CHECK(game_saves_root(TID_PS5, root, sizeof(root)) == 0, "game_saves_root 失败");
    CHECK(strstr(root, "/savedata_prospero/") != NULL,
          "PS5 存档根没落在 savedata_prospero 下：%s"
          " —— 这就是「列表永远空」的根因", root);
    CHECK(strstr(root, "/" U2 "/") != NULL,
          "根目录该取 uid 升序第一个：%s", root);
    CHECK(strstr(root, "/00000001/") == NULL,
          "根目录选到了只有空 sce_backup* 的用户：%s", root);

    /* 找不到 tid 时应当明确失败，而不是空手回 0 */
    CHECK(game_saves_root(TID_NONE, root, sizeof(root)) < 0,
          "不存在的 tid 居然找到了根目录");

    /* ── ② PS5 列表：一个用户一条，多用户各一条 ─────────────────────────── */
    cJSON *o = get_list(TID_PS5);
    CHECK(o != NULL, "list json 解析失败: %s", g_buf);
    if (o) {
        const char *tid = jstr(NULL, o, "tid");
        CHECK(tid && !strcmp(tid, TID_PS5), "tid 对不上: %s", tid ? tid : "(null)");

        const char *lp = jstr(NULL, o, "local_path");
        CHECK(lp && strstr(lp, "/savedata_prospero/"),
              "local_path 没指向 savedata_prospero: %s", lp ? lp : "(null)");

        cJSON *autoo = cJSON_GetObjectItem(o, "auto");
        CHECK(cJSON_IsObject(autoo), "auto 不是对象");
        cJSON *saves = cJSON_GetObjectItem(o, "saves");
        CHECK(cJSON_IsArray(saves), "saves 不是数组");
        CHECK(cJSON_GetArraySize(saves) == 3,
              "条目数 = %d（该 3：三个用户都存过这个游戏）",
              cJSON_GetArraySize(saves));

        /* 主用户：整目录递归统计（含 _bu_ 备份）、kind 由所在目录决定 */
        cJSON *u2 = find_slot(saves, U2);
        CHECK(u2 != NULL, "用户 %s 没被列出", U2);
        if (u2) {
            CHECK(jnum(u2, "size") == U2_BYTES,
                  "%s size=%lld（该 %d）", U2, jnum(u2, "size"), U2_BYTES);
            CHECK(jnum(u2, "files") == U2_FILES,
                  "%s files=%lld（该 %d）", U2, jnum(u2, "files"), U2_FILES);
            const char *k = jstr(NULL, u2, "kind");
            CHECK(k && !strcmp(k, "PS5"), "%s 该是 PS5，实际 %s", U2, k ? k : "(null)");
            const char *nm = jstr(NULL, u2, "name");
            CHECK(nm && !strcmp(nm, U2), "%s name 该是 uid，实际 %s", U2, nm ? nm : "(null)");
            const char *mt = jstr(NULL, u2, "mtime");
            CHECK(mt && mt[0], "mtime 没给出");
        }

        /* 第二个用户：同一个游戏也必须单独一条，不能被「取体积最大」盖掉 */
        cJSON *u3 = find_slot(saves, U3);
        CHECK(u3 != NULL, "第二个用户 %s 被吞了 —— 多用户必须各一条", U3);
        if (u3) {
            CHECK(jnum(u3, "size") == U3_BYTES,
                  "%s size=%lld（该 %d）", U3, jnum(u3, "size"), U3_BYTES);
            CHECK(jnum(u3, "files") == U3_FILES,
                  "%s files=%lld（该 %d）", U3, jnum(u3, "files"), U3_FILES);
        }

        /* 两处都有的用户：只取 savedata_prospero 那条 */
        cJSON *u5 = find_slot(saves, U5);
        CHECK(u5 != NULL, "用户 %s 没被列出", U5);
        if (u5) {
            CHECK(jnum(u5, "size") == U5_BYTES,
                  "%s size=%lld（该 %d）—— 把 savedata 下那份也算进来了？"
                  "两处都有时该只取 savedata_prospero", U5, jnum(u5, "size"), U5_BYTES);
        }

        CHECK(find_slot(saves, HIDDEN_USER) == NULL,
              "隐藏目录被当成用户列出来了");

        /* count 与 saves 长度必须同口径 —— /game_detail 的 saves_count 就是它 */
        CHECK(game_saves_count(TID_PS5) == 3,
              "count=%d（该 3）", game_saves_count(TID_PS5));
        CHECK(game_saves_count(TID_NONE) == 0,
              "不存在的 tid count=%d（该 0）", game_saves_count(TID_NONE));

        /* ── ④ auto 默认值 + 明文不外泄 ──────────────────────────────── */
        CHECK(jbool(autoo, "enabled") == 0, "enabled 默认该 false");
        const char *mode = jstr(NULL, autoo, "mode");
        /* [CN-FORK] v0.5.2.142 默认协议从 smb 改成 ftp：SDK 那份 libcurl 8.18.0
         * 编了 http/https/ftp/file/dict/imap/ldap/pop3/smtp，**没有 smb** ——
         * 默认成 smb 等于默认一个点了保存也传不出去的协议。 */
        CHECK(mode && !strcmp(mode, "ftp"), "mode 默认该 ftp，实际 %s", mode ? mode : "(null)");
        CHECK(jbool(autoo, "on_launch") == 1, "on_launch 默认该 true");
        CHECK(jbool(autoo, "on_exit") == 1, "on_exit 默认该 true");

        cJSON *smb = cJSON_GetObjectItem(autoo, "smb");
        CHECK(cJSON_IsObject(smb), "smb 段不是对象");
        if (smb) {
            CHECK(jbool(smb, "has_password") == 0, "还没存过密码，has_password 该 false");
            CHECK(cJSON_GetObjectItem(smb, "password") == NULL,
                  "密码明文漏进响应体了");
        }

        cJSON *ftp = cJSON_GetObjectItem(autoo, "ftp");
        CHECK(cJSON_IsObject(ftp), "ftp 段不是对象");
        if (ftp) {
            CHECK(jnum(ftp, "port") == 21, "port 默认该 21，实际 %lld", jnum(ftp, "port"));
            CHECK(cJSON_GetObjectItem(ftp, "password") == NULL,
                  "密码明文漏进响应体了");
        }

        cJSON *http = cJSON_GetObjectItem(autoo, "http");
        CHECK(cJSON_IsObject(http), "http 段不是对象");
        if (http) {
            CHECK(jbool(http, "token_set") == 0, "token_set 默认该 false");
            CHECK(jbool(http, "has_token") == 0, "has_token 默认该 false");
            CHECK(cJSON_GetObjectItem(http, "token") == NULL, "令牌明文漏进响应体了");
        }
        cJSON_Delete(o);
    }

    /* ── ③ PS4：走 savedata/，kind=PS4，.bin 密封密钥也计入 ─────────────── */
    cJSON *o4 = get_list(TID_PS4);
    CHECK(o4 != NULL, "PS4 list json 解析失败: %s", g_buf);
    if (o4) {
        cJSON *saves = cJSON_GetObjectItem(o4, "saves");
        CHECK(cJSON_IsArray(saves), "PS4 saves 不是数组");
        CHECK(cJSON_GetArraySize(saves) == 1,
              "PS4 条目数 = %d（该 1）", cJSON_GetArraySize(saves));
        cJSON *u4 = find_slot(saves, U4);
        CHECK(u4 != NULL, "PS4 用户 %s 没被列出", U4);
        if (u4) {
            CHECK(jnum(u4, "size") == U4_BYTES,
                  "%s size=%lld（该 %d，含 .bin 密封密钥 —— 少了它还原不回去）",
                  U4, jnum(u4, "size"), U4_BYTES);
            CHECK(jnum(u4, "files") == U4_FILES,
                  "%s files=%lld（该 %d）", U4, jnum(u4, "files"), U4_FILES);
            const char *k = jstr(NULL, u4, "kind");
            CHECK(k && !strcmp(k, "PS4"), "%s 该是 PS4（它在 savedata/ 下），实际 %s",
                  U4, k ? k : "(null)");
        }
        cJSON_Delete(o4);
    }

    /* ── ⑤⑥⑦ 保存配置 ──────────────────────────────────────────────────── *
     * port 故意填**字符串**：前端 <input> 的 onChange 存的是 e.target.value，
     * 出来永远是字符串，只认 JSON 数字的后端会把它整个丢掉 —— 这一条就是那次的回归。 */
    const char *body =
        "{\"tid\":\"" TID_PS4 "\",\"enabled\":true,\"mode\":\"ftp\","
        "\"on_launch\":false,\"on_exit\":false,"
        "\"smb\":{\"host\":\"10.0.0.9\",\"share\":\"ps5save\",\"user\":\"ps5\","
        "\"password\":\"s3cr3t\"},"
        "\"ftp\":{\"host\":\"10.0.0.9\",\"port\":\"2121\",\"path\":\"/ps5save\"},"
        "\"http\":{\"url\":\"http://10.0.0.9:8099/save\",\"token\":\"tok-abc\"}}";
    CHECK(game_saves_auto_set(TID_PS4, body, strlen(body), msg, sizeof(msg)) == 0,
          "auto_set 失败: %s", msg);

    o = get_list(TID_PS4);
    CHECK(o != NULL, "保存后 list 失败");
    if (o) {
        cJSON *autoo = cJSON_GetObjectItem(o, "auto");
        CHECK(jbool(autoo, "enabled") == 1, "enabled 没存上");
        const char *m1 = jstr(NULL, autoo, "mode");
        CHECK(m1 && !strcmp(m1, "ftp"), "mode 没存上（%s）", m1 ? m1 : "(null)");
        CHECK(jbool(autoo, "on_launch") == 0, "on_launch 没存上");
        CHECK(jbool(autoo, "on_exit") == 0, "on_exit 没存上");

        cJSON *smb = cJSON_GetObjectItem(autoo, "smb");
        if (smb) {
            const char *h = jstr(NULL, smb, "host");
            CHECK(h && !strcmp(h, "10.0.0.9"), "smb.host 没存上（%s）", h ? h : "(null)");
            CHECK(jbool(smb, "has_password") == 1, "存了密码但 has_password 还是 false");
            CHECK(cJSON_GetObjectItem(smb, "password") == NULL,
                  "密码明文漏进响应体了");
        }

        cJSON *ftp = cJSON_GetObjectItem(autoo, "ftp");
        if (ftp) {
            CHECK(jnum(ftp, "port") == 2121,
                  "字符串端口 \"2121\" 没存进去（得到 %lld）—— 前端 <input> 出来的"
                  "就是字符串，只认 JSON 数字会把用户填的端口整个丢掉", jnum(ftp, "port"));
            const char *h = jstr(NULL, ftp, "host");
            CHECK(h && !strcmp(h, "10.0.0.9"), "ftp.host 没存上（%s）", h ? h : "(null)");
        }

        cJSON *http = cJSON_GetObjectItem(autoo, "http");
        if (http) {
            CHECK(jbool(http, "token_set") == 1, "token_set 没存上");
            CHECK(jbool(http, "has_token") == 1,
                  "has_token 没给 —— 前端按 `has_${f.key}` 拼提示，缺了它"
                  "令牌栏永远不显示『已保存过』");
            CHECK(cJSON_GetObjectItem(http, "token") == NULL, "令牌明文漏进响应体了");
        }
        cJSON_Delete(o);
    }

    /* 留空即不修改：整段没传就整段保持，smb 段一个字没提也得原样活着 */
    const char *body2 = "{\"tid\":\"" TID_PS4 "\",\"enabled\":false}";
    CHECK(game_saves_auto_set(TID_PS4, body2, strlen(body2), msg, sizeof(msg)) == 0,
          "auto_set(2) 失败: %s", msg);
    o = get_list(TID_PS4);
    if (o) {
        cJSON *autoo = cJSON_GetObjectItem(o, "auto");
        CHECK(jbool(autoo, "enabled") == 0, "enabled 没被覆盖");
        const char *m2 = jstr(NULL, autoo, "mode");
        CHECK(m2 && !strcmp(m2, "ftp"), "没传 mode 却被改了（%s）", m2 ? m2 : "(null)");
        cJSON *smb = cJSON_GetObjectItem(autoo, "smb");
        if (smb) {
            const char *h = jstr(NULL, smb, "host");
            CHECK(h && !strcmp(h, "10.0.0.9"),
                  "smb 整段没传却被清空（%s）", h ? h : "(null)");
            CHECK(jbool(smb, "has_password") == 1, "密码被误清了");
        }
        cJSON *ftp = cJSON_GetObjectItem(autoo, "ftp");
        CHECK(ftp && jnum(ftp, "port") == 2121, "端口被误清了（%lld）",
              ftp ? jnum(ftp, "port") : -1);
        cJSON_Delete(o);
    }

    /* 数字端口照样认；填空串 = 留空即不修改 */
    const char *body3 = "{\"tid\":\"" TID_PS4 "\",\"ftp\":{\"port\":2122}}";
    CHECK(game_saves_auto_set(TID_PS4, body3, strlen(body3), msg, sizeof(msg)) == 0,
          "auto_set(3) 失败: %s", msg);
    const char *body4 = "{\"tid\":\"" TID_PS4 "\",\"ftp\":{\"port\":\"\"}}";
    CHECK(game_saves_auto_set(TID_PS4, body4, strlen(body4), msg, sizeof(msg)) == 0,
          "auto_set(4) 失败: %s", msg);
    o = get_list(TID_PS4);
    if (o) {
        cJSON *ftp = cJSON_GetObjectItem(cJSON_GetObjectItem(o, "auto"), "ftp");
        CHECK(ftp && jnum(ftp, "port") == 2122,
              "数字端口没存进去（%lld）", ftp ? jnum(ftp, "port") : -1);
        cJSON_Delete(o);
    }

    /* 非法 tid 一律拒，别让它落盘 */
    CHECK(game_saves_auto_set("bad tid", body2, strlen(body2), msg, sizeof(msg)) < 0,
          "非法 tid 没被拒");

    /* ── 不存在的 tid：200 + 空列表，不是报错 ───────────────────────────── */
    o = get_list(TID_NONE);
    CHECK(o != NULL, "不存在的 tid 该照样回一份空列表");
    if (o) {
        const char *lp = jstr(NULL, o, "local_path");
        CHECK(lp && !lp[0], "不存在的 tid local_path 该是空串（%s）", lp ? lp : "(null)");
        cJSON *saves = cJSON_GetObjectItem(o, "saves");
        CHECK(cJSON_GetArraySize(saves) == 0, "不存在的 tid 却列出了 %d 条",
              cJSON_GetArraySize(saves));
        cJSON_Delete(o);
    }

    /* ── ⑧ 打包：整份存档下载 ───────────────────────────────────────────── */
    const char *zpath = ROOT "/out.zip";
    unlink(zpath);
    /* [CN-FORK] v0.5.2.147 补第 4 个实参 `out_files`。`.146` 那份未提交的 WIP 给
     * game_saves_pack 加了 `long long *out_files`（推送时写 <uid>.meta.json 用），
     * 但**没同步改这条调用** —— 于是整个 game_saves 关卡直接编不过、run_host_tests.sh
     * 恒 `overall: FAIL`。这里只补参数把它恢复成可编译，不动本关原有的断言口径。 */
    CHECK(game_saves_pack(TID_PS5, U2, zpath, NULL, msg, sizeof(msg)) == 0,
          "打包失败: %s", msg);
    CHECK(exists_reg(zpath), "zip 没落盘");
    {
        struct stat st;
        if (stat(zpath, &st) == 0) {
            CHECK(st.st_size > 22, "zip 太小: %lld", (long long)st.st_size);
            char *zb = (char *)malloc((size_t)st.st_size + 1);
            int fd = open(zpath, O_RDONLY);
            CHECK(fd >= 0, "zip 打不开");
            if (fd >= 0 && zb) {
                ssize_t got = read(fd, zb, (size_t)st.st_size);
                if (got > 0) {
                    /* STORE 不压缩，文件名是明文，够用来认内容 */
                    CHECK(memcmp(zb, "PK\x03\x04", 4) == 0, "不是 zip 本地文件头");
                    /* 顶层目录用 tid 不是 uid —— 包该像「这个游戏的存档」 */
                    CHECK(mem_find(zb, (size_t)got, TID_PS5, strlen(TID_PS5)) != NULL,
                          "zip 里没有顶层目录 %s（头几个字节: %.60s）", TID_PS5, zb);
                    CHECK(mem_find(zb, (size_t)got, "sdimg_STARBOARD", 15) != NULL,
                          "zip 里没有 sdimg_STARBOARD");
                    CHECK(mem_find(zb, (size_t)got, "sdimg_sce_bu_sdmemory", 20) != NULL,
                          "zip 里没有 _bu_ 备份 —— 整目录打包漏了它，还原会缺件");
                }
                close(fd);
            }
            free(zb);
        }
    }
    /* 打包过之后**源必须还活着**（边下边把自己的源写脏是当初记过的坑） */
    CHECK(exists_reg(HOME "/" U2 "/savedata_prospero/" TID_PS5 "/sdimg_STARBOARD"),
          "打包把源文件弄没了");
    unlink(zpath);

    /* ── ⑧' 上传：整份替换（旧内容先留一手，成功后清掉；**目录本身不换**）─────
     * 这一节同时是 v0.5.2.149 的回归守卫：拉取/上传/还原三条路都走 gs_swap_dir，
     * 而「换掉目录 inode」正是「游戏启动时拉取没效果」的根因。 */
    const char *updir = HOME "/" U5 "/savedata_prospero/" TID_PS5;
    unsigned long long ino_before = ino_of_dir(updir);
    CHECK(ino_before != 0, "⑧' 前置：目标存档目录不存在（%s）", updir);

    long long uf = 0, usz = 0;
    const unsigned char raw[] = "hello saves";
    CHECK(game_saves_upload(TID_PS5, U5, raw, sizeof(raw) - 1,
                            &uf, &usz, msg, sizeof(msg)) == 0,
          "上传失败: %s", msg);
    CHECK(uf == 1, "上传该落 1 个文件，实际 %lld", uf);
    CHECK(usz == (long long)(sizeof(raw) - 1), "上传 size=%lld（该 %zu）",
          usz, sizeof(raw) - 1);
    CHECK(game_saves_count(TID_PS5) == 3, "上传后条目数=%d（该 3：用户数不变）",
          game_saves_count(TID_PS5));
    CHECK(exists_reg(HOME "/" U5 "/savedata_prospero/" TID_PS5 "/data.bin"),
          "上传的文件没落到目标用户下");
    CHECK(!exists_reg(HOME "/" U5 "/savedata_prospero/" TID_PS5 "/sdimg_STARBOARD"),
          "整份替换没生效 —— 旧文件还在（该被换掉）");
    CHECK(exists_reg(HOME "/" U2 "/savedata_prospero/" TID_PS5 "/sdimg_STARBOARD"),
          "上传把另一个用户的存档删了");
    /* ★ 本节的要害：目录 inode 必须原封不动。
     * 变异对照（把 gs_swap_dir 改回 `rename(target, old)`）这一条必红。 */
    CHECK(ino_of_dir(updir) == ino_before,
          "存档**目录本身**被换掉了（ino %llu -> %llu）—— 跑着的游戏会一直看到旧目录",
          ino_before, ino_of_dir(updir));
    CHECK(count_temp_in(HOME "/" U5 "/savedata_prospero") == 0,
          "父目录里留了 .old_*/.tmp_* 半成品（%d 个）",
          count_temp_in(HOME "/" U5 "/savedata_prospero"));

    /* 主机上不存在的用户：拒绝，不许瞎建 */
    CHECK(game_saves_upload(TID_PS5, "99999999", raw, sizeof(raw) - 1,
                            &uf, &usz, msg, sizeof(msg)) < 0,
          "给不存在的用户上传居然成功了");

    /* ── ⑧'' 删除：删掉这个用户的整份存档 ──────────────────────────────── */
    CHECK(game_saves_remove(TID_PS5, "no-such-user", msg, sizeof(msg)) < 0,
          "删不存在的用户居然成功了");
    CHECK(game_saves_remove(TID_PS5, U3, msg, sizeof(msg)) == 0,
          "删除失败: %s", msg);
    CHECK(game_saves_count(TID_PS5) == 2, "删完条目数=%d（该 2）",
          game_saves_count(TID_PS5));
    CHECK(exists_reg(HOME "/" U2 "/savedata_prospero/" TID_PS5 "/sdimg_STARBOARD"),
          "删一个用户把另一个用户的存档也带走了");
    CHECK(!exists_reg(HOME "/" U3 "/savedata_prospero/" TID_PS5 "/sdimg_STARBOARD"),
          "目标用户的存档没被删掉");

    /* ── ⑨ 本地快照（v0.5.2.140）────────────────────────────────────────── *
     * 走到这里 U3 已经被上一步删掉，「有存档的用户」是 U2 与 U5 两个。 */
    char obuf[2048];
    char ts5[24] = "", ts2b[24] = "";

    /* 默认值：没配过就是关的 */
    o = get_list(TID_PS5);
    CHECK(o != NULL, "快照段 list 失败");
    if (o) {
        cJSON *snap = cJSON_GetObjectItem(cJSON_GetObjectItem(o, "auto"), "snap");
        CHECK(cJSON_IsObject(snap), "auto.snap 段不是对象");
        if (snap) {
            CHECK(jbool(snap, "enabled") == 0, "snap.enabled 默认该 false");
            CHECK(jnum(snap, "interval") == GAME_SAVES_SNAP_INTERVAL_DEF,
                  "snap.interval 默认该 %d，实际 %lld",
                  GAME_SAVES_SNAP_INTERVAL_DEF, jnum(snap, "interval"));
            CHECK(jnum(snap, "keep") == GAME_SAVES_SNAP_KEEP_DEF,
                  "snap.keep 默认该 %d，实际 %lld",
                  GAME_SAVES_SNAP_KEEP_DEF, jnum(snap, "keep"));
        }
        cJSON *sns = cJSON_GetObjectItem(o, "snapshots");
        CHECK(cJSON_IsArray(sns), "snapshots 不是数组");
        CHECK(cJSON_GetArraySize(sns) == 0, "还没做过快照却列出了 %d 条",
              cJSON_GetArraySize(sns));
        CHECK(jnum(o, "snap_used") == 0, "还没做过快照 snap_used 却是 %lld",
              jnum(o, "snap_used"));
        cJSON_Delete(o);
    }

    /* 立即备份：两个用户各一份 */
    int made = game_saves_snap_run_now(TID_PS5, msg, sizeof(msg));
    CHECK(made == 2, "立即备份该生成 2 份（U2/U5），实际 %d（%s）", made, msg);

    o = get_list(TID_PS5);
    if (o) {
        cJSON *sns = cJSON_GetObjectItem(o, "snapshots");
        CHECK(cJSON_GetArraySize(sns) == 2, "快照该 2 条，实际 %d", cJSON_GetArraySize(sns));
        CHECK(jnum(o, "snap_used") > 0, "snap_used 还是 0");
        cJSON *s_u2 = find_snap(sns, U2, 0);
        cJSON *s_u5 = find_snap(sns, U5, 0);
        CHECK(s_u2 != NULL, "U2 的快照没被列出");
        CHECK(s_u5 != NULL, "U5 的快照没被列出");
        if (s_u2) {
            const char *t = jstr(NULL, s_u2, "ts");
            snprintf(ts2b, sizeof(ts2b), "%s", t ? t : "");
            CHECK(jnum(s_u2, "size") == U2_BYTES, "U2 快照 size=%lld（该 %d）",
                  jnum(s_u2, "size"), U2_BYTES);
            CHECK(jnum(s_u2, "files") == U2_FILES, "U2 快照 files=%lld（该 %d）",
                  jnum(s_u2, "files"), U2_FILES);
            char p[1024];
            snprintf(p, sizeof(p), GAME_SAVES_SNAP_DIR "/" TID_PS5 "/" U2 "/%s/sdimg_STARBOARD", ts2b);
            CHECK(exists_reg(p), "快照目录里没有 sdimg_STARBOARD：%s", p);
            snprintf(p, sizeof(p), GAME_SAVES_SNAP_DIR "/" TID_PS5 "/" U2 "/%s/sdimg_sce_bu_sdmemory", ts2b);
            CHECK(exists_reg(p), "快照没把 _bu_ 备份一起拷过去：%s", p);
        }
        if (s_u5) {
            const char *t = jstr(NULL, s_u5, "ts");
            snprintf(ts5, sizeof(ts5), "%s", t ? t : "");
        }
        cJSON_Delete(o);
    }

    /* 内容没变 → 不新增。这是省 /data 的关键：不然一天就是 48 份一模一样的东西。 */
    made = game_saves_snap_run_now(TID_PS5, msg, sizeof(msg));
    CHECK(made == 0, "内容没变却又生成了 %d 份（%s）", made, msg);
    o = get_list(TID_PS5);
    if (o) {
        CHECK(cJSON_GetArraySize(cJSON_GetObjectItem(o, "snapshots")) == 2,
              "内容没变却多出了快照");
        cJSON_Delete(o);
    }

    /* 只改 U2 的源存档 → 只该 U2 新增，U5 不动 */
    put_blob(HOME "/" U2 "/savedata_prospero/" TID_PS5 "/sdimg_NEW", 128);
    made = game_saves_snap_run_now(TID_PS5, msg, sizeof(msg));
    CHECK(made == 1, "只改了 U2，该只新增 1 份，实际 %d（%s）", made, msg);
    o = get_list(TID_PS5);
    if (o) {
        cJSON *sns = cJSON_GetObjectItem(o, "snapshots");
        CHECK(count_snap(sns, U2) == 2, "U2 该有 2 份快照，实际 %d", count_snap(sns, U2));
        CHECK(count_snap(sns, U5) == 1, "U5 该还是 1 份，实际 %d", count_snap(sns, U5));
        cJSON *s = find_snap(sns, U2, 0);
        if (s) {
            const char *t = jstr(NULL, s, "ts");
            snprintf(ts2b, sizeof(ts2b), "%s", t ? t : "");
        }
        cJSON_Delete(o);
    }

    /* 一键还原：把 U2 的存档删掉两个文件，再从最新那份整份还原回来 */
    unlink(HOME "/" U2 "/savedata_prospero/" TID_PS5 "/sdimg_STARBOARD");
    unlink(HOME "/" U2 "/savedata_prospero/" TID_PS5 "/sdimg_NEW");
    CHECK(!exists_reg(HOME "/" U2 "/savedata_prospero/" TID_PS5 "/sdimg_STARBOARD"),
          "前置删除没生效，后面还原就测不出东西");
    /* v0.5.2.149：还原走的是同一个 gs_swap_dir，目录 inode 同样不许被换掉 */
    unsigned long long ino_rst = ino_of_dir(HOME "/" U2 "/savedata_prospero/" TID_PS5);
    snprintf(obuf, sizeof(obuf),
             "{\"tid\":\"" TID_PS5 "\",\"action\":\"restore\",\"uid\":\"" U2 "\",\"ts\":\"%s\"}",
             ts2b);
    CHECK(game_saves_snap_action(obuf, strlen(obuf), g_buf, sizeof(g_buf), msg, sizeof(msg)) == 0,
          "还原失败: %s", msg);
    CHECK(exists_reg(HOME "/" U2 "/savedata_prospero/" TID_PS5 "/sdimg_STARBOARD"),
          "还原没把 sdimg_STARBOARD 放回来");
    CHECK(exists_reg(HOME "/" U2 "/savedata_prospero/" TID_PS5 "/sdimg_NEW"),
          "还原没把 sdimg_NEW 放回来");
    CHECK(exists_reg(HOME "/" U5 "/savedata_prospero/" TID_PS5 "/data.bin"),
          "还原把另一个用户的存档牵连了（整份替换只该动目标用户）");
    CHECK(ino_of_dir(HOME "/" U2 "/savedata_prospero/" TID_PS5) == ino_rst,
          "还原把存档**目录本身**换掉了（ino %llu -> %llu）",
          ino_rst, ino_of_dir(HOME "/" U2 "/savedata_prospero/" TID_PS5));
    CHECK(count_temp_in(HOME "/" U2 "/savedata_prospero") == 0,
          "还原在父目录里留了 .old_*/.rst_* 半成品（%d 个）",
          count_temp_in(HOME "/" U2 "/savedata_prospero"));

    /* uid / ts 是路径的最后两段 —— 穿越一律在拼路径之前就被拒 */
    const char *bad[] = {
        "{\"tid\":\"" TID_PS5 "\",\"action\":\"delete\",\"uid\":\"../..\",\"ts\":\"20260101-000000\"}",
        "{\"tid\":\"" TID_PS5 "\",\"action\":\"restore\",\"uid\":\"" U2 "\",\"ts\":\"../../etc\"}",
        "{\"tid\":\"" TID_PS5 "\",\"action\":\"delete\",\"uid\":\"" U2 "\",\"ts\":\"2026-01-01\"}",
        "{\"tid\":\"" TID_PS5 "\",\"action\":\"nope\",\"uid\":\"" U2 "\",\"ts\":\"20260101-000000\"}",
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
        CHECK(game_saves_snap_action(bad[i], strlen(bad[i]), g_buf, sizeof(g_buf),
                                     msg, sizeof(msg)) < 0,
              "非法请求没被拒: %s", bad[i]);

    /* 间隔与保留数的归一化。**86400 不能被当成越界回落成 30 分钟** ——
     * 拿读端口的那套（gs_get_port，夹 1~65535）来读间隔就会这样：用户填了
     * 「一天」，存进去变成默认值，而且没有任何提示。 */
    const char *sb = "{\"tid\":\"" TID_PS5 "\",\"snap\":{\"enabled\":true,"
                     "\"interval\":\"86400\",\"keep\":\"50\"}}";
    CHECK(game_saves_auto_set(TID_PS5, sb, strlen(sb), msg, sizeof(msg)) == 0,
          "snap 配置保存失败: %s", msg);
    o = get_list(TID_PS5);
    if (o) {
        cJSON *snap = cJSON_GetObjectItem(cJSON_GetObjectItem(o, "auto"), "snap");
        CHECK(jbool(snap, "enabled") == 1, "snap.enabled 没存上");
        CHECK(jnum(snap, "interval") == 86400,
              "interval=86400 被回落成 %lld —— 读间隔不能用读端口的那套（夹 1~65535）",
              jnum(snap, "interval"));
        CHECK(jnum(snap, "keep") == GAME_SAVES_SNAP_KEEP_MAX,
              "keep=50 该被夹到 %d，实际 %lld",
              GAME_SAVES_SNAP_KEEP_MAX, jnum(snap, "keep"));
        cJSON_Delete(o);
    }

    /* 保留数裁剪：keep=1，再改一次源 → U2 只该剩最新那份 */
    const char *kb = "{\"tid\":\"" TID_PS5 "\",\"snap\":{\"keep\":1}}";
    CHECK(game_saves_auto_set(TID_PS5, kb, strlen(kb), msg, sizeof(msg)) == 0,
          "snap.keep 保存失败: %s", msg);
    put_blob(HOME "/" U2 "/savedata_prospero/" TID_PS5 "/sdimg_NEW2", 64);
    made = game_saves_snap_run_now(TID_PS5, msg, sizeof(msg));
    CHECK(made == 1, "改了 U2 该新增 1 份，实际 %d（%s）", made, msg);
    o = get_list(TID_PS5);
    if (o) {
        cJSON *sns = cJSON_GetObjectItem(o, "snapshots");
        CHECK(count_snap(sns, U2) == 1, "keep=1 却没裁到 1 份，实际 %d", count_snap(sns, U2));
        cJSON_Delete(o);
    }

    /* 删除一份快照；再删一次必须失败（它已经不在了）。**放在最后** ——
     * 删掉之后这个用户就没有「最近一份」可比，下一轮 run 会给他重新生成一份
     * （这是对的：用户手删了快照，守护线程自然要补回来）。放在中间会把上面
     * 「只该新增 1 份」的断言带偏 —— 这个顺序坑真的踩过一次。 */
    snprintf(obuf, sizeof(obuf),
             "{\"tid\":\"" TID_PS5 "\",\"action\":\"delete\",\"uid\":\"" U5 "\",\"ts\":\"%s\"}",
             ts5);
    CHECK(game_saves_snap_action(obuf, strlen(obuf), g_buf, sizeof(g_buf), msg, sizeof(msg)) == 0,
          "删除快照失败: %s", msg);
    CHECK(game_saves_snap_action(obuf, strlen(obuf), g_buf, sizeof(g_buf), msg, sizeof(msg)) < 0,
          "删一份已经不存在的快照居然成功了");
    o = get_list(TID_PS5);
    if (o) {
        CHECK(count_snap(cJSON_GetObjectItem(o, "snapshots"), U5) == 0, "U5 的快照没被删掉");
        cJSON_Delete(o);
    }

    /* ⑨c 崩溃留下的半成品要清掉，**但不能误删正在写的那一份**。
     * `.tmp_*` / `.rst_*` 只在正常路径上由 gs_rmtree 收掉；进程死在复制中途
     * （`.141` 那个栈溢出就是）它就永远留在盘上，而且列表与全局预算**两套账都不认它**
     * （都只认 15 字符时间戳目录）—— 真机上 `.140` 崩溃留下的 .tmp_226_1791374835
     * 在 `.141` 起来之后还原样躺着。判据用 mtime：够旧（>1h）才删。 */
    {
        const char *udir = GAME_SAVES_SNAP_DIR "/" TID_PS5 "/" U2;
        char stale[512], fresh[512], inner[600];
        snprintf(stale, sizeof(stale), "%s/.tmp_999_1", udir);
        snprintf(fresh, sizeof(fresh), "%s/.tmp_999_2", udir);
        /* 崩溃现场的形态：两个半截目录，各带一个半截文件。
         * 先断言造出来了 —— 否则下面「旧的被清了」会因为**夹具根本不存在**而假绿。 */
        CHECK(mkdir(stale, 0755) == 0, "造不出 .tmp_ 夹具（父目录不在？）：%s", stale);
        CHECK(mkdir(fresh, 0755) == 0, "造不出 .tmp_ 夹具（父目录不在？）：%s", fresh);
        snprintf(inner, sizeof(inner), "%s/sdimg_part", stale);
        put_blob(inner, 32);
        snprintf(inner, sizeof(inner), "%s/sdimg_part", fresh);
        put_blob(inner, 32);
        /* 「旧的」那份的 mtime 拨回 2 小时前（阈值是 1 小时） */
        struct timeval old[2];
        old[0].tv_sec = time(NULL) - 7200;
        old[0].tv_usec = 0;
        old[1] = old[0];
        utimes(stale, old);

        put_blob(HOME "/" U2 "/savedata_prospero/" TID_PS5 "/sdimg_TMPTEST", 48); /* 避开「内容没变」 */
        game_saves_snap_run_now(TID_PS5, msg, sizeof(msg));
        CHECK(access(stale, F_OK) != 0, "崩溃留下的旧 .tmp_* 没被清掉：%s", stale);
        CHECK(access(fresh, F_OK) == 0, "刚建的 .tmp_* 被误删了（阈值判反了？）：%s", fresh);

        /* 收尾：把故意留着的那份删掉，别让它影响后面的用例 */
        snprintf(inner, sizeof(inner), "%s/sdimg_part", fresh);
        unlink(inner);
        rmdir(fresh);
    }

    /* ── ⑪ garlic 兼容导入（`.154`）────────────────────────────────────────
     * 兼容对象是 garlic-savemgr（earthonion）导出的**加密**镜像，也就是它在主机
     * 盘上原样看到的那个东西：
     *   · PS5 → 裸的一个 `sdimg_<名>`（前端把文件名一起带上来）
     *   · PS4 → zip，里面是 `sdimg_<名>` 与配套的 `<名>.bin` 密封密钥
     *
     * 为什么只做「原样搬运」、不做 garlic 那种在主机上**新建**加密镜像：后者要
     * /dev/pfsmgr 的 ioctl（0xc0845302）+ libSceFsInternalForVsh 的 dlsym + 进程提权，
     * 是内核级动作（本项目 `.54` 有整机死机的前科），不该顺手加进来。只接受已经
     * 加密好的镜像，与「下载存档」是同一层东西。
     *
     * 这一节钉四条不变量：
     *   ① 是**逐文件合并**，不是上传那种整份替换 —— 别的文件必须原样留着
     *      （存档目录里还有主机自己的记账：`sdimg_sce_bu_<名>`、`sdimg_sce_sdmemory`，
     *        整份替换会把它们一起抹掉，主机很可能就认不出这份存档了）；
     *   ② 裸镜像**必须**带文件名（主机认的就是那个名字，我们不猜）；
     *   ③ 名字里带目录层级一律压平，**绝不在存档目录之外落任何字节**；
     *   ④ 空 body / 坏 tid / 坏 uid 一律拒。 */
    {
        const char *sdir = HOME "/" U2 "/savedata_prospero/" TID_PS5;
        const char *keep = HOME "/" U2 "/savedata_prospero/" TID_PS5 "/sdimg_KEEP";
        char p[900];
        long long nf = 0;

        /* 合并的反向证据：一个与导入无关的既有文件 */
        put_blob(keep, 40);

        unsigned char raw[300];
        memset(raw, 'G', sizeof(raw));

        /* ① 裸镜像（PS5 形态）：带文件名 */
        CHECK(game_saves_import_garlic(TID_PS5, U2, "sdimg_GARLIC", raw, sizeof(raw),
                                       &nf, msg, sizeof(msg)) == 0,
              "裸镜像导入失败: %s", msg);
        CHECK(nf == 1, "裸镜像该导入 1 个文件，实得 %lld", nf);
        snprintf(p, sizeof(p), "%s/sdimg_GARLIC", sdir);
        {
            struct stat st;
            CHECK(stat(p, &st) == 0 && st.st_size == (off_t)sizeof(raw),
                  "导入的镜像不在或长度不对: %s", p);
        }
        CHECK(exists_reg(keep),
              "逐文件合并把无关文件弄没了 —— 这是整份替换，不是合并");

        /* ② 裸镜像**没有文件名** → 必须拒（我们不猜主机认的那个名字） */
        CHECK(game_saves_import_garlic(TID_PS5, U2, NULL, raw, sizeof(raw), &nf,
                                       msg, sizeof(msg)) < 0,
              "裸镜像没有文件名居然成功了");
        CHECK(strstr(msg, "文件名") != NULL, "拒的理由要说清缺文件名: %s", msg);
        CHECK(game_saves_import_garlic(TID_PS5, U2, "", raw, sizeof(raw), &nf,
                                       msg, sizeof(msg)) < 0,
              "空文件名居然成功了");

        /* ③ 名字里带目录层级 → 只取 basename（前端 <input type=file> 给的就是
         *    basename，这里只是把「外部字节拼路径」这件事挡死在存档目录里） */
        CHECK(game_saves_import_garlic(TID_PS5, U2, "../evil", raw, sizeof(raw), &nf,
                                       msg, sizeof(msg)) == 0,
              "带层级的文件名该被压平后接受: %s", msg);
        CHECK(access(ROOT "/evil", F_OK) != 0, "存档目录**外**被写出了文件！");
        CHECK(access(HOME "/" U2 "/evil", F_OK) != 0, "用户目录下被写出了文件！");
        snprintf(p, sizeof(p), "%s/evil", sdir);
        CHECK(exists_reg(p), "压平之后该落在存档目录里: %s", p);
        unlink(p);

        /* ④ PS4 形态：zip（`sdimg_<名>` + `<名>.bin`）。用自研 zip 现造一个 ——
         *    与 garlic 的包同构（平铺、STORE，没有目录层级）。 */
        const char *z1 = ROOT "/gx_a";
        const char *z2 = ROOT "/gx_b";
        const char *gz = ROOT "/garlic.zip";
        put_blob(z1, 120);
        put_blob(z2, 60);
        zip_ent_t ents[2];
        memset(ents, 0, sizeof(ents));
        snprintf(ents[0].abs, sizeof(ents[0].abs), "%s", z1);
        snprintf(ents[0].zname, sizeof(ents[0].zname), "sdimg_CUSA12345");
        snprintf(ents[1].abs, sizeof(ents[1].abs), "%s", z2);
        snprintf(ents[1].zname, sizeof(ents[1].zname), "CUSA12345.bin");
        const char *zerr = NULL;
        CHECK(zip_write(gz, ents, 2, NULL, &zerr) == 0, "造 garlic zip 失败: %s",
              zerr ? zerr : "?");
        long zn = read_all(gz, g_buf, sizeof(g_buf));
        CHECK(zn > 22, "zip 读不进来: %ld", zn);
        CHECK(game_saves_import_garlic(TID_PS5, U2, NULL, (unsigned char *)g_buf, (size_t)zn,
                                       &nf, msg, sizeof(msg)) == 0,
              "zip 形态导入失败: %s", msg);
        CHECK(nf == 2, "zip 该导入 2 个文件（镜像 + 密封密钥），实得 %lld", nf);
        snprintf(p, sizeof(p), "%s/sdimg_CUSA12345", sdir);
        CHECK(exists_reg(p), "zip 里的镜像没落盘: %s", p);
        snprintf(p, sizeof(p), "%s/CUSA12345.bin", sdir);
        CHECK(exists_reg(p), "zip 里的 .bin 密封密钥没落盘: %s", p);
        CHECK(exists_reg(keep), "zip 导入把无关文件弄没了");

        /* ⑤ 带路径穿越的包 → **整包拒收**（zip_extract 那一层就挡了，且不许落一半） */
        zip_ent_t bad[2];
        memset(bad, 0, sizeof(bad));
        snprintf(bad[0].abs, sizeof(bad[0].abs), "%s", z1);
        snprintf(bad[0].zname, sizeof(bad[0].zname), "sdimg_EVIL");
        snprintf(bad[1].abs, sizeof(bad[1].abs), "%s", z1);
        snprintf(bad[1].zname, sizeof(bad[1].zname), "../evil");
        zerr = NULL;
        CHECK(zip_write(gz, bad, 2, NULL, &zerr) == 0, "造穿越包失败: %s", zerr ? zerr : "?");
        zn = read_all(gz, g_buf, sizeof(g_buf));
        CHECK(game_saves_import_garlic(TID_PS5, U2, NULL, (unsigned char *)g_buf, (size_t)zn,
                                       &nf, msg, sizeof(msg)) < 0,
              "带路径穿越的包居然被接受了");
        CHECK(access(ROOT "/evil", F_OK) != 0, "路径穿越写到了存档目录外！");
        snprintf(p, sizeof(p), "%s/sdimg_EVIL", sdir);
        CHECK(access(p, F_OK) != 0, "整包该被拒收，却已经落了一半: %s", p);

        /* ⑥ 空 body / 坏参数 */
        CHECK(game_saves_import_garlic(TID_PS5, U2, "sdimg_X", raw, 0, &nf, msg, sizeof(msg)) < 0,
              "空文件居然成功了");
        CHECK(game_saves_import_garlic("../etc", U2, "sdimg_X", raw, sizeof(raw), &nf,
                                       msg, sizeof(msg)) < 0,
              "带 .. 的 tid 居然成功了");
        CHECK(game_saves_import_garlic(TID_PS5, "a/b", "sdimg_X", raw, sizeof(raw), &nf,
                                       msg, sizeof(msg)) < 0,
              "带 / 的 uid 居然成功了");

        unlink(z1);
        unlink(z2);
        unlink(gz);
    }

    /* ── ⑫ 本地快照只剩「游戏退出时」这一个触发点（`.154`）────────────────
     * 用户口径：「每次游戏结束后，在本地快照存一份存档；本地自动存储快照删除
     * （定时的功能不要了）」。定时那整条链（gs_snap_due / g_snap_marks /
     * gs_snap_sweep / gs_snap_main / 按配置枚举游戏）已经删掉，触发点只剩
     * game_auto.c 的 GA_ACT_SNAP（由「上一轮在跑的游戏已退出」驱动）。
     *
     * 这一节钉住留下来的两个**查询**接口 —— 它们是引擎做决定时的全部输入：
     *   · game_saves_snap_enabled()  → 这个游戏自己的开关（不看远程总开关：
     *     本地备份不依赖网络，不该被远程那套连坐）；
     *   · game_saves_local_mtime()   → 「这份存档最晚被写过的时刻」，即拉取前
     *     新旧判定的本地一侧（纯函数 ga_pull_needed 的入参就来自这里）。 */
    {
        CHECK(game_saves_snap_enabled(TID_PS5) == 1,
              "TID_PS5 的快照开关该是开的（上面刚存过 enabled:true）");
        CHECK(game_saves_snap_enabled(TID_PS4) == 0,
              "没配过的游戏该回默认（关），别被别的游戏的配置串味");
        CHECK(game_saves_snap_enabled("../etc") == 0, "坏 tid 该回 0 而不是崩");

        const char *off = "{\"tid\":\"" TID_PS5 "\",\"snap\":{\"enabled\":false}}";
        CHECK(game_saves_auto_set(TID_PS5, off, strlen(off), msg, sizeof(msg)) == 0,
              "关掉快照开关失败: %s", msg);
        CHECK(game_saves_snap_enabled(TID_PS5) == 0, "关掉之后开关还是开的");

        /* 本地存档时间戳：有存档 → 1 + 一个真实时刻；没有 → 0（**不是** 1 + 0，
         * 否则拉取前的判定会把「读不到」当成「本地是 1970 年」，白拉一次） */
        long long mt = 0;
        CHECK(game_saves_local_mtime(TID_PS5, U2, &mt) == 1 && mt > 0,
              "U2 的本地存档时间戳该读得到，实得 %lld", mt);
        CHECK(mt <= (long long)time(NULL) + 60,
              "时间戳跑到未来去了（%lld）—— 单位 / 时区搞错了？", mt);
        CHECK(game_saves_local_mtime(TID_PS5, "00000009", &mt) == 0,
              "没有存档的用户该回 0");
        CHECK(game_saves_local_mtime(TID_NONE, U2, &mt) == 0, "没存过的游戏该回 0");
        CHECK(game_saves_local_mtime("../etc", U2, &mt) == 0, "坏 tid 该回 0");
        CHECK(game_saves_local_mtime(TID_PS5, "a/b", &mt) == 0, "坏 uid 该回 0");
    }

    /* ⑩ 小栈。放在最后：它会动 U2 的源、也会给 U5 补一份快照（上面刚删掉），
     * 放在前面会把 ⑨ 那几条计数断言带偏。 */
    put_blob(HOME "/" U2 "/savedata_prospero/" TID_PS5 "/sdimg_STACK", 96); /* 避开「内容没变」 */
    pthread_attr_t at;
    pthread_attr_init(&at);
    if (pthread_attr_setstacksize(&at, SMALL_STACK) != 0) {
        CHECK(0, "pthread_attr_setstacksize(%u) 失败", (unsigned)SMALL_STACK);
    } else {
        pthread_t th;
        if (pthread_create(&th, &at, small_stack_worker, NULL) != 0) {
            CHECK(0, "小栈线程创建失败");
        } else {
            /* 崩了就崩在这里 —— 进程直接没，下面两条 CHECK 根本不会执行。
             * 这正是我们要的效果：真机那个 bug 的宿主等价物。 */
            pthread_join(th, NULL);
            CHECK(g_ss_made >= 1, "32KB 栈上「立即备份」没生成快照（rc=%d, %s）",
                  g_ss_made, g_ss_msg);
            CHECK(g_ss_restore == 0, "32KB 栈上「一键还原」失败（rc=%d, %s）",
                  g_ss_restore, g_ss_msg2);
        }
    }
    pthread_attr_destroy(&at);

    printf("game_saves: %d checks, %d failures\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
