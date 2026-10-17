/* [CN-FORK] v0.6.0.178 越狱引导器同步（/data/ps5_autoloader）的宿主用例。
 *
 * 背景：真机反馈「在载荷管理里上传了 0.6.0.177、引导器目录里还是 0.6.0.176」。
 * 代码里这条链有三处静默 return，还有两条安装路径（U 盘导入 / 仓库安装）根本没调同步，
 * 启动对账又是 slot 化之前的死代码 —— 全都要有东西钉住，不然下次还是「看起来没问题」。
 *
 * 所有数据路径由 run_autoloader_sync_tests.py 改写到临时目录（同 run_storage_tests.py）。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>

#include "pldmgr.h"
#include "payload_mgr.h"
#include "slot.h"
#include "utils.h"

static int checks;

#define CHECK(x)                                                                       \
    do {                                                                               \
        checks++;                                                                      \
        if (!(x)) {                                                                    \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                        \
            exit(1);                                                                   \
        }                                                                              \
    } while (0)

/* 驱动里把日志打到 stderr：失败时能直接看到 AutoloaderSync 走了哪条分支。 */
void pldmgr_log(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}
int pldmgr_autoload_update_config_entry(const char *a, const char *b) { (void)a; (void)b; return 0; }
void pldmgr_autoload_abort(void) {}
int download_to_file(const char *url, const char *path) { (void)url; (void)path; return -1; }
int download_to_file_limited(const char *url, const char *path, size_t limit) {
    (void)url; (void)path; (void)limit;
    return -1;
}

/* ── 小工具 ───────────────────────────────────────────────── */

static void make_elf(const char *path, int fill) {
    unsigned char buf[2048];
    memset(buf, fill, sizeof(buf));
    buf[0] = 0x7f;
    buf[1] = 'E';
    buf[2] = 'L';
    buf[3] = 'F';
    FILE *f = fopen(path, "wb");
    CHECK(f != NULL);
    CHECK(fwrite(buf, 1, sizeof(buf), f) == sizeof(buf));
    fclose(f);
}

static void write_text(const char *path, const char *text) {
    FILE *f = fopen(path, "wb");
    CHECK(f != NULL);
    fputs(text, f);
    fclose(f);
}

static int read_text(const char *path, char *buf, size_t size) {
    FILE *f = fopen(path, "rb");
    if (!f) { buf[0] = '\0'; return -1; }
    size_t n = fread(buf, 1, size - 1, f);
    fclose(f);
    buf[n] = '\0';
    return (int)n;
}

static int path_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static void copy_raw(const char *src, const char *dst) {
    char b[4096];
    size_t n;
    FILE *in = fopen(src, "rb");
    FILE *out = fopen(dst, "wb");
    CHECK(in != NULL && out != NULL);
    while ((n = fread(b, 1, sizeof(b), in)) > 0) CHECK(fwrite(b, 1, n, out) == n);
    fclose(in);
    fclose(out);
}

static void rmtree(const char *dir) {
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char p[1024];
        snprintf(p, sizeof(p), "%s/%s", dir, e->d_name);
        struct stat st;
        if (stat(p, &st) == 0 && S_ISDIR(st.st_mode)) rmtree(p);
        else remove(p);
    }
    closedir(d);
    rmdir(dir);
}

/* ── 环境搭建 ─────────────────────────────────────────────── */

/* 重建引导器目录；txt 为 NULL 表示连 autoload.txt 都不建 */
static void reset_autoloader(const char *txt) {
    rmtree(PS5_AUTOLOADER_DIR);
    CHECK(mkdir(PS5_AUTOLOADER_DIR, 0755) == 0);
    if (txt) write_text(PS5_AUTOLOADER_CONFIG, txt);
}

static void put_autoloader_file(const char *name, int fill) {
    char p[512];
    snprintf(p, sizeof(p), "%s/%s", PS5_AUTOLOADER_DIR, name);
    make_elf(p, fill);
}

