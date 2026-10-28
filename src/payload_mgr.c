#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "payload_mgr.h"
#include "slot.h"
#include "json_helpers.h"
#include "sha256.h"
#include "repository.h"
#include "config.h"
#include "utils.h"   /* [CN-FORK] 引导器同步需要 pldmgr_utils_get_payload_folder_name */
#include "pldmgr.h"
#include "autoload.h"
#include "utils.h"
#include "collection_helpers.h"
#include <errno.h>

#ifndef PLDMGR_USB_ROOT_FORMAT
#define PLDMGR_USB_ROOT_FORMAT "/mnt/usb%d"
#endif

/* ── Extension / path checks ──────────────────────────────── */

static int is_supported_extension(const char *path) {
    const char *ext = strrchr(path, '.');
    if (!ext) return 0;
    return (strcasecmp(ext, ".elf") == 0 || strcasecmp(ext, ".bin") == 0);
}

static int is_allowed_usb_path(const char *path) {
    if (!path || path[0] == '\0') return 0;
    if (strstr(path, "..")) return 0;
    if (!is_supported_extension(path)) return 0;
    for (int i = 0; i < 8; i++) {
        char root[512];
        snprintf(root, sizeof(root), PLDMGR_USB_ROOT_FORMAT, i);
        size_t len = strlen(root);
        if (!strncmp(path, root, len) && path[len] == '/') return 1;
    }
    return 0;
}

/* ── USB Scanning (Unmanaged, run-in-place) ─────────────────── */

/* [CN-FORK] 判断一个载荷文件是否「真的能用」：0 字节残包一旦被复制进 ps5_autoloader
 * 并登记到 autoload.txt，越狱引导阶段就会加载一个空文件，表现为 autoloader 静默失效
 * （实机反馈：autoload.txt 指向 NayePlmTools_0.5.1.53.elf，而该文件 0 字节）。
 * 只卡体积下限与 .elf 的魔数；.bin（goldHEN 等原始载荷）没有 ELF 头，不能误杀。 */
static int payload_file_sane(const char *path) {
    struct stat st;
    if (!path || stat(path, &st) != 0 || !S_ISREG(st.st_mode)) return 0;
    if (st.st_size < 1024) return 0;
    const char *ext = strrchr(path, '.');
    if (ext && strcasecmp(ext, ".elf") == 0) {
        FILE *f = fopen(path, "rb");
        if (!f) return 0;
        unsigned char magic[4] = {0};
        size_t got = fread(magic, 1, 4, f);
        fclose(f);
        if (got != 4 || magic[0] != 0x7f || magic[1] != 'E' || magic[2] != 'L' || magic[3] != 'F')
            return 0;
    }
    return 1;
}

/* [CN-FORK] 取一行的净内容（去 CR/LF/BOM/前后空白）到 dst */
static void autoload_line_clean(const char *src, char *dst, size_t dstsz) {
    if (!dst || dstsz == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    strncpy(dst, src, dstsz - 1);
    dst[dstsz - 1] = '\0';
    dst[strcspn(dst, "\r\n")] = '\0';
    pldmgr_utils_strip_config_line(dst);
}

/* [CN-FORK] 一行去掉 CR/LF/BOM/空白 之后是否什么都不剩（即空行） */
static int autoload_line_blank(const char *s) {
    char probe[256];
    autoload_line_clean(s, probe, sizeof(probe));
    return probe[0] == '\0';
}

/* [CN-FORK] 规范本工具那一整条（「!延时」行 + 文件名行）的格式，两件事：
 *   1) 折叠条目块上方那串空行 —— 记事本手改/旧版残留会留一个空行，而本工具那条
 *      在文件开头时看着就是「txt 前面多了一行」；
 *   2) 块顶不是 !延时 行时，在文件名行前插一条 !1000。
 * 只动本工具块自己上方的空行，不碰别的程序的条目（空行不是条目）。
 * 返回值按位：1=插了延时行，2=删了空行，0=本来就规范（此时不改文件）。 */
static int ensure_delay_before(char (*lines)[256], int *line_count, int name_idx) {
    int changed = 0;
    char probe[256];

    /* 跳过空行往上找第一条非空行：它是 ! 开头，说明延时行已在，块顶就是它 */
    int d = name_idx - 1;
    while (d >= 0 && autoload_line_blank(lines[d])) d--;
    int has_delay = 0;
    if (d >= 0) {
        autoload_line_clean(lines[d], probe, sizeof(probe));
        has_delay = (probe[0] == '!');
    }

    /* 块顶（有延时行则是延时行，否则是文件名行）再往上那串连续空行：起点 start */
    int top = has_delay ? d : name_idx;
    int b = top - 1;
    while (b >= 0 && autoload_line_blank(lines[b])) b--;
    int start = b + 1;

    /* 折叠 [start, name_idx) 里的空行（这段只可能是空行 + 自己的延时行） */
    int w = start;
    for (int r = start; r < name_idx; r++) {
        if (autoload_line_blank(lines[r])) continue;
        if (w != r) memcpy(lines[w], lines[r], sizeof(lines[0]));
        w++;
    }
    int nb = name_idx - w;
    if (nb > 0) {
        for (int i = name_idx; i < *line_count; i++)
            memcpy(lines[i - nb], lines[i], sizeof(lines[0]));
        *line_count -= nb;
        name_idx  -= nb;
        changed  |= 2;
    }

    if (has_delay) return changed;
    if (*line_count >= 100) return changed;  /* 行数已满，不越界插 */
    for (int k = *line_count; k > name_idx; k--)
        memcpy(lines[k], lines[k - 1], sizeof(lines[0]));
    snprintf(lines[name_idx], sizeof(lines[0]), "!1000\n");
    (*line_count)++;
    return changed | 1;
}

/* [CN-FORK] 回写整份 txt：行尾缺 \n 的补上，避免上一行没换行时新内容粘到它尾巴上 */
static void write_autoload_lines(FILE *f, char (*lines)[256], int line_count) {
    for (int i = 0; i < line_count; i++) {
        size_t l = strlen(lines[i]);
        fputs(lines[i], f);
        if (l == 0 || lines[i][l - 1] != '\n') fputc('\n', f);
    }
}

/* [CN-FORK] 原子复制：先写 dst.tmp，校验字节数与源一致后再 rename 覆盖。
 * 旧实现直接 fopen(dst, "wb") 会先把目标截断成 0 字节，中途失败就留个残包在自启目录；
 * 更狠的是 src 与 dst 指向同一文件时，截断会把好文件清空、函数还返回成功。 */
static int copy_file(const char *src, const char *dst) {
    struct stat sst, dst_st;
    if (!src || !dst) return -1;
    if (stat(src, &sst) != 0 || !S_ISREG(sst.st_mode) || sst.st_size == 0) return -1;
    if (stat(dst, &dst_st) == 0 && dst_st.st_dev == sst.st_dev && dst_st.st_ino == sst.st_ino)
        return 0; /* 同一个文件：无需也不允许“自我复制” */

    char tmp[600];
    snprintf(tmp, sizeof(tmp), "%s.tmp", dst);
    FILE *in = fopen(src, "rb");
    if (!in) return -1;
    FILE *out = fopen(tmp, "wb");
    if (!out) {
        fclose(in);
        return -1;
    }

    char buf[8192];
    size_t n;
    int failed = 0;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            failed = 1;
            break;
        }
    }
    if (ferror(in)) failed = 1;
    fclose(in);
    fclose(out);

    struct stat tst;
    if (!failed && (stat(tmp, &tst) != 0 || tst.st_size != sst.st_size)) failed = 1;
    if (failed || rename(tmp, dst) != 0) {
        remove(tmp);
        return -1;
    }
    return 0;
}

