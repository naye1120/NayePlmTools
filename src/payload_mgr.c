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
#include "utils.h"   /* [CN-FORK] ps5_autoloader_sync 需要 pldmgr_utils_get_payload_folder_name */
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

/* [CN-FORK] 安装 payload 后，若 /data/ps5_autoloader/autoload.txt 中已登记同一工具
 * （按去除版本号的 folder_name 匹配），则把新 ELF 同步到该目录、更新 txt、删除旧文件。
 * 匹不到则静默返回，不影响正常安装流程。 */
static void ps5_autoloader_sync(const char *new_filename, const char *new_file_path) {
    FILE *f = fopen(PS5_AUTOLOADER_CONFIG, "r");
    if (!f) return; /* 引导器不存在，跳过 */

    char new_folder[128];
    pldmgr_utils_get_payload_folder_name(new_filename, new_folder, sizeof(new_folder));

    char   lines[100][256];
    int    line_count = 0;
    int    matched    = 0;
    int    same_name  = 0;
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

    /* [CN-FORK] 不管走哪条分支，本工具那一行上面都必须有 !延时行，且中间不留空行。
     * 旧逻辑只在「首次追加」时写延时行，换版本号时只替文件名，手改删掉就再也没人补。 */
    int delay_fix = (our_idx >= 0) ? ensure_delay_before(lines, &line_count, our_idx) : 0;
    if (delay_fix & 1)
        pldmgr_log("[AutoloaderSync] added missing '!1000' delay line before %s\n", new_filename);
    if (delay_fix & 2)
        pldmgr_log("[AutoloaderSync] trimmed blank line(s) above %s\n", new_filename);
    int need_rewrite = matched || delay_fix;

    if (!matched) {
        /* [CN-FORK] 旧逻辑到这里直接返回：同版本已登记时永远不动手，于是引导器目录里
         * 那个 0 字节残包没人修 —— 实机就是卡在这个状态（autoloader 加载空文件）。 */
        if (same_name) {
            char cur[512];
            snprintf(cur, sizeof(cur), "%s/%s", PS5_AUTOLOADER_DIR, new_filename);
            if (!payload_file_sane(cur) && copy_file(new_file_path, cur) == 0)
                pldmgr_log("[AutoloaderSync] healed broken copy: %s\n", cur);
        }
        if (need_rewrite) {
            f = fopen(PS5_AUTOLOADER_CONFIG, "w");
            if (f) {
                write_autoload_lines(f, lines, line_count);
                fclose(f);
            }
        }
        return;
    }

    /* 复制新 ELF 到引导器目录 */
    char dest[512];
    snprintf(dest, sizeof(dest), "%s/%s", PS5_AUTOLOADER_DIR, new_filename);
    if (copy_file(new_file_path, dest) != 0) {
        pldmgr_log("[AutoloaderSync] FAILED to copy %s to ps5_autoloader\n", new_filename);
        return;
    }

    /* 删除引导器目录里的旧版本 */
    if (old_filename[0]) {
        char old_path[512];
        snprintf(old_path, sizeof(old_path), "%s/%s", PS5_AUTOLOADER_DIR, old_filename);
        remove(old_path);
    }

    /* 写回更新后的 autoload.txt */
    f = fopen(PS5_AUTOLOADER_CONFIG, "w");
    if (f) {
        write_autoload_lines(f, lines, line_count);
        fclose(f);
    }

    pldmgr_log("[AutoloaderSync] Updated ps5_autoloader: %s -> %s\n", old_filename, new_filename);
}

/* [CN-FORK] 启动自检同步：解决旧版本没有 sync 代码导致首次升级到 .20 时 ps5_autoloader 未更新。
 * 在 storage 中找已安装的 NayePlmTools ELF：
 *   - 若 autoload.txt 已有同名工具 → 更新版本（由 ps5_autoloader_sync 完成）
 *   - 若 autoload.txt 没有 → 主动登记（复制 ELF + 追加文件名）
 *   - 若 ps5_autoloader/ 不存在 → 静默跳过 */
