/*
 * Payload Manager — Local payload storage management.
 *
 * Handles scanning, listing, importing, deleting, and USB transfer
 * of ELF/BIN payload files on the PS5.
 */

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
#include "json_helpers.h"
#include "sha256.h"
#include "repository.h"
#include "config.h"
#include "utils.h"   /* [CN-FORK] ps5_autoloader_sync 需要 pldmgr_utils_get_payload_folder_name */
#include "pldmgr.h"

/* ── Extension / path checks ──────────────────────────────── */

static int is_supported_extension(const char *path) {
    const char *ext = strrchr(path, '.');
    if (!ext)
        return 0;
    return (strcasecmp(ext, ".elf") == 0 || strcasecmp(ext, ".bin") == 0);
}

static int is_allowed_usb_path(const char *path) {
    if (!path || path[0] == '\0')
        return 0;
    if (strstr(path, ".."))
        return 0;
    if (!is_supported_extension(path))
        return 0;
    if (strncmp(path, "/mnt/usb", 8) == 0 && path[8] >= '0' && path[8] <= '7' &&
        path[9] == '/')
        return 1;
    return 0;
}

/* ── File utilities ────────────────────────────────────────── */

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

/* Remove all files in a payload directory EXCEPT the new incoming file.
 * Also updates the autoload config if a file is being replaced. */
void payload_mgr_remove_old_files(const char *dir_path, const char *new_filename) {
    DIR *d = opendir(dir_path);
    if (!d) return;

    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        if (strcmp(entry->d_name, new_filename) == 0) continue;
        /* Skip .json metadata files for the incoming file */
        {
            size_t new_len = strlen(new_filename);
            size_t ent_len = strlen(entry->d_name);
            if (ent_len == new_len + 5 &&
                strncmp(entry->d_name, new_filename, new_len) == 0 &&
                strcmp(entry->d_name + new_len, ".json") == 0)
                continue;
        }

        char full_path[640];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);

        /* If it's a supported payload, update autoload config */
        if (is_supported_extension(entry->d_name)) {
            pldmgr_autoload_update_config_entry(entry->d_name, new_filename);
        }

        remove(full_path);
    }
    closedir(d);
}

/* ── Recursive scan ────────────────────────────────────────── */

typedef struct {
    char path[512];
    char filename[256];
    int  is_storage;
    long mtime;
} PayloadEntry;

static void scan_payloads_recursive(const char *dir_path, int is_storage,
                                    PayloadEntry **out, int *count, int max, int max_depth) {
    if (max_depth < 0) return;

    DIR *d = opendir(dir_path);
    if (!d) return;

    struct dirent *entry;
    while (*count < max && (entry = readdir(d)) != NULL) {
        if (entry->d_name[0] == '.')
            continue;

        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) != 0)
            continue;

        if (S_ISDIR(st.st_mode)) {
            scan_payloads_recursive(full_path, is_storage, out, count, max, max_depth - 1);
            continue;
        }

        if (!is_supported_extension(entry->d_name))
            continue;

        PayloadEntry *tmp = realloc(*out, (*count + 1) * sizeof(PayloadEntry));
        if (!tmp) break;
        *out = tmp;

        strncpy((*out)[*count].path, full_path, sizeof((*out)[*count].path) - 1);
        (*out)[*count].path[sizeof((*out)[*count].path) - 1] = '\0';
        strncpy((*out)[*count].filename, entry->d_name, sizeof((*out)[*count].filename) - 1);
        (*out)[*count].filename[sizeof((*out)[*count].filename) - 1] = '\0';
        (*out)[*count].is_storage = is_storage;
        (*out)[*count].mtime = (long)st.st_mtime;
        (*count)++;
    }
    closedir(d);
}

/* ── Recursive resolve ─────────────────────────────────────── */

static int resolve_recursive(const char *dir_path, const char *filename,
                             char *out_path, size_t out_size) {
    DIR *d = opendir(dir_path);
    if (!d) return -1;

    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (entry->d_name[0] == '.')
            continue;

        char full[512];
        snprintf(full, sizeof(full), "%s/%s", dir_path, entry->d_name);

        struct stat st;
        if (stat(full, &st) != 0)
            continue;

        if (S_ISDIR(st.st_mode)) {
            if (resolve_recursive(full, filename, out_path, out_size) == 0) {
                closedir(d);
                return 0;
            }
            continue;
        }

        if (strcmp(entry->d_name, filename) == 0) {
            strncpy(out_path, full, out_size - 1);
            out_path[out_size - 1] = '\0';
            closedir(d);
            return 0;
        }
    }
    closedir(d);
    return -1;
}

