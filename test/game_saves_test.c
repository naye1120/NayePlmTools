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
#include <sys/stat.h>

#include "game_saves.h"
#include "cJSON.h"

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

int main(void) {
    /* 夹具每次都会 rm -rf 重建，但**单独重跑测试**时上次的 auto 配置还在
     * —— 「默认值」那段断言就得从干净状态起，所以这里自己先清掉。 */
    unlink(GAME_SAVES_AUTO_PATH);

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
        CHECK(mode && !strcmp(mode, "smb"), "mode 默认该 smb，实际 %s", mode ? mode : "(null)");
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
    CHECK(game_saves_pack(TID_PS5, U2, zpath, msg, sizeof(msg)) == 0,
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

    /* ── ⑧' 上传：整份替换（旧的先留一手，成功后清掉）───────────────────── */
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

    printf("game_saves: %d checks, %d failures\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