static int autoloader_has_file(const char *name) {
    char p[512];
    snprintf(p, sizeof(p), "%s/%s", PS5_AUTOLOADER_DIR, name);
    return path_exists(p);
}

static void clear_storage(void) {
    rmtree(PAYLOADS_STORAGE_DIR);
}

/* 往 storage 里装一份（网页上传 / 仓库安装走的是同一个 commit） */
static void install_storage(const char *name, const char *mode, int fill) {
    static int seq;
    char src[512], up[512], msg[256], key[128] = "";
    snprintf(src, sizeof(src), "/tmp/autoloader_sync_src_%d.elf", ++seq);
    snprintf(up, sizeof(up), "/tmp/autoloader_sync_up_%d.elf", seq);
    make_elf(src, fill);
    copy_raw(src, up);
    int rc = payload_mgr_import_to_storage_mode(name, up, mode, "web_upload", "", msg,
                                                sizeof(msg), key, sizeof(key));
    CHECK(rc == 0);
}

static const char *entry_line(const char *txt) {
    /* 返回 txt 里第一条非空、非 '!' 开头的行（静态缓冲，够用） */
    static char line[256];
    const char *p = txt;
    while (*p) {
        const char *e = strpbrk(p, "\r\n");
        size_t n = e ? (size_t)(e - p) : strlen(p);
        if (n > 0 && p[0] != '!') {
            if (n >= sizeof(line)) n = sizeof(line) - 1;
            memcpy(line, p, n);
            line[n] = '\0';
            return line;
        }
        if (!e) break;
        p = e + 1;
        while (*p == '\r' || *p == '\n') p++;
    }
    line[0] = '\0';
    return line;
}

/* ── 用例 ─────────────────────────────────────────────────── */

/* 1. 已登记 -> 换版本：上传 177，引导器里 176 应当被替换掉 */
static void case_update_registered(void) {
    char txt[4096];
    const char *entry;
    clear_storage();
    reset_autoloader("!1000\nNayePlmTools_0.6.0.176-me.elf\n");
    put_autoloader_file("NayePlmTools_0.6.0.176-me.elf", 0x11);
    install_storage("NayePlmTools_0.6.0.176-me.elf", "new", 0x11);
    install_storage("NayePlmTools_0.6.0.177-me.elf", "replace", 0x22);

    CHECK(autoloader_has_file("NayePlmTools_0.6.0.177-me.elf"));
    CHECK(!autoloader_has_file("NayePlmTools_0.6.0.176-me.elf"));
    CHECK(read_text(PS5_AUTOLOADER_CONFIG, txt, sizeof(txt)) > 0);
    entry = entry_line(txt);
    CHECK(strcmp(entry, "NayePlmTools_0.6.0.177-me.elf") == 0);
    CHECK(strstr(txt, "!1000") != NULL);
}

/* 2. 本工具没登记 -> 登记（fork 文档承诺的「装完自动同步越狱引导器」） */
static void case_register_self(void) {
    char txt[4096];
    clear_storage();
    reset_autoloader("!1000\nps5-account-tools.elf\n");
    install_storage("NayePlmTools_0.6.0.177-me.elf", "new", 0x22);

    CHECK(autoloader_has_file("NayePlmTools_0.6.0.177-me.elf"));
    CHECK(read_text(PS5_AUTOLOADER_CONFIG, txt, sizeof(txt)) > 0);
    CHECK(strstr(txt, "NayePlmTools_0.6.0.177-me.elf") != NULL);
    CHECK(strstr(txt, "ps5-account-tools.elf") != NULL); /* 别人的条目一行不动 */
    CHECK(strstr(txt, "!1000") != NULL);
}