/* [CN-FORK] 这个载荷文件名是不是「本工具自己」。
 * autoload.txt 里按去掉版本号的工具名匹配，所以 NayePlmTools_<任意版本><变体>.elf 都算。
 * is_pldmgr_name() 认的旧名（pldmgr_*）也算 —— 老版本装进去的就是那种名字，
 * 引导器目录里可能还躺着，不能当第三方载荷处理。 */
int payload_mgr_is_self_payload(const char *filename) {
    if (!filename || !filename[0]) return 0;
    if (is_pldmgr_name(filename)) return 1;
    char folder[128];
    pldmgr_utils_get_payload_folder_name(filename, folder, sizeof(folder));
    return strcasecmp(folder, PLDMGR_APP_NAME) == 0;
}

/* [CN-FORK] 从文件名里抠出版本号：NayePlmTools_0.6.0.176-me.elf → 0.6.0.176，
 * pldmgr_v0.6.0.178.elf → 0.6.0.178。抠不出（被手工改过名）时返回空串。 */
static void autoloader_version_of(const char *filename, char *out, size_t out_size) {
    if (out_size) out[0] = '\0';
    if (!filename || out_size < 2) return;
    const char *p = filename;
    while (*p && !isdigit((unsigned char)*p)) p++;
    size_t n = 0;
    while (p[n] && (isdigit((unsigned char)p[n]) || p[n] == '.')) n++;
    while (n > 0 && p[n - 1] == '.') n--;
    if (n == 0 || n >= out_size) { out[0] = '\0'; return; }
    memcpy(out, p, n);
    out[n] = '\0';
}

/* [CN-FORK] 文件名尾部的变体后缀（-me / -ga），没有就按本包变体。 */
static void autoloader_variant_suffix(const char *filename, char *out, size_t out_size) {
    snprintf(out, out_size, "%s", PLDMGR_VARIANT_SUFFIX);
    if (!filename) return;
    char base[256];
    snprintf(base, sizeof(base), "%s", filename);
    char *dot = strrchr(base, '.');
    if (dot) *dot = '\0';
    char *dash = strrchr(base, '-');
    if (!dash || dash[1] == '\0' || strlen(dash + 1) > 4) return;
    for (const char *q = dash + 1; *q; q++)
        if (!isalpha((unsigned char)*q)) return;
    snprintf(out, out_size, "%s", dash);
}

/* [CN-FORK] 安装 payload 后，若 /data/ps5_autoloader/autoload.txt 中已登记同一工具
 * （按去除版本号的 folder_name 匹配），则把新 ELF 同步到该目录、更新 txt、删除旧文件。
 * 没登记时：本工具（allow_register）追加登记，别的载荷什么都不做。
 *
 * v0.6.0.178 起三处「静默 return」全部改成留日志：真机反馈「在载荷管理里上传成功、
 * 但 /data/ps5_autoloader/ 一直没变」时，旧代码在「txt 读不开」「没匹配到条目」
 * 「复制失败」这三种情况下做的事完全一样 —— 什么都不做、什么都不记，查不出卡在哪。
 * 另外补了 allow_register 分支：本工具首次装上时旧代码永远不登记，而 fork 的文档
 * 一直写着「装完自动同步越狱引导器」。 */
