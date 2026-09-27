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

static int copy_file(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    if (!in)
        return -1;
    FILE *out = fopen(dst, "wb");
    if (!out) {
        fclose(in);
        return -1;
    }
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            fclose(in);
            fclose(out);
            return -1;
        }
    }
    fclose(in);
    fclose(out);
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
    char   old_filename[256] = "";

    while (line_count < 100 && fgets(lines[line_count], (int)sizeof(lines[0]), f)) {
        char clean[256];
        strncpy(clean, lines[line_count], sizeof(clean) - 1);
        clean[sizeof(clean) - 1] = '\0';
        clean[strcspn(clean, "\r\n")] = '\0';

        char folder[128];
        pldmgr_utils_get_payload_folder_name(clean, folder, sizeof(folder));

        if (strcasecmp(folder, new_folder) == 0 && strcmp(clean, new_filename) != 0) {
            strncpy(old_filename, clean, sizeof(old_filename) - 1);
            old_filename[sizeof(old_filename) - 1] = '\0';
            snprintf(lines[line_count], sizeof(lines[0]), "%s\n", new_filename);
            matched = 1;
        }
        line_count++;
    }
    fclose(f);

    if (!matched) return;

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
        for (int i = 0; i < line_count; i++) fputs(lines[i], f);
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
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        size_t len = strlen(entry->d_name);
        if (len > 4 && strcasecmp(entry->d_name + len - 4, ".elf") == 0) {
            strncpy(installed_filename, entry->d_name, sizeof(installed_filename) - 1);
            snprintf(installed_fullpath, sizeof(installed_fullpath), "%s/%s", dir, entry->d_name);
            break;
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
    f = fopen(PS5_AUTOLOADER_CONFIG, "a");
    if (f) {
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