/* ── Public API ────────────────────────────────────────────── */

size_t payload_mgr_list_json(char *json_buf, size_t buf_size) {
    PayloadEntry *payloads = NULL;
    int count = 0;
    int max = 256;
    int scan_usb = config_read_bool("SCAN_USB_PAYLOADS", 0);

    /* Scan pldmgr subdirs (this includes PAYLOADS_STORAGE_DIR under /data/nayeplmtools) */
    for (int i = 0; i < SCAN_DIRS_COUNT; i++) {
        scan_payloads_recursive(SCAN_DIRS[i], 0, &payloads, &count, max, 3);
    }

    /* Optionally scan USB root directories */
    if (scan_usb) {
        for (int usb = 0; usb < 8; usb++) {
            char usb_root[64];
            snprintf(usb_root, sizeof(usb_root), "/mnt/usb%d", usb);
            struct stat st;
            if (stat(usb_root, &st) == 0 && S_ISDIR(st.st_mode))
                scan_payloads_recursive(usb_root, 0, &payloads, &count, max, 0);
        }
    }

    /* Build JSON response */
    JsonListBuilder jb = { json_buf, buf_size, 0, 1 };
    json_append(&jb, "{\"payloads\":[");

    for (int i = 0; i < count; i++) {
        char path_escaped[1024];
        pldmgr_json_escape(payloads[i].path, path_escaped, sizeof(path_escaped));
        json_append(&jb, "%s\"%s\"", (i > 0) ? "," : "", path_escaped);
    }

    json_append(&jb, "],\"meta\":{");

    int meta_first = 1;
    for (int i = 0; i < count; i++) {
        /* Try to load sidecar .json metadata */
        char details_path[640];
        snprintf(details_path, sizeof(details_path), "%s.json", payloads[i].path);
        char *details_json = NULL;
        size_t details_size = 0;

        if (read_file_text(details_path, &details_json, &details_size) == 0 && details_json) {
            /* Extract fields from sidecar */
            char d_name[256] = "", d_desc[1024] = "", d_ver[64] = "", d_url[1024] = "";
            char d_src[1024] = "", d_src_direct[1024] = "", d_last_update[64] = "";
            char d_checksum[65] = "", d_downloaded[64] = "";
            char d_install_src[128] = "", d_install_detail[1024] = "";
            char d_source_name[256] = "";

            const char *end = details_json + details_size;
            json_extract_string(details_json, end, "name", d_name, sizeof(d_name));
            json_extract_string(details_json, end, "description", d_desc, sizeof(d_desc));
            json_extract_string(details_json, end, "version", d_ver, sizeof(d_ver));
            json_extract_string(details_json, end, "url", d_url, sizeof(d_url));
            json_extract_string(details_json, end, "source", d_src, sizeof(d_src));
            json_extract_string(details_json, end, "source_direct", d_src_direct, sizeof(d_src_direct));
            json_extract_string(details_json, end, "last_update", d_last_update, sizeof(d_last_update));
            json_extract_string(details_json, end, "checksum", d_checksum, sizeof(d_checksum));
            json_extract_string(details_json, end, "downloaded_at", d_downloaded, sizeof(d_downloaded));
            json_extract_string(details_json, end, "install_source", d_install_src, sizeof(d_install_src));
            json_extract_string(details_json, end, "install_source_detail", d_install_detail, sizeof(d_install_detail));
            json_extract_string(details_json, end, "source_name", d_source_name, sizeof(d_source_name));

            char filename_escaped[512], ne[512], de[2048], ve[128], ue[2048], se[2048], sde[2048];
            char lue[128], ce[128], dae[128], ise[256], ide[2048], sne[512];
            pldmgr_json_escape(payloads[i].filename, filename_escaped, sizeof(filename_escaped));
            pldmgr_json_escape(d_name, ne, sizeof(ne));
            pldmgr_json_escape(d_desc, de, sizeof(de));
            pldmgr_json_escape(d_ver, ve, sizeof(ve));
            pldmgr_json_escape(d_url, ue, sizeof(ue));
            pldmgr_json_escape(d_src, se, sizeof(se));
            pldmgr_json_escape(d_src_direct, sde, sizeof(sde));
            pldmgr_json_escape(d_last_update, lue, sizeof(lue));
            pldmgr_json_escape(d_checksum, ce, sizeof(ce));
            pldmgr_json_escape(d_downloaded, dae, sizeof(dae));
            pldmgr_json_escape(d_install_src, ise, sizeof(ise));
            pldmgr_json_escape(d_install_detail, ide, sizeof(ide));
            pldmgr_json_escape(d_source_name, sne, sizeof(sne));

            json_append(&jb, "%s\"%s\":{"
                        "\"display_name\":\"%s\",\"description\":\"%s\",\"version\":\"%s\","
                        "\"url\":\"%s\",\"source\":\"%s\",\"source_direct\":\"%s\","
                        "\"last_update\":\"%s\",\"checksum\":\"%s\",\"downloaded_at\":\"%s\","
                        "\"install_source\":\"%s\",\"install_source_detail\":\"%s\","
                        "\"source_name\":\"%s\"}",
                        meta_first ? "" : ",",
                        filename_escaped,
                        ne, de, ve, ue, se, sde, lue, ce, dae, ise, ide, sne);
            
            meta_first = 0;
            free(details_json);
        }
    }

    json_append(&jb, "}}");

    if (payloads)
        free(payloads);

    return jb.pos;
}