int payload_mgr_autoloader_sync(const char *new_filename, const char *new_file_path,
                                int allow_register) {
    if (!new_filename || !new_filename[0] || !new_file_path || !new_file_path[0]) return -1;
    if (strchr(new_filename, '/') || strstr(new_filename, "..")) return -1;

    struct stat dir_st;
    if (stat(PS5_AUTOLOADER_DIR, &dir_st) != 0 || !S_ISDIR(dir_st.st_mode)) {
        /* [CN-FORK] v0.6.3.196 **这个目录归越狱引导器所有，我们不建它** ——
         * 没装引导器的机器上凭空多一个 /data/ps5_autoloader 毫无意义（里面没有
         * autoload.txt，引导器也不会去读）。
         * 但「没检测到」这件事原来**一个字都不留**：真机上装完载荷、
         * `/data/ps5_autoloader` 一直不出现，日志、界面、返回值三处都看不出原因
         * （2026-10-28 实测：/data 下只有 nayeplmtools / pkg / accounts /
         * ps5-account-tools / homebrew，连目录都没有，日志里也没有 AutoloaderSync）。
         * 现在留一行 —— 看到它就知道该去装引导器，而不是继续怀疑本工具的同步逻辑。 */
        pldmgr_log("[AutoloaderSync] %s 不存在（本机没装越狱引导器）—— 跳过同步\n",
                   PS5_AUTOLOADER_DIR);
        return 1;
    }

    FILE *f = fopen(PS5_AUTOLOADER_CONFIG, "r");
    if (!f) {
        /* 目录在、txt 读不开：以前这里静默 return，现在留痕 */
        pldmgr_log("[AutoloaderSync] cannot read %s: %s — sync skipped\n",
                   PS5_AUTOLOADER_CONFIG, strerror(errno));
        return -1;
    }

    char new_folder[128];
    pldmgr_utils_get_payload_folder_name(new_filename, new_folder, sizeof(new_folder));

    char   lines[100][256];
    int    line_count = 0;
    int    matched    = 0;
    int    same_name  = 0;
    int    registered = 0;
    int    our_idx    = -1;   /* 本工具条目在 lines[] 里的下标 */
    char   old_filename[256] = "";

    while (line_count < 100 && fgets(lines[line_count], (int)sizeof(lines[0]), f)) {
        char clean[256];
        strncpy(clean, lines[line_count], sizeof(clean) - 1);
        clean[sizeof(clean) - 1] = '\0';
        clean[strcspn(clean, "\r\n")] = '\0';
        pldmgr_utils_strip_config_line(clean); /* [CN-FORK] 容忍记事本手改的 BOM/缩进 */

        char folder[128];
        pldmgr_utils_get_payload_folder_name(clean, folder, sizeof(folder));

        /* 同名条目：这个版本已经登记过；但引导器目录里那份可能已经是 0 字节残包，
         * 记下来交给下面 !matched 分支做自愈。 */
        if (strcmp(clean, new_filename) == 0) {
            same_name = 1;
            our_idx   = line_count;
        } else if (strcasecmp(folder, new_folder) == 0) {
            strncpy(old_filename, clean, sizeof(old_filename) - 1);
            old_filename[sizeof(old_filename) - 1] = '\0';
            snprintf(lines[line_count], sizeof(lines[0]), "%s\n", new_filename);
            matched   = 1;
            our_idx   = line_count;
        }
        line_count++;
    }
    fclose(f);

    if (!matched && !same_name) {
        if (!allow_register) {
            /* 以前这里是静默 return：真机上「装完引导器没变」既没动静也没日志。
             * 现在留一行，排查时能一眼区分「没登记所以没动」和「复制失败」。 */
            pldmgr_log("[AutoloaderSync] %s not registered in %s — nothing to do\n",
                       new_filename, PS5_AUTOLOADER_CONFIG);
            return 1;
        }
        if (line_count >= 100) {
            pldmgr_log("[AutoloaderSync] %s already has %d lines — cannot register %s\n",
                       PS5_AUTOLOADER_CONFIG, line_count, new_filename);
            return -1;
        }
        /* 本工具首次装进 storage：fork 的文档一直写着「装完自动同步越狱引导器」，
         * 但旧代码只在「已经登记过」时才动手，首次安装永远登记不上。 */
        snprintf(lines[line_count], sizeof(lines[0]), "%s\n", new_filename);
        our_idx = line_count;
        line_count++;
        registered = 1;
    }

    /* [CN-FORK] 不管走哪条分支，本工具那一行上面都必须有 !延时行，且中间不留空行。
     * 旧逻辑只在「首次追加」时写延时行，换版本号时只替文件名，手改删掉就再也没人补。 */
    int delay_fix = (our_idx >= 0) ? ensure_delay_before(lines, &line_count, our_idx) : 0;
    if (delay_fix & 1)
        pldmgr_log("[AutoloaderSync] added missing '!1000' delay line before %s\n", new_filename);
    if (delay_fix & 2)
        pldmgr_log("[AutoloaderSync] trimmed blank line(s) above %s\n", new_filename);

    if (!matched && !registered) {
        /* [CN-FORK] 这一版已经登记过：只有「引导器目录里那份不健全」才动手。
         * 实机出现过 0 字节残包卡在这里 —— autoloader 每次开机加载空文件，
         * 而旧代码在同版本情况下永远不动手，没人修。 */
        char cur[512];
        snprintf(cur, sizeof(cur), "%s/%s", PS5_AUTOLOADER_DIR, new_filename);
        if (payload_file_sane(cur)) {
            if (!delay_fix)
                pldmgr_log("[AutoloaderSync] %s already up to date\n", new_filename);
        } else if (copy_file(new_file_path, cur) == 0) {
            pldmgr_log("[AutoloaderSync] healed broken copy: %s\n", cur);
        } else {
            pldmgr_log("[AutoloaderSync] FAILED to heal %s: %s\n", cur, strerror(errno));
        }
        if (!delay_fix) return 0;
        f = fopen(PS5_AUTOLOADER_CONFIG, "w");
        if (!f) {
            pldmgr_log("[AutoloaderSync] cannot write %s: %s\n", PS5_AUTOLOADER_CONFIG, strerror(errno));
            return -1;
        }
        write_autoload_lines(f, lines, line_count);
        if (fclose(f)) {
            pldmgr_log("[AutoloaderSync] cannot flush %s: %s\n", PS5_AUTOLOADER_CONFIG, strerror(errno));
            return -1;
        }
        return 0;
    }

    /* 复制新 ELF 到引导器目录（先落文件、再改 txt，txt 永远不指向不存在的文件） */
    char dest[512];
    snprintf(dest, sizeof(dest), "%s/%s", PS5_AUTOLOADER_DIR, new_filename);
    if (copy_file(new_file_path, dest) != 0) {
        pldmgr_log("[AutoloaderSync] FAILED to copy %s -> %s: %s\n",
                   new_file_path, dest, strerror(errno));
        return -1;
    }

    /* 删除引导器目录里的旧版本 */
    if (old_filename[0]) {
        char old_path[512];
        snprintf(old_path, sizeof(old_path), "%s/%s", PS5_AUTOLOADER_DIR, old_filename);
        if (remove(old_path) != 0)
            pldmgr_log("[AutoloaderSync] could not remove old copy %s: %s\n", old_path, strerror(errno));
    }

    /* 写回更新后的 autoload.txt */
    f = fopen(PS5_AUTOLOADER_CONFIG, "w");
    if (!f) {
        pldmgr_log("[AutoloaderSync] cannot write %s: %s\n", PS5_AUTOLOADER_CONFIG, strerror(errno));
        return -1;
    }
    write_autoload_lines(f, lines, line_count);
    if (fclose(f)) {
        pldmgr_log("[AutoloaderSync] cannot flush %s: %s\n", PS5_AUTOLOADER_CONFIG, strerror(errno));
        return -1;
    }

    if (registered)
        pldmgr_log("[AutoloaderSync] registered %s in ps5_autoloader\n", new_filename);
    else
        pldmgr_log("[AutoloaderSync] Updated ps5_autoloader: %s -> %s\n", old_filename, new_filename);
    return 0;
}