/* 3. 第三方载荷没登记 -> 不许顺手塞进自启 */
static void case_no_register_other(void) {
    char txt[4096];
    clear_storage();
    reset_autoloader("!1000\nps5-account-tools.elf\n");
    install_storage("GoldHEN_v2.4b18.elf", "new", 0x33);

    CHECK(!autoloader_has_file("GoldHEN_v2.4b18.elf"));
    CHECK(read_text(PS5_AUTOLOADER_CONFIG, txt, sizeof(txt)) > 0);
    CHECK(strstr(txt, "GoldHEN") == NULL);
}

/* 4. 第三方载荷已登记 -> 仍然换版本（网页上传这条老路不能被我改坏） */
static void case_update_other_registered(void) {
    char txt[4096];
    const char *entry;
    clear_storage();
    reset_autoloader("!1000\nGoldHEN_v2.4b18.elf\n");
    put_autoloader_file("GoldHEN_v2.4b18.elf", 0x33);
    install_storage("GoldHEN_v2.4b18.elf", "new", 0x33);
    install_storage("GoldHEN_v2.4b19.elf", "replace", 0x34);

    CHECK(autoloader_has_file("GoldHEN_v2.4b19.elf"));
    CHECK(!autoloader_has_file("GoldHEN_v2.4b18.elf"));
    CHECK(read_text(PS5_AUTOLOADER_CONFIG, txt, sizeof(txt)) > 0);
    entry = entry_line(txt);
    CHECK(strcmp(entry, "GoldHEN_v2.4b19.elf") == 0);
}

/* 5. U 盘导入：此前这条路径完全不调同步（真机上从 U 盘装新版，引导器不动） */
static void case_usb_move_syncs(void) {
    char txt[4096], usb_dir[512], usb_file[640], json[2048];
    clear_storage();
    reset_autoloader("!1000\nNayePlmTools_0.6.0.176-me.elf\n");
    put_autoloader_file("NayePlmTools_0.6.0.176-me.elf", 0x11);
    install_storage("NayePlmTools_0.6.0.176-me.elf", "new", 0x11);

    snprintf(usb_dir, sizeof(usb_dir), PLDMGR_USB_ROOT_FORMAT, 0);
    CHECK(mkdir(usb_dir, 0755) == 0 || errno == EEXIST);
    snprintf(usb_file, sizeof(usb_file), "%s/NayePlmTools_0.6.0.178-me.elf", usb_dir);
    make_elf(usb_file, 0x44);

    CHECK(payload_mgr_usb_move(usb_file, "replace", 1, json, sizeof(json)) == 0);
    CHECK(strstr(json, "\"ok\":true") != NULL);
    CHECK(autoloader_has_file("NayePlmTools_0.6.0.178-me.elf"));
    CHECK(!autoloader_has_file("NayePlmTools_0.6.0.176-me.elf"));
    CHECK(read_text(PS5_AUTOLOADER_CONFIG, txt, sizeof(txt)) > 0);
    CHECK(strcmp(entry_line(txt), "NayePlmTools_0.6.0.178-me.elf") == 0);
}

/* 6. 启动对账：storage/自更新目录里有更新的一份 -> 对齐；同步不上就保持原样 */
static void case_bootstrap_reconcile(void) {
    char txt[4096], upd_dir[512], upd_file[640];
    clear_storage();
    reset_autoloader("!1000\nNayePlmTools_0.6.0.176-me.elf\n");
    put_autoloader_file("NayePlmTools_0.6.0.176-me.elf", 0x11);

    /* 6a. 只装了 176：没有更新的一份，什么都不该动 */
    install_storage("NayePlmTools_0.6.0.176-me.elf", "new", 0x11);
    payload_mgr_bootstrap_self_sync();
    CHECK(autoloader_has_file("NayePlmTools_0.6.0.176-me.elf"));
    CHECK(!autoloader_has_file("NayePlmTools_0.6.0.177-me.elf"));

    /* 6b. 自更新落点有 0.6.0.178 -> 对账应当把它写进引导器（这就是「自更新后同步」） */
    snprintf(upd_dir, sizeof(upd_dir), "%s/pldmgr", PAYLOADS_STORAGE_DIR);
    CHECK(mkdir(upd_dir, 0755) == 0 || errno == EEXIST);
    snprintf(upd_file, sizeof(upd_file), "%s/pldmgr_v0.6.0.178.elf", upd_dir);
    make_elf(upd_file, 0x55);
    payload_mgr_bootstrap_self_sync();
    CHECK(autoloader_has_file("NayePlmTools_0.6.0.178-me.elf"));
    CHECK(!autoloader_has_file("NayePlmTools_0.6.0.176-me.elf"));
    CHECK(read_text(PS5_AUTOLOADER_CONFIG, txt, sizeof(txt)) > 0);
    CHECK(strcmp(entry_line(txt), "NayePlmTools_0.6.0.178-me.elf") == 0);
}