int payload_mgr_resolve_path(const char *filename, char *out_path, size_t out_size) {
    for (int i = 0; i < SCAN_DIRS_COUNT; i++) {
        if (resolve_recursive(SCAN_DIRS[i], filename, out_path, out_size) == 0)
            return 0;
    }
    return -1;
}

int payload_mgr_delete_payload_file(const char *filename) {
    char resolved[512];
    if (!filename || strstr(filename, "/") || strstr(filename, ".."))
        return -1;

    if (payload_mgr_resolve_path(filename, resolved, sizeof(resolved)) != 0)
        return -1;

    /* Also remove .json sidecar */
    char meta_path[640];
    snprintf(meta_path, sizeof(meta_path), "%s.json", resolved);
    remove(meta_path);

    /* Update autoload config */
    pldmgr_autoload_update_config_entry(filename, NULL);

    return remove(resolved);
}

int payload_mgr_import_to_storage(const char *filename, const char *temp_path,
                                  const char *install_source, const char *install_source_detail,
                                  char *msg_buf, size_t msg_buf_size) {
    if (!filename || strstr(filename, "/") || strstr(filename, "..")) {
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

    char folder_name[128];
    pldmgr_utils_get_payload_folder_name(filename, folder_name, sizeof(folder_name));

    char payload_dir[512];
    snprintf(payload_dir, sizeof(payload_dir), "%s/%s", PAYLOADS_STORAGE_DIR, folder_name);
    if (ensure_dir_recursive(payload_dir) != 0) {
        snprintf(msg_buf, msg_buf_size, "Failed to create directory");
        return -1;
    }

    char final_path[640];
    snprintf(final_path, sizeof(final_path), "%s/%s", payload_dir, filename);

    /* Remove old files in the payload directory */
    payload_mgr_remove_old_files(payload_dir, filename);

    if (rename(temp_path, final_path) != 0) {
        snprintf(msg_buf, msg_buf_size, "Failed to move file");
        return -1;
    }

    /* Write metadata sidecar */
    char details_path[700];
    snprintf(details_path, sizeof(details_path), "%s/%s.json", payload_dir, filename);
    write_simple_payload_details_json(filename, details_path, install_source, install_source_detail);

    pldmgr_log("[PLDMGR] Imported payload: %s -> %s\n", filename, final_path);
    snprintf(msg_buf, msg_buf_size, "Imported %s", filename);
    /* [CN-FORK] 同步更新越狱引导器目录（若该工具在其中有登记） */
    ps5_autoloader_sync(filename, final_path);
    return 0;
}

int payload_mgr_check_existing(const char *filename, char *out_json, size_t out_size) {
    char folder_name[128];
    char folder_path[512];
    char file_path[640];
    struct stat st;

    pldmgr_utils_get_payload_folder_name(filename, folder_name, sizeof(folder_name));
    snprintf(folder_path, sizeof(folder_path), "%s/%s", PAYLOADS_STORAGE_DIR, folder_name);
    snprintf(file_path, sizeof(file_path), "%s/%s", folder_path, filename);

    int folder_exists = (stat(folder_path, &st) == 0 && S_ISDIR(st.st_mode));
    int file_exists = (stat(file_path, &st) == 0 && S_ISREG(st.st_mode));

    snprintf(out_json, out_size, "{\"status\":\"ok\",\"folder_exists\":%s,\"file_exists\":%s,\"folder_name\":\"%s\"}",
             folder_exists ? "true" : "false", file_exists ? "true" : "false", folder_name);
    return 0;
}

int payload_mgr_write_metadata(const char *payload_path, const char *install_source,
                               const char *install_source_detail) {
    char details_path[700];
    const char *filename = strrchr(payload_path, '/');
    filename = filename ? filename + 1 : payload_path;
    snprintf(details_path, sizeof(details_path), "%s.json", payload_path);
    return write_simple_payload_details_json(filename, details_path, install_source, install_source_detail);
}

/* ── USB Check / Move ──────────────────────────────────────── */

int payload_mgr_usb_check(const char *usb_path, char *out_json, size_t out_size) {
    if (!is_allowed_usb_path(usb_path)) {
        snprintf(out_json, out_size, "{\"error\":\"Invalid USB path\"}");
        return -1;
    }

    struct stat st;
    if (stat(usb_path, &st) != 0) {
        snprintf(out_json, out_size, "{\"error\":\"File not found\"}");
        return -1;
    }

    const char *filename = strrchr(usb_path, '/');
    filename = filename ? filename + 1 : usb_path;

    char existing_path[512];
    int exists = (payload_mgr_resolve_path(filename, existing_path, sizeof(existing_path)) == 0);

    char name_e[512], path_e[1024];
    pldmgr_json_escape(filename, name_e, sizeof(name_e));
    pldmgr_json_escape(usb_path, path_e, sizeof(path_e));

    snprintf(out_json, out_size,
             "{\"filename\":\"%s\",\"path\":\"%s\",\"size\":%lld,\"exists\":%s}",
             name_e, path_e, (long long)st.st_size, exists ? "true" : "false");
    return 0;
}

int payload_mgr_usb_move(const char *usb_path, int overwrite, int keep_original, char *out_json,
                         size_t out_size) {
    if (!is_allowed_usb_path(usb_path)) {
        snprintf(out_json, out_size, "{\"ok\":false,\"message\":\"Invalid USB path\"}");
        return -1;
    }

    struct stat st;
    if (stat(usb_path, &st) != 0) {
        snprintf(out_json, out_size, "{\"ok\":false,\"message\":\"File not found\"}");
        return -1;
    }

    const char *filename = strrchr(usb_path, '/');
    filename = filename ? filename + 1 : usb_path;

    char folder_name[128];
    pldmgr_utils_get_payload_folder_name(filename, folder_name, sizeof(folder_name));

    char payload_dir[512];
    snprintf(payload_dir, sizeof(payload_dir), "%s/%s", PAYLOADS_STORAGE_DIR, folder_name);
    ensure_dir_recursive(payload_dir);

    char final_path[640];
    snprintf(final_path, sizeof(final_path), "%s/%s", payload_dir, filename);

    /* Check for existing file */
    if (!overwrite && access(final_path, F_OK) == 0) {
        snprintf(out_json, out_size, "{\"ok\":false,\"message\":\"File already exists\","
                 "\"exists\":true}");
        return -1;
    }

    /* Remove old files if overwriting */
    if (overwrite) {
        payload_mgr_remove_old_files(payload_dir, filename);
    }

    if (copy_file(usb_path, final_path) != 0) {
        snprintf(out_json, out_size, "{\"ok\":false,\"message\":\"Copy failed\"}");
        return -1;
    }

    /* Write metadata */
    char details_path[700];
    snprintf(details_path, sizeof(details_path), "%s/%s.json", payload_dir, filename);
    write_simple_payload_details_json(filename, details_path, "usb", usb_path);

    /* Remove the original file if not copying */
    if (!keep_original) {
        if (remove(usb_path) != 0) {
            pldmgr_log("[PLDMGR] Warning: Failed to remove original file from USB: %s\n", usb_path);
            snprintf(out_json, out_size, "{\"ok\":true,\"message\":\"Moved successfully\",\"warning\":\"Payload copied, but failed to delete original from USB.\"}");
        } else {
            pldmgr_log("[PLDMGR] USB payload moved: %s -> %s\n", usb_path, final_path);
            snprintf(out_json, out_size, "{\"ok\":true,\"message\":\"Moved successfully\"}");
        }
    } else {
        pldmgr_log("[PLDMGR] USB payload copied: %s -> %s\n", usb_path, final_path);
        snprintf(out_json, out_size, "{\"ok\":true,\"message\":\"Copied successfully\"}");
    }
    return 0;
}