/* [CN-FORK] 启动对账：把引导器目录里那份「本工具」对齐到 storage / 自更新目录里最新的一份。
 *
 * v0.6.0.178 重写。旧实现（v0.5.1.x 时代写的）找的是 slot 化之前的旧布局
 * `payloads/<工具名>/`，而 v0.6.0 起载荷落在 `payloads/local|repo/<key>/`（见 slot.c），
 * 那个目录根本不存在 —— 于是整段成了死代码。真机表现就是：storage 里已经是新版、
 * 自己更新也成功了，但 /data/ps5_autoloader/ 一直停在旧版本，只有「网页上传」
 * 那一条路会顺带同步。
 *
 * 规则（宁可不动，也不做让人意外的改动）：
 *   - 没装引导器目录 / autoload.txt 读不开 → 只留日志，不动；
 *   - autoload.txt 里没有本工具这一条 → 不新登记（登记只发生在用户明确安装时）；
 *   - storage 与 payloads/pldmgr 里都找不到自己的 ELF → 不动；
 *   - 候选版本不比在册版本新 → 不动 —— 绝不把引导器退回旧版本。 */
void payload_mgr_bootstrap_self_sync(void) {
    struct stat st;
    if (stat(PS5_AUTOLOADER_DIR, &st) != 0 || !S_ISDIR(st.st_mode)) return;

    /* 1) 引导器里现在登记的是哪一份 */
    FILE *f = fopen(PS5_AUTOLOADER_CONFIG, "r");
    if (!f) {
        pldmgr_log("[AutoloaderSync] cannot read %s: %s — reconcile skipped\n",
                   PS5_AUTOLOADER_CONFIG, strerror(errno));
        return;
    }
    char entry_name[256] = "";
    char entry_version[64] = "";
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char clean[256];
        strncpy(clean, line, sizeof(clean) - 1);
        clean[sizeof(clean) - 1] = '\0';
        clean[strcspn(clean, "\r\n")] = '\0';
        pldmgr_utils_strip_config_line(clean);
        if (!clean[0] || clean[0] == '!') continue;
        if (!payload_mgr_is_self_payload(clean)) continue;
        snprintf(entry_name, sizeof(entry_name), "%s", clean);
        autoloader_version_of(clean, entry_version, sizeof(entry_version));
        break;
    }
    fclose(f);
    if (!entry_name[0]) {
        pldmgr_log("[AutoloaderSync] %s has no %s entry — leaving it alone\n",
                   PS5_AUTOLOADER_CONFIG, PLDMGR_APP_NAME);
        return;
    }

    /* 2) 找 storage / 自更新目录里最新的一份自己 */
    char best_path[640] = "";
    char best_name[256] = "";
    char best_version[64] = "";

    /* 2a. 自更新落点：payloads/pldmgr/pldmgr_v<版本>.elf */
    char upd_dir[512];
    snprintf(upd_dir, sizeof(upd_dir), "%s/pldmgr", PAYLOADS_STORAGE_DIR);
    DIR *d = opendir(upd_dir);
    if (d) {
        struct dirent *ent;
        while ((ent = readdir(d)) != NULL) {
            if (ent->d_name[0] == '.') continue;
            char ver[64];
            autoloader_version_of(ent->d_name, ver, sizeof(ver));
            if (!ver[0]) continue;
            char cand[640];
            snprintf(cand, sizeof(cand), "%s/%s", upd_dir, ent->d_name);
            if (!payload_file_sane(cand)) continue;
            if (best_version[0] && slot_version_compare(ver, best_version) <= 0) continue;
            snprintf(best_version, sizeof(best_version), "%s", ver);
            snprintf(best_path, sizeof(best_path), "%s", cand);
            snprintf(best_name, sizeof(best_name), "%s_%s%s.elf", PLDMGR_APP_NAME, ver,
                     PLDMGR_VARIANT_SUFFIX);
        }
        closedir(d);
    }

    /* 2b. storage 里已安装的槽位（local 与 repo 都算） */
    SlotMeta *slots = NULL;
    size_t slot_count = 0;
    if (slot_list_all(&slots, &slot_count) == 0) {
        for (size_t i = 0; i < slot_count; i++) {
            if (!payload_mgr_is_self_payload(slots[i].filename)) continue;
            char ver[64];
            autoloader_version_of(slots[i].filename, ver, sizeof(ver));
            if (!ver[0] || !payload_file_sane(slots[i].payload_path)) continue;
            if (best_version[0] && slot_version_compare(ver, best_version) <= 0) continue;
            char suffix[8];
            autoloader_variant_suffix(slots[i].filename, suffix, sizeof(suffix));
            snprintf(best_version, sizeof(best_version), "%s", ver);
            snprintf(best_path, sizeof(best_path), "%s", slots[i].payload_path);
            snprintf(best_name, sizeof(best_name), "%s_%s%s.elf", PLDMGR_APP_NAME, ver, suffix);
        }
        slot_free_all(slots);
    }

    if (!best_path[0]) {
        pldmgr_log("[AutoloaderSync] no installed %s copy in storage — bootloader left at %s\n",
                   PLDMGR_APP_NAME, entry_name);
        return;
    }

    /* 3) 只在确实变新时才动 */
    if (entry_version[0] && slot_version_compare(best_version, entry_version) <= 0) return;
    if (slot_version_compare(best_version, MENU_VERSION) < 0)
        pldmgr_log("[AutoloaderSync] note: autoloader gets %s while running %s — "
                   "install %s through the payload manager to sync it\n",
                   best_version, MENU_VERSION, MENU_VERSION);
    pldmgr_log("[AutoloaderSync] startup reconcile: %s (%s) -> %s (%s)\n", entry_name,
               entry_version[0] ? entry_version : "?", best_name, best_version);
    payload_mgr_autoloader_sync(best_name, best_path, 0);
}

static int append_usb_path(char ***paths, size_t *count, size_t *capacity, const char *path) {
    char **grown = collection_reserve(*paths, capacity, *count + 1, sizeof(char *));
    if (!grown) return -1;
    *paths = grown;
    char *copy = strdup(path);
    if (!copy) return -1;
    (*paths)[(*count)++] = copy;
    return 0;
}

static int scan_usb_dir(const char *dir_path, char ***out_paths, size_t *count, size_t *cap) {
    DIR *d = opendir(dir_path);
    if (!d) return errno == ENOENT ? 0 : -1;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;

        char full[512];
        snprintf(full, sizeof(full), "%s/%s", dir_path, ent->d_name);

        struct stat st;
        if (stat(full, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            /* Scan 1 level deep */
            DIR *sub = opendir(full);
            if (sub) {
                struct dirent *sub_ent;
                while ((sub_ent = readdir(sub)) != NULL) {
                    if (sub_ent->d_name[0] == '.') continue;
                    if (is_supported_extension(sub_ent->d_name)) {
                        char sub_full[640];
                        snprintf(sub_full, sizeof(sub_full), "%s/%s", full, sub_ent->d_name);
                        if (append_usb_path(out_paths, count, cap, sub_full) != 0) {
                            closedir(sub); closedir(d); return -1;
                        }
                    }
                }
                closedir(sub);
            }
        } else if (is_supported_extension(ent->d_name)) {
            if (append_usb_path(out_paths, count, cap, full) != 0) { closedir(d); return -1; }
        }
    }
    closedir(d);
    return 0;
}