/* 7. 对账不许倒着走：引导器里比 storage 新时保持不动 */
static void case_bootstrap_never_downgrades(void) {
    char txt[4096];
    clear_storage();
    install_storage("NayePlmTools_0.6.0.177-me.elf", "new", 0x22);
    /* 安装本身会把引导器换成 177 —— 这里手动摆出「引导器比 storage 新」的状态再对账 */
    reset_autoloader("!1000\nNayePlmTools_0.6.0.179-me.elf\n");
    put_autoloader_file("NayePlmTools_0.6.0.179-me.elf", 0x66);

    payload_mgr_bootstrap_self_sync();
    CHECK(autoloader_has_file("NayePlmTools_0.6.0.179-me.elf"));
    CHECK(!autoloader_has_file("NayePlmTools_0.6.0.177-me.elf"));
    CHECK(read_text(PS5_AUTOLOADER_CONFIG, txt, sizeof(txt)) > 0);
    CHECK(strcmp(entry_line(txt), "NayePlmTools_0.6.0.179-me.elf") == 0);
}

/* 8. 引导器里没有本工具这一条 -> 对账不新登记（登记只发生在用户明确安装时） */
static void case_bootstrap_no_register(void) {
    char txt[4096];
    clear_storage();
    reset_autoloader("!1000\nps5-account-tools.elf\n");
    install_storage("NayePlmTools_0.6.0.177-me.elf", "new", 0x22);
    /* 上一步的安装会顺手登记（allow_register），把 txt 还原成「没登记」再对账 */
    reset_autoloader("!1000\nps5-account-tools.elf\n");

    payload_mgr_bootstrap_self_sync();
    CHECK(!autoloader_has_file("NayePlmTools_0.6.0.177-me.elf"));
    CHECK(read_text(PS5_AUTOLOADER_CONFIG, txt, sizeof(txt)) > 0);
    CHECK(strstr(txt, "NayePlmTools") == NULL);
}

/* 9. 引导器目录里是本版本但文件是 0 字节残包 -> 自愈 */
static void case_heal_broken_copy(void) {
    char p[512];
    clear_storage();
    reset_autoloader("!1000\nNayePlmTools_0.6.0.176-me.elf\n");
    snprintf(p, sizeof(p), "%s/NayePlmTools_0.6.0.176-me.elf", PS5_AUTOLOADER_DIR);
    write_text(p, ""); /* 0 字节残包 */
    install_storage("NayePlmTools_0.6.0.176-me.elf", "new", 0x11);

    struct stat st;
    CHECK(stat(p, &st) == 0);
    CHECK(st.st_size > 1024);
}

int main(void) {
    printf("PAYLOADS_STORAGE_DIR = %s\n", PAYLOADS_STORAGE_DIR);
    printf("PS5_AUTOLOADER_DIR   = %s\n", PS5_AUTOLOADER_DIR);

    case_update_registered();
    case_register_self();
    case_no_register_other();
    case_update_other_registered();
    case_usb_move_syncs();
    case_bootstrap_reconcile();
    case_bootstrap_never_downgrades();
    case_bootstrap_no_register();
    case_heal_broken_copy();

    printf("autoloader sync tests: OK (%d checks)\n", checks);
    return 0;
}