void payload_mgr_bootstrap_self_sync(void) {
    /* 引导器目录不存在则跳过 */
    struct stat st;
    if (stat(PS5_AUTOLOADER_DIR, &st) != 0 || !S_ISDIR(st.st_mode)) return;

    /* 当前版本名推算 folder_name（结果必为 "NayePlmTools"） */
    char probe[128];
    snprintf(probe, sizeof(probe), "NayePlmTools_%s.elf", MENU_VERSION);
    char folder[128];
    pldmgr_utils_get_payload_folder_name(probe, folder, sizeof(folder));

    char dir[512];
    snprintf(dir, sizeof(dir), "%s/%s", PAYLOADS_STORAGE_DIR, folder);
    DIR *d = opendir(dir);
    if (!d) return; /* 尚未安装到 storage，跳过 */

    char installed_filename[256] = "";
    char installed_fullpath[640] = "";
    time_t newest = 0;
    struct dirent *entry;
    /* storage 目录正常只留一份（import 时会清旧），但手工 FTP 塞进来的可能有多份，
     * 其中还可能包括 0 字节残包 —— 取「合法里最新」的，而不是 readdir 的随机首个。 */
    while ((entry = readdir(d)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        size_t len = strlen(entry->d_name);
        if (len <= 4 || strcasecmp(entry->d_name + len - 4, ".elf") != 0) continue;
        char cand[640];
        snprintf(cand, sizeof(cand), "%s/%s", dir, entry->d_name);
        if (!payload_file_sane(cand)) {
            pldmgr_log("[AutoloaderSync] skip invalid payload copy: %s\n", cand);
            continue;
        }
        struct stat fst;
        if (stat(cand, &fst) != 0) continue;
        if (!installed_filename[0] || fst.st_mtime >= newest) {
            newest = fst.st_mtime;
            strncpy(installed_filename, entry->d_name, sizeof(installed_filename) - 1);
            installed_filename[sizeof(installed_filename) - 1] = '\0';
            snprintf(installed_fullpath, sizeof(installed_fullpath), "%s", cand);
        }
    }
    closedir(d);
    if (!installed_filename[0]) return;

    /* 先尝试更新已有登记（folder_name 匹配但版本不同的情况） */
    ps5_autoloader_sync(installed_filename, installed_fullpath);

    /* 若 autoload.txt 里根本没有这个工具，则主动登记 */
    FILE *f = fopen(PS5_AUTOLOADER_CONFIG, "r");
    int found = 0;
    if (f) {
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            char clean[256];
            strncpy(clean, line, sizeof(clean) - 1);
            clean[sizeof(clean) - 1] = '\0';
            clean[strcspn(clean, "\r\n")] = '\0';
            pldmgr_utils_strip_config_line(clean); /* [CN-FORK] 同上：别因为手改的空白就重复追加 */
            if (strcmp(clean, installed_filename) == 0) { found = 1; break; }
            char lf[128];
            pldmgr_utils_get_payload_folder_name(clean, lf, sizeof(lf));
            if (strcasecmp(lf, folder) == 0) { found = 1; break; }
        }
        fclose(f);
    }
    if (found) return; /* 已登记（sync 已处理版本更新），无需追加 */

    /* 复制 ELF 到引导器目录 */
    char dest[512];
    snprintf(dest, sizeof(dest), "%s/%s", PS5_AUTOLOADER_DIR, installed_filename);
    if (copy_file(installed_fullpath, dest) != 0) {
        pldmgr_log("[AutoloaderSync] bootstrap: FAILED to copy %s\n", installed_filename);
        return;
    }

    /* 追加文件名到 autoload.txt（格式参考 nayeplmtools/autoload.txt：!延时 + 文件名） */
    /* [CN-FORK] 先探一下末尾：上一行没换行符就补一个，否则「!1000」会粘到上一条
     * 尾巴上（变成 other.bin!1000），两条一起坏。用单独的 rb 探，
     * 因为 "a" 模式的流上 fseek/fgetc 读不可靠。 */
    int need_nl = 0;
    FILE *rd = fopen(PS5_AUTOLOADER_CONFIG, "rb");
    if (rd) {
        if (fseek(rd, -1, SEEK_END) == 0) {
            int c = fgetc(rd);
            need_nl = (c != EOF && c != '\n');
        }
        fclose(rd);
    }
    f = fopen(PS5_AUTOLOADER_CONFIG, "a");
    if (f) {
        if (need_nl) fputc('\n', f);
        fprintf(f, "!1000\n%s\n", installed_filename);
        fclose(f);
    }
    pldmgr_log("[AutoloaderSync] bootstrap: registered %s in ps5_autoloader\n", installed_filename);
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
    /* [CN-FORK] 同步更新越狱引导器目录（若该工具在其中有登记）。
     * v0.6.0 起落点由 slot 管理，这里把刚写入的 slot key 解回真实路径。 */
    char final_path[640];
    if (slot_resolve(target_slot, final_path, sizeof(final_path)) == 0)
        ps5_autoloader_sync(filename, final_path);
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