/* ── Listing API ───────────────────────────────────────────── */

/* [CN-FORK] v0.6.0.176 栈帧红线：本函数原来把 legacy_map[16384] 和 items 循环里
 * 一整套 6 倍最坏转义缓冲（sk_e/pp_e/id_e/surl_e/det_e/src_e/desc_e/org_e/od_e …）
 * 都放在栈上，整帧 53848 B —— 真机 MHD 每条连接一条 pthread、栈只有 64KB，必 SIGSEGV。
 * 整块搬堆：函数入口 malloc 一次，两个出口各 free 一次。 */
typedef struct {
    char legacy_map[16384];
    char sk_e[6 * 128], pp_e[6 * 512], kind_e[6 * 16], fn_e[6 * 256];
    char id_e[6 * 128], surl_e[6 * 1024], sname_e[6 * 256], name_e[6 * 256];
    char ver_e[6 * 64], csum_e[6 * 65], lup_e[6 * 64], auth_e[6 * 128], det_e[6 * 1024];
    char src_e[6 * 1024], desc_e[6 * 1024];
    char org_e[6 * 64], od_e[6 * 1024];
} PayloadListJsonScratch;

size_t payload_mgr_list_json(char *json_buf, size_t buf_size) {
    if (!json_buf || !buf_size) return 0;
    SlotMeta *slots = NULL;
    size_t slot_count = 0;
    json_buf[0] = '\0';
    if (slot_list_all(&slots, &slot_count) != 0) return 0;

    /* [CN-FORK] v0.6.0.176 大缓冲 scratch 指针先置 NULL：USB 扫描段可能 goto
     * scan_failed（在 malloc 之前），出口统一 free(NULL) 是安全的。 */
    PayloadListJsonScratch *sc = NULL;

    /* Enumerate USB payloads */
    char **usb_paths = NULL;
    size_t usb_count = 0, usb_cap = 0;
    int scan_usb = config_read_bool("SCAN_USB_PAYLOADS", 0);

    for (int u = 0; u < 8; u++) {
        char pldmgr_usb[64];
        /* [CN-FORK] U 盘库目录名统一成 nayeplmtools（上游用 "pldmgr"），见 pldmgr.h */
        snprintf(pldmgr_usb, sizeof(pldmgr_usb), PLDMGR_USB_ROOT_FORMAT "/" PLDMGR_USB_LIB_NAME, u);
        if (scan_usb_dir(pldmgr_usb, &usb_paths, &usb_count, &usb_cap) != 0) goto scan_failed;

        if (scan_usb) {
            char root_usb[64];
            snprintf(root_usb, sizeof(root_usb), PLDMGR_USB_ROOT_FORMAT, u);
            DIR *d = opendir(root_usb);
            if (d) {
                struct dirent *ent;
                while ((ent = readdir(d)) != NULL) {
                    if (ent->d_name[0] == '.') continue;
                    if (is_supported_extension(ent->d_name)) {
                        char full[512];
                        snprintf(full, sizeof(full), "%s/%s", root_usb, ent->d_name);
                        if (append_usb_path(&usb_paths, &usb_count, &usb_cap, full) != 0) {
                            closedir(d); goto scan_failed;
                        }
                    }
                }
                closedir(d);
            }
        }
    }

    /* Pop any legacy paths migration map */
    /* [CN-FORK] v0.6.0.176 栈帧红线：legacy_map(16KB) 与下面 items 循环那套 6 倍转义
     * 缓冲（~37KB）原来都在栈上，整帧 53848 B。真机 64KB 线程栈必死 —— 整块搬堆。 */
    sc = malloc(sizeof(*sc));
    if (!sc) goto scan_failed;   /* 走统一清理出口（slot_free_all / usb_paths / free(sc)） */
    sc->legacy_map[0] = '\0';
    int legacy_result = slot_pop_legacy_paths_json(sc->legacy_map, sizeof(sc->legacy_map));
    int has_legacy = legacy_result == 0 && sc->legacy_map[0] != '\0';

    JsonListBuilder jb = { json_buf, buf_size, 0, 1, 0 };
    if (legacy_result < -1) json_builder_fail(&jb);
    char update_path[1024], pending_version[64], pending_e[6 * 64];
    int pending = repository_check_self_update(update_path, sizeof(update_path), pending_version, sizeof(pending_version));
    json_escape_checked(&jb, pending ? pending_version : "", pending_e, sizeof(pending_e));
    json_append(&jb, "{\n  \"self\":{\"id\":\"pldmgr\",\"running_version\":\"%s\",\"pending_version\":\"%s\"},\n", MENU_VERSION, pending_e);
    json_append(&jb, "  \"items\": [\n");

    for (size_t i = 0; i < slot_count; i++) {
        json_escape_checked(&jb, slots[i].slot_key, sc->sk_e, sizeof(sc->sk_e));
        json_escape_checked(&jb, slots[i].payload_path, sc->pp_e, sizeof(sc->pp_e));
        json_escape_checked(&jb, slots[i].kind, sc->kind_e, sizeof(sc->kind_e));
        json_escape_checked(&jb, slots[i].filename, sc->fn_e, sizeof(sc->fn_e));

        json_append(&jb, "    {\n"
                         "      \"slot\": \"%s\",\n"
                         "      \"path\": \"%s\",\n"
                         "      \"kind\": \"%s\",\n"
                         "      \"filename\": \"%s\"",
                    sc->sk_e, sc->pp_e, sc->kind_e, sc->fn_e);

        if (strcmp(slots[i].kind, SLOT_KIND_REPO) == 0) {

            json_escape_checked(&jb, slots[i].id, sc->id_e, sizeof(sc->id_e));
            json_escape_checked(&jb, slots[i].source_url, sc->surl_e, sizeof(sc->surl_e));
            json_escape_checked(&jb, slots[i].source_name, sc->sname_e, sizeof(sc->sname_e));
            json_escape_checked(&jb, slots[i].name, sc->name_e, sizeof(sc->name_e));
            json_escape_checked(&jb, slots[i].version, sc->ver_e, sizeof(sc->ver_e));
            json_escape_checked(&jb, slots[i].checksum, sc->csum_e, sizeof(sc->csum_e));
            json_escape_checked(&jb, slots[i].last_update, sc->lup_e, sizeof(sc->lup_e));
            json_escape_checked(&jb, slots[i].author, sc->auth_e, sizeof(sc->auth_e));
            json_escape_checked(&jb, slots[i].details_url, sc->det_e, sizeof(sc->det_e));

            json_escape_checked(&jb, slots[i].source, sc->src_e, sizeof(sc->src_e));
            json_escape_checked(&jb, slots[i].description, sc->desc_e, sizeof(sc->desc_e));

            json_append(&jb, ",\n"
                             "      \"id\": \"%s\",\n"
                             "      \"source_url\": \"%s\",\n"
                             "      \"source_name\": \"%s\",\n"
                             "      \"name\": \"%s\",\n"
                             "      \"version\": \"%s\",\n"
                             "      \"checksum\": \"%s\",\n"
                             "      \"last_update\": \"%s\",\n"
                             "      \"author\": \"%s\",\n"
                             "      \"details_url\": \"%s\",\n"
                             "      \"source\": \"%s\",\n"
                             "      \"description\": \"%s\"",
                        sc->id_e, sc->surl_e, sc->sname_e, sc->name_e, sc->ver_e,
                        sc->csum_e, sc->lup_e, sc->auth_e, sc->det_e, sc->src_e, sc->desc_e);
        } else {
            json_escape_checked(&jb, slots[i].origin, sc->org_e, sizeof(sc->org_e));
            json_escape_checked(&jb, slots[i].origin_detail, sc->od_e, sizeof(sc->od_e));

            json_append(&jb, ",\n"
                             "      \"origin\": \"%s\",\n"
                             "      \"origin_detail\": \"%s\"",
                        sc->org_e, sc->od_e);
        }

        json_append(&jb, "\n    }%s\n", (i + 1 < slot_count) ? "," : "");
    }

    json_append(&jb, "  ],\n  \"usb\": [\n");
    for (size_t u = 0; u < usb_count; u++) {
        char u_e[6 * 512];
        json_escape_checked(&jb, usb_paths[u], u_e, sizeof(u_e));
        json_append(&jb, "    \"%s\"%s\n", u_e, (u + 1 < usb_count) ? "," : "");
    }

    /* Provide payloads array containing all paths for backward compatibility */
    json_append(&jb, "  ],\n  \"payloads\": [\n");
    size_t total_p = slot_count + (size_t)usb_count;
    size_t curr_p = 0;
    for (size_t i = 0; i < slot_count; i++) {
        char pp_e[6 * 512];
        json_escape_checked(&jb, slots[i].payload_path, pp_e, sizeof(pp_e));
        curr_p++;
        json_append(&jb, "    \"%s\"%s\n", pp_e, (curr_p < total_p) ? "," : "");
    }
    for (size_t u = 0; u < usb_count; u++) {
        char u_e[6 * 512];
        json_escape_checked(&jb, usb_paths[u], u_e, sizeof(u_e));
        curr_p++;
        json_append(&jb, "    \"%s\"%s\n", u_e, (curr_p < total_p) ? "," : "");
    }

    if (has_legacy) {
        json_append(&jb, "  ],\n  \"legacy_paths\": %s\n}", sc->legacy_map);
    } else {
        json_append(&jb, "  ]\n}\n");
    }

    slot_free_all(slots);
    for (size_t u = 0; u < usb_count; u++) free(usb_paths[u]);
    if (usb_paths) free(usb_paths);
    free(sc);

    return jb.pos;
scan_failed:
    slot_free_all(slots);
    for (size_t u = 0; u < usb_count; u++) free(usb_paths[u]);
    free(usb_paths);
    free(sc);
    json_buf[0] = '\0';
    return 0;
}

int payload_mgr_delete_payload(const char *slot_or_filename) {
    if (!slot_or_filename || slot_or_filename[0] == '\0') return -1;
    if (strstr(slot_or_filename, "..")) return -1;

    /* If it is a direct slot key */
    if (strncmp(slot_or_filename, "repo/", 5) == 0 || strncmp(slot_or_filename, "local/", 6) == 0) {
        return slot_delete(slot_or_filename);
    }

    /* If it starts with PAYLOADS_STORAGE_DIR */
    size_t base_len = strlen(PAYLOADS_STORAGE_DIR);
    if (strncmp(slot_or_filename, PAYLOADS_STORAGE_DIR, base_len) == 0 && slot_or_filename[base_len] == '/') {
        const char *rel = slot_or_filename + base_len + 1;
        if (strncmp(rel, "repo/", 5) == 0 || strncmp(rel, "local/", 6) == 0) {
            char extracted_slot[256];
            strncpy(extracted_slot, rel, sizeof(extracted_slot) - 1);
            extracted_slot[sizeof(extracted_slot) - 1] = '\0';
            char *s1 = strchr(extracted_slot, '/');
            if (s1) {
                char *s2 = strchr(s1 + 1, '/');
                if (s2) *s2 = '\0';
            }
            return slot_delete(extracted_slot);
        }
    }

    /* Fallback: match by filename or payload_path */
    const char *target_fn = strrchr(slot_or_filename, '/');
    target_fn = target_fn ? target_fn + 1 : slot_or_filename;

    SlotMeta *slots = NULL;
    size_t count = 0;
    if (slot_list_all(&slots, &count) == 0 && slots) {
        for (size_t i = 0; i < count; i++) {
            if (strcmp(slots[i].filename, target_fn) == 0 ||
                strcmp(slots[i].payload_path, slot_or_filename) == 0) {
                int res = slot_delete(slots[i].slot_key);
                slot_free_all(slots);
                return res;
            }
        }
        slot_free_all(slots);
    }

    return -1;
}

int payload_mgr_check_existing(const char *filename, char *out_json, size_t out_size) {
    JsonListBuilder jb = { out_json, out_size, 0, 1, 0 };
    if (is_pldmgr_name(filename)) {
        json_append(&jb, "{\"status\":\"error\",\"message\":\"Payload Manager updates require the official repository\"}");
        return -1;
    }
    if (!filename || filename[0] == '\0') {
        json_append(&jb, "{\"status\":\"error\",\"message\":\"Missing filename\"}");
        return -1;
    }

    char lkey[128];
    slot_local_key(filename, lkey, sizeof(lkey));

    char check_dir[640];
    snprintf(check_dir, sizeof(check_dir), "%s/local/%s", PAYLOADS_STORAGE_DIR, lkey);

    SlotMeta meta;
    if (slot_read_meta(check_dir, &meta) != 0) {
        json_append(&jb, "{\"error\":\"Failed to read slot metadata\"}");
        return -1;
    }
    int exists = meta.payload_path[0] != '\0';

    char key_e[6 * 128];
    json_escape_checked(&jb, lkey, key_e, sizeof(key_e));

    if (exists) {
        char slot_e[6 * 128], fn_e[6 * 256];
        json_escape_checked(&jb, meta.slot_key, slot_e, sizeof(slot_e));
        json_escape_checked(&jb, meta.filename, fn_e, sizeof(fn_e));
        json_append(&jb,
                 "{\"status\":\"ok\",\"key\":\"%s\",\"folder_exists\":true,\"file_exists\":true,"
                 "\"folder_name\":\"%s\",\"existing\":{\"slot\":\"%s\",\"filename\":\"%s\"}}",
                 key_e, key_e, slot_e, fn_e);
    } else {
        json_append(&jb,
                 "{\"status\":\"ok\",\"key\":\"%s\",\"folder_exists\":false,\"file_exists\":false,"
                 "\"folder_name\":\"%s\",\"existing\":null}",
                 key_e, key_e);
    }
    return jb.error ? -1 : 0;
}

int payload_mgr_import_to_storage_mode(const char *filename, const char *temp_path,
                                       const char *mode, const char *install_source,
                                       const char *install_source_detail,
                                       char *msg_buf, size_t msg_buf_size,
                                       char *out_slot, size_t out_slot_size) {
    if (!filename || strstr(filename, "/") || strstr(filename, "..") || is_pldmgr_name(filename)) {
        snprintf(msg_buf, msg_buf_size, "Invalid filename");
        return -1;
    }

    /* [CN-FORK] 上传中断/空 body 提交过去就是一个自启残包（旧版本就是这么把
     * 0 字节的 NayePlmTools_*.elf 弄进 ps5_autoloader 的），这里挡住并清掉 temp。 */
    if (!payload_file_sane(temp_path)) {
        snprintf(msg_buf, msg_buf_size, "Refused: file is empty or not a valid ELF/BIN");
        pldmgr_log("[PLDMGR] !!! import refused, unsane file: %s\n", temp_path ? temp_path : "(null)");
        if (temp_path) remove(temp_path);
        return -1;
    }

    char lkey[128];
    slot_local_key(filename, lkey, sizeof(lkey));

    char target_slot[128], base_slot[128];
    snprintf(base_slot, sizeof(base_slot), "local/%s", lkey);
    SlotMeta meta;
    memset(&meta, 0, sizeof(meta));
    meta.schema = PAYLOAD_SLOT_SCHEMA;
    strcpy(meta.kind, SLOT_KIND_LOCAL);
    strncpy(meta.filename, filename, sizeof(meta.filename) - 1);

    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    strftime(meta.installed_at, sizeof(meta.installed_at), "%Y-%m-%dT%H:%M:%S%z", &tmv);

    strncpy(meta.origin, install_source ? install_source : "web_upload", sizeof(meta.origin) - 1);
    if (install_source_detail) {
        strncpy(meta.origin_detail, install_source_detail, sizeof(meta.origin_detail) - 1);
    }

    if (slot_commit_file(base_slot, temp_path, mode, &meta, 0, target_slot, sizeof(target_slot))) {
        snprintf(msg_buf, msg_buf_size, "Storage commit failed (conflict, invalid mode, or I/O error)");
        return -1;
    }
    /* On failure the request completion callback still owns the upload. */
    if (remove(temp_path))
        pldmgr_log("[PLDMGR] Upload committed; retaining request file for startup cleanup: %s\n", temp_path);

    if (out_slot && out_slot_size > 0) {
        strncpy(out_slot, target_slot, out_slot_size - 1);
        out_slot[out_slot_size - 1] = '\0';
    }

    pldmgr_log("[PLDMGR] Local import complete: slot=%s filename=%s\n", target_slot, filename);
    snprintf(msg_buf, msg_buf_size, "Imported %s", filename);
    /* [CN-FORK] 同步更新越狱引导器目录（若该工具在其中有登记；本工具没登记就登记）。
     * v0.6.0.178 起直接用 slot_commit_file 填好的 meta.payload_path，不再靠 slot_resolve
     * 反解 —— 反解失败时旧代码是「静默什么都不做」，真机上传成功却不见引导器更新时，
     * 到底是哪一环断了完全看不出来。 */
    char installed_path[640];
    snprintf(installed_path, sizeof(installed_path), "%s", meta.payload_path);
    if (installed_path[0] == '\0')
        snprintf(installed_path, sizeof(installed_path), "%s/%s/%s", PAYLOADS_STORAGE_DIR,
                 target_slot, filename);
    if (access(installed_path, F_OK) != 0)
        pldmgr_log("[AutoloaderSync] installed payload not found (%s) — bootloader sync skipped\n",
                   installed_path);
    else
        payload_mgr_autoloader_sync(filename, installed_path, payload_mgr_is_self_payload(filename));

    /* [CN-FORK] v0.6.1.191 手动上传「本程序」自身：普通导入 + 引导器登记照常走完（保持与
     * 任意 payload 一致的契约，开机仍会被引导器接管），这里只是「额外」把已落盘的这份
     * 复制进自更新 slot（/payloads/pldmgr/），让顶部专属行出现「重启生效」的干净交接。
     * repository_stage_uploaded_self_update 内部只在 ELF 内嵌版本严格高于当前运行、且高于
     * 已 pending 的那份时才发布；不满足就是 no-op，普通卡照常留在列表里。 */
    if (is_self_update_name(filename) && access(installed_path, F_OK) == 0) {
        char staged[64] = "";
        if (repository_stage_uploaded_self_update(installed_path, staged, sizeof(staged)) == 0)
            pldmgr_log("[PLDMGR] Uploaded self-update also staged for restart: v%s\n", staged);
    }
    return 0;
}

int payload_mgr_usb_check(const char *usb_path, char *out_json, size_t out_size) {
    JsonListBuilder jb = { out_json, out_size, 0, 1, 0 };
    if (!is_allowed_usb_path(usb_path) || is_pldmgr_name(usb_path)) {
        json_append(&jb, "{\"error\":\"Invalid USB path\"}");
        return -1;
    }

    struct stat st;
    if (stat(usb_path, &st) != 0) {
        json_append(&jb, "{\"error\":\"File not found\"}");
        return -1;
    }

    const char *filename = strrchr(usb_path, '/');
    filename = filename ? filename + 1 : usb_path;

    char lkey[128];
    slot_local_key(filename, lkey, sizeof(lkey));

    char check_dir[640];
    snprintf(check_dir, sizeof(check_dir), "%s/local/%s", PAYLOADS_STORAGE_DIR, lkey);

    SlotMeta meta;
    if (slot_read_meta(check_dir, &meta) != 0) {
        json_append(&jb, "{\"error\":\"Failed to read slot metadata\"}");
        return -1;
    }
    int exists = meta.payload_path[0] != '\0';
    char usb_hash[65], installed_hash[65];
    if (!S_ISREG(st.st_mode) || st.st_size <= 0 || compute_sha256_file(usb_path, usb_hash) ||
        (exists && compute_sha256_file(meta.payload_path, installed_hash))) {
        json_append(&jb, "{\"error\":\"Failed to verify payload contents\"}"); return -1;
    }

    char name_e[6 * 256], path_e[6 * 512], key_e[6 * 128];
    json_escape_checked(&jb, filename, name_e, sizeof(name_e));
    json_escape_checked(&jb, usb_path, path_e, sizeof(path_e));
    json_escape_checked(&jb, lkey, key_e, sizeof(key_e));

    if (exists) {
        char slot_e[6 * 128], exfn_e[6 * 256];
        json_escape_checked(&jb, meta.slot_key, slot_e, sizeof(slot_e));
        json_escape_checked(&jb, meta.filename, exfn_e, sizeof(exfn_e));

        int is_same = !strcmp(meta.filename, filename) && !strcmp(usb_hash, installed_hash);
        json_append(&jb,
                 "{\"filename\":\"%s\",\"path\":\"%s\",\"size\":%lld,\"sha256\":\"%s\",\"key\":\"%s\","
                 "\"status\":\"%s\",\"folder_exists\":true,\"folder_name\":\"%s\","
                 "\"existing\":{\"slot\":\"%s\",\"filename\":\"%s\"}}",
                 name_e, path_e, (long long)st.st_size, usb_hash, key_e,
                 is_same ? "exists_same" : "exists_different", key_e,
                 slot_e, exfn_e);
    } else {
        json_append(&jb,
                 "{\"filename\":\"%s\",\"path\":\"%s\",\"size\":%lld,\"sha256\":\"%s\",\"key\":\"%s\","
                 "\"status\":\"ready\",\"folder_exists\":false,\"folder_name\":\"%s\","
                 "\"existing\":null}",
                 name_e, path_e, (long long)st.st_size, usb_hash, key_e, key_e);
    }
    return jb.error ? -1 : 0;
}

int payload_mgr_usb_move(const char *usb_path, const char *mode, int keep_original,
                         char *out_json, size_t out_size) {
    JsonListBuilder jb = { out_json, out_size, 0, 1, 0 };
    if (!is_allowed_usb_path(usb_path) || is_pldmgr_name(usb_path)) {
        json_append(&jb, "{\"ok\":false,\"message\":\"Invalid USB path\"}");
        return -1;
    }

    struct stat st;
    if (stat(usb_path, &st) != 0) {
        json_append(&jb, "{\"ok\":false,\"message\":\"File not found\"}");
        return -1;
    }

    const char *filename = strrchr(usb_path, '/');
    filename = filename ? filename + 1 : usb_path;

    char lkey[128];
    slot_local_key(filename, lkey, sizeof(lkey));

    char target_slot[128], base_slot[128];
    snprintf(base_slot, sizeof(base_slot), "local/%s", lkey);
    SlotMeta meta;
    memset(&meta, 0, sizeof(meta));
    meta.schema = PAYLOAD_SLOT_SCHEMA;
    strcpy(meta.kind, SLOT_KIND_LOCAL);
    strncpy(meta.filename, filename, sizeof(meta.filename) - 1);

    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    strftime(meta.installed_at, sizeof(meta.installed_at), "%Y-%m-%dT%H:%M:%S%z", &tmv);

    strcpy(meta.origin, "usb");
    strncpy(meta.origin_detail, usb_path, sizeof(meta.origin_detail) - 1);

    if (slot_commit_file(base_slot, usb_path, mode, &meta, 0, target_slot, sizeof(target_slot))) {
        json_append(&jb, "{\"ok\":false,\"message\":\"Storage commit failed (conflict, invalid mode, or I/O error)\"}");
        return -1;
    }

    /* [CN-FORK] v0.6.0.178 从 U 盘导入同样是一次「装载荷」：引导器目录里有这个工具时
     * 要换版本、本工具没登记时要登记。此前只有网页上传那条路会同步，U 盘导入整条漏掉 ——
     * 从 U 盘装新版之后 /data/ps5_autoloader/ 停在旧版本，就是这个原因。 */
    if (meta.payload_path[0] != '\0' && access(meta.payload_path, F_OK) == 0)
        payload_mgr_autoloader_sync(filename, meta.payload_path, payload_mgr_is_self_payload(filename));
    else
        pldmgr_log("[AutoloaderSync] installed payload not found (%s) — bootloader sync skipped\n",
                   meta.payload_path);

    if (!keep_original) {
        char original_hash[65], copied_hash[65];
        struct stat current;
        int unchanged = !stat(usb_path, &current) && current.st_dev == st.st_dev &&
            current.st_ino == st.st_ino && current.st_size == st.st_size &&
            !compute_sha256_file(usb_path, original_hash) &&
            !compute_sha256_file(meta.payload_path, copied_hash) && !strcmp(original_hash, copied_hash);
        if (!unchanged || remove(usb_path) != 0) {
            pldmgr_log("[PLDMGR] Warning: Failed to remove original file from USB: %s\n", usb_path);
            json_append(&jb,
                     "{\"ok\":true,\"slot\":\"%s\",\"message\":\"Moved successfully\","
                     "\"warning\":\"Payload copied, but failed to delete original from USB.\"}",
                     target_slot);
        } else {
            pldmgr_log("[PLDMGR] USB payload moved: %s -> %s\n", usb_path, meta.payload_path);
            json_append(&jb, "{\"ok\":true,\"slot\":\"%s\",\"message\":\"Moved successfully\"}",
                     target_slot);
        }
    } else {
        pldmgr_log("[PLDMGR] USB payload copied: %s -> %s\n", usb_path, meta.payload_path);
        json_append(&jb, "{\"ok\":true,\"slot\":\"%s\",\"message\":\"Copied successfully\"}",
                 target_slot);
    }

    return jb.error ? -1 : 0;
}
