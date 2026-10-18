#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>
#include <curl/curl.h>
#include <stdint.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#include "sha256.h"
#include "utils.h"

#include "repository.h"
#include "collection_helpers.h"
#include "sources.h"
#include "slot.h"
#include "json_helpers.h"
#include "pldmgr.h"
#include "config.h" /* [CN-FORK] repository_ensure_fresh 的 DEFAULT_REPO_ENABLED 开关 */
#include "assets_cacert_pem.h"

/* ── cURL download helper ──────────────────────────────────── */

typedef struct { FILE *file; size_t bytes, limit; } DownloadOutput;
static size_t write_data(void *ptr, size_t size, size_t nmemb, void *context) {
    DownloadOutput *out = context;
    if (size && nmemb > SIZE_MAX / size) return 0;
    size_t bytes = size * nmemb;
    if (bytes > out->limit - out->bytes) return 0;
    size_t written = fwrite(ptr, 1, bytes, out->file);
    out->bytes += written;
    return written;
}

int download_to_file_limited(const char *url, const char *out_path, size_t limit) {
    if (!url || !out_path || !limit ||
        (strncasecmp(url, "https://", 8) && strncasecmp(url, "http://", 7))) return -1;
    FILE *fp = fopen(out_path, "wb");
    if (!fp) return -1;
    CURL *curl = curl_easy_init();
    CURLcode res = CURLE_FAILED_INIT;
    DownloadOutput output = { fp, 0, limit };
    if (curl) {
#define SET_OPTION(option, value) do { res = curl_easy_setopt(curl, option, value); if (res != CURLE_OK) goto finished; } while (0)
        SET_OPTION(CURLOPT_URL, url);
        SET_OPTION(CURLOPT_NOSIGNAL, 1L);
        SET_OPTION(CURLOPT_CONNECTTIMEOUT, strcmp(url, REPOSITORY_SOURCE_URL) ? 10L : 3L);
        SET_OPTION(CURLOPT_TIMEOUT, strcmp(url, REPOSITORY_SOURCE_URL) ? 60L : 5L);
        SET_OPTION(CURLOPT_WRITEFUNCTION, write_data);
        SET_OPTION(CURLOPT_WRITEDATA, &output);
        SET_OPTION(CURLOPT_USERAGENT, "pldmgr/1.0");
        struct curl_blob blob = { (void *)assets_cacert_pem, assets_cacert_pem_len, CURL_BLOB_COPY };
        SET_OPTION(CURLOPT_CAINFO_BLOB, &blob);
        SET_OPTION(CURLOPT_SSL_VERIFYPEER, 1L);
        SET_OPTION(CURLOPT_SSL_VERIFYHOST, 2L);
        SET_OPTION(CURLOPT_FOLLOWLOCATION, 1L);
        SET_OPTION(CURLOPT_MAXREDIRS, 5L);
        SET_OPTION(CURLOPT_MAXFILESIZE_LARGE, (curl_off_t)limit);
#if LIBCURL_VERSION_NUM >= 0x075500
        SET_OPTION(CURLOPT_PROTOCOLS_STR, "http,https");
        SET_OPTION(CURLOPT_REDIR_PROTOCOLS_STR, strncasecmp(url, "https://", 8) ? "http,https" : "https");
#else
        SET_OPTION(CURLOPT_PROTOCOLS, (long)(CURLPROTO_HTTP | CURLPROTO_HTTPS));
        SET_OPTION(CURLOPT_REDIR_PROTOCOLS, strncasecmp(url, "https://", 8) ? (long)(CURLPROTO_HTTP | CURLPROTO_HTTPS) : (long)CURLPROTO_HTTPS);
#endif
        res = curl_easy_perform(curl);
        long status = 0;
        if (res == CURLE_OK && (curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status) != CURLE_OK || status != 200))
            res = CURLE_HTTP_RETURNED_ERROR;
finished:
        if (res != CURLE_OK) pldmgr_log("[PLDMGR] Verified download failed: %s (%s)\n", url, curl_easy_strerror(res));
        curl_easy_cleanup(curl);
#undef SET_OPTION
    }
    int failed = res != CURLE_OK || ferror(fp);
    if (fflush(fp)) failed = 1;
    if (!failed && fsync(fileno(fp))) failed = 1;
    if (fclose(fp)) failed = 1;
    if (failed) { remove(out_path); return -1; }
    return 0;
}

int download_to_file(const char *url, const char *out_path) {
    return download_to_file_limited(url, out_path, PLDMGR_PAYLOAD_DOWNLOAD_LIMIT);
}

int repository_checksum_valid(const char *checksum) {
    if (!checksum) return 0;
    if (!checksum[0]) return 1; /* Missing/empty remains optional. */
    if (strlen(checksum) != 64) return 0;
    for (size_t i = 0; i < 64; i++)
        if (!((checksum[i] >= '0' && checksum[i] <= '9') ||
              (checksum[i] >= 'a' && checksum[i] <= 'f') ||
              (checksum[i] >= 'A' && checksum[i] <= 'F'))) return 0;
    return 1;
}
int repository_entry_valid(const RepoPayload *entry) {
    if (!entry || !entry->filename[0] || entry->filename[0] == '.' ||
        strchr(entry->filename, '/') || strstr(entry->filename, "..") ||
        is_pldmgr_name(entry->id) || is_pldmgr_name(entry->name) || is_pldmgr_name(entry->filename) ||
        !repository_checksum_valid(entry->checksum)) return 0;
    const char *ext = strrchr(entry->filename, '.');
    return ext && (!strcasecmp(ext, ".elf") || !strcasecmp(ext, ".bin"));
}

/* ── JSON parsing ──────────────────────────────────────────── */

static const char *skip_top_level_wrapper(const char *json) {
    const char *p = json;
    int in_string = 0, escaped = 0;

    while (*p && isspace((unsigned char)*p)) p++;
    if (*p != '{') return json;

    const char *q = p + 1;
    int depth = 1, is_wrapper = 0;

    while (*q && depth > 0) {
        if (in_string) {
            if (escaped) escaped = 0;
            else if (*q == '\\') escaped = 1;
            else if (*q == '"') in_string = 0;
        } else {
            if (*q == '"') {
                in_string = 1;
                if (depth == 1 && strncmp(q, "\"payloads\"", 10) == 0) {
                    is_wrapper = 1;
                    break;
                }
            } else if (*q == '{') depth++;
            else if (*q == '}' && --depth == 0) break;
        }
        q++;
    }

    if (is_wrapper) return p + 1;
    return json;
}

static const char *find_next_object_start(const char *p) {
    int in_string = 0, escaped = 0;

    while (*p) {
        if (in_string) {
            if (escaped) escaped = 0;
            else if (*p == '\\') escaped = 1;
            else if (*p == '"') in_string = 0;
        } else {
            if (*p == '"') in_string = 1;
            else if (*p == '{') return p;
        }
        p++;
    }
    return NULL;
}

int parse_repository_payloads(const char *json, RepoPayload **out_items, size_t *out_count) {
    const char *p;
    RepoPayload *items = NULL;
    size_t count = 0, capacity = 0;

    if (!out_items || !out_count) return -1;
    *out_items = NULL;
    *out_count = 0;

    if (!json || strnlen(json, 16 * 1024 * 1024 + 1) > 16 * 1024 * 1024) return -1;
    p = skip_top_level_wrapper(json);
    if (!p) return 0;

    while ((p = find_next_object_start(p)) != NULL) {
        const char *end = p + 1;
        int depth = 1, in_string = 0, escaped = 0;

        while (*end && depth > 0) {
            if (in_string) {
                if (escaped) escaped = 0;
                else if (*end == '\\') escaped = 1;
                else if (*end == '"') in_string = 0;
            } else {
                if (*end == '"') in_string = 1;
                else if (*end == '{') depth++;
                else if (*end == '}') depth--;
            }
            if (depth > 0) end++;
        }

        if (!*end && depth > 0) { free(items); return -1; }

        RepoPayload entry;
        RepoPayload *item = &entry;

        memset(item, 0, sizeof(*item));
        if (json_extract_string(p, end + 1, "name", item->name, sizeof(item->name)) != 0 ||
            json_extract_string(p, end + 1, "filename", item->filename, sizeof(item->filename)) != 0 ||
            json_extract_string(p, end + 1, "url", item->url, sizeof(item->url)) != 0) {
            p = end + 1;
            continue;
        }

        /* Extract id or fallback to slug(name) */
        json_extract_string(p, end + 1, "id", item->id, sizeof(item->id));
        if (item->id[0] == '\0') {
            slot_slug(item->name, item->id, sizeof(item->id));
        }

        json_extract_string(p, end + 1, "source", item->source, sizeof(item->source));
        json_extract_string(p, end + 1, "source_direct", item->source_direct, sizeof(item->source_direct));
        json_extract_string_prefix(p, end + 1, "description", item->description, sizeof(item->description));
        json_extract_string(p, end + 1, "last_update", item->last_update, sizeof(item->last_update));
        json_extract_string(p, end + 1, "version", item->version, sizeof(item->version));
        int checksum_result = json_extract_string(p, end + 1, "checksum", item->checksum, sizeof(item->checksum));
        /* A supplied oversized/non-string hash must not turn into an absent,
         * optional checksum after extraction fails. Hex validation is separate. */
        if ((checksum_result != JSON_STRING_OK && checksum_result != JSON_STRING_MISSING) ||
            !repository_checksum_valid(item->checksum)) {
            p = end + 1;
            continue;
        }
        json_extract_string(p, end + 1, "author", item->author, sizeof(item->author));
        json_extract_string(p, end + 1, "details_url", item->details_url, sizeof(item->details_url));

        int notes_result = json_extract_string_prefix(p, end + 1, "release_notes",
                                                     item->release_notes, sizeof(item->release_notes));
        if (notes_result == JSON_STRING_TRUNCATED) {
            size_t length = strlen(item->release_notes);
            if (length > sizeof(item->release_notes) - 4) length = sizeof(item->release_notes) - 4;
            while (length && ((unsigned char)item->release_notes[length] & 0xc0) == 0x80) length--;
            strcpy(item->release_notes + length, "...");
        }

        json_extract_string(p, end + 1, "icon_key", item->icon_key, sizeof(item->icon_key));

        if (json_extract_string(p, end + 1, "category", item->category, sizeof(item->category)) != 0 ||
            strlen(item->category) == 0) {
            strncpy(item->category, "Uncategorized", sizeof(item->category) - 1);
        }

        RepoPayload *tmp = (RepoPayload *)collection_reserve(items, &capacity, count + 1, sizeof(RepoPayload));
        if (!tmp) {
            free(items);
            return -1;
        }
        items = tmp;
        items[count] = *item;
        count++;
        p = end + 1;
    }

    *out_items = items;
    *out_count = count;
    return 0;
}

int repository_ensure_fresh(int force_refresh) {
    /* [CN-FORK] v0.5.1.65 内置官方源总开关：关掉后本函数不再向 REPOSITORY_SOURCE_URL
     * 发任何请求（开机预取 / 手动刷新 / 安装前补缓存三条路径都走这里）。磁盘上的旧缓存
     * 保持不动，重新打开可立即复用。v0.5.2.67 缺键时默认按「关」处理（与 config_read
     * 默认值一致）。
     *
     * 上游 v0.6.0 把缓存逻辑整体搬进了 sources.c（按 URL 分源缓存），所以这里只留
     * 开关判断 + 转调。用户自建来源不受本开关影响 —— 那条路走 sources_multi_* 。 */
    if (!config_read_bool("DEFAULT_REPO_ENABLED", 0))
        return 0;
    return sources_ensure_fresh(REPOSITORY_SOURCE_URL, force_refresh);
}

size_t repository_list_json(char *json_buf, size_t buf_size, int force_refresh) {
    return sources_multi_repository_list_json(json_buf, buf_size, force_refresh);
}


/* Self-updates deliberately retain the pre-slot layout: old launchers scan it. */
static pthread_mutex_t self_update_mutex = PTHREAD_MUTEX_INITIALIZER;

/* [CN-FORK] 磁贴变体后缀（-me 媒体栏 / -ga 游戏栏）是文件名约定，不属于版本号。
 * 仓库条目可能把它写进 version 字段（或干脆发 -ga 包）；自更新只认数字版本，
 * 校验与比对一律忽略尾部的 -me / -ga（或 _me / _ga）——这样「不管 me 还是 ga，
 * 只要版本号合适就更新」。 */
static int version_tail_is_variant(const char *p) {
    if (!p || (p[0] != '-' && p[0] != '_')) return 0;
    if (p[1] && p[2] && !p[3] &&
        (!strncasecmp(p + 1, "me", 2) || !strncasecmp(p + 1, "ga", 2))) return 1;
    return 0;
}
static void version_strip_variant(char *v) {
    if (!v) return;
    size_t n = strlen(v);
    if (n >= 3 && (v[n - 3] == '-' || v[n - 3] == '_') &&
        (!strncasecmp(v + n - 2, "me", 2) || !strncasecmp(v + n - 2, "ga", 2))) {
        v[n - 3] = '\0';
    }
}

static int self_version_valid(const char *version) {
    const char *p = version;
    if (!p) return 0;
    if (*p == 'v' || *p == 'V') p++;
    /* [CN-FORK] 上游只认 major.minor.patch 三段（原注释：「老引导器都能解析」）。但本 fork 的
     * MENU_VERSION 带构建序号是四段（如 0.6.1.181），构建时烧进 ELF 的 PLDMGR_VER 也是四段。
     * 若这里仍限三段就形成死结：清单写四段 → 被本函数判非法（目录过滤 + 安装拒绝 + 开机扫不到）；
     * 清单改写三段 → 又和 ELF 里烧的四段版本号不相等（repository_install_self_update 要求
     * slot_version_compare(embedded, entry->version)==0），照样被拒。结果 fork 永远走不了
     * payloads_v2.json 的在线自更新，只能靠上传。放宽到 3~4 段纯数字（每段 1~9 位）：既兼容
     * 上游三段发布，也让 fork 的四段构建号生效；第五段及以后仍判非法。 */
    int parts = 0;
    while (parts < 4) {
        int digits = 0;
        while (*p >= '0' && *p <= '9') { p++; if (++digits > 9) return 0; }
        if (!digits) return 0;
        parts++;
        if (*p != '.') break;
        p++;
    }
    return parts >= 3 && (*p == '\0' || version_tail_is_variant(p));
}

int repository_self_update_entry_valid(const RepoPayload *entry, const char *source_url) {
    if (!entry) return 0;
    /* [CN-FORK] 分两条身份：
     *  · fork 自身名（非 pldmgr 族，如 NayePlmTools）：源可信性已由调用方
     *    （sources_repo_install 的 src_configured）把关，此处只校条目形态；
     *  · 上游 pldmgr 族：依旧要求官方源 + id=="pldmgr"，保持既有策略与测试不变。
     * 两条都要求 version 为 3~4 段数字（尾部 -me/-ga 变体后缀会被忽略），带真校验和。 */
    int fork_only = is_self_update_name(entry->id) && !is_pldmgr_name(entry->id) &&
        !is_pldmgr_name(entry->filename) && !is_pldmgr_name(entry->name);
    if (fork_only) {
        if (!is_self_update_name(entry->filename)) return 0;
    } else if (!is_pldmgr_official_source(source_url) || strcmp(entry->id, "pldmgr") ||
               !is_pldmgr_name(entry->filename)) {
        return 0;
    }
    if (strchr(entry->filename, '/') || strchr(entry->filename, '\\') ||
        strstr(entry->filename, "..") || !self_version_valid(entry->version) ||
        !entry->checksum[0] || !repository_checksum_valid(entry->checksum)) return 0;
    const char *ext = strrchr(entry->filename, '.');
    return ext && (!strcasecmp(ext, ".elf") || !strcasecmp(ext, ".bin"));
}

int repository_self_update_version_fd(int fd, char *version, size_t size) {
    struct stat st;
    unsigned char magic[4];
    if (!version || !size) return -1;
    version[0] = 0;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size <= 0 ||
        (uint64_t)st.st_size > PLDMGR_PAYLOAD_DOWNLOAD_LIMIT ||
        pread(fd, magic, 4, 0) != 4 || memcmp(magic, "\177ELF", 4)) return -1;
    /* Overlap covers a marker and its complete, bounded version across chunks. */
    char buffer[8192 + 74];
    size_t carry = 0;
    off_t offset = 0;
    while (offset < st.st_size) {
        ssize_t n = pread(fd, buffer + carry, 8192, offset);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return -1;
        size_t used = carry + (size_t)n;
        for (size_t i = 0; i + 11 < used; i++) {
            if (memcmp(buffer + i, "PLDMGR_VER:", 11)) continue;
            const char *v = buffer + i + 11;
            size_t available = used - i - 11;
            const char *end = memchr(v, 0, available < 64 ? available : 64);
            if (!end || (size_t)(end - v) >= size || *v < '0' || *v > '9' || !self_version_valid(v)) continue;
            strcpy(version, v);
            return 0;
        }
        offset += n;
        carry = used < 74 ? used : 74;
        memmove(buffer, buffer + used - carry, carry);
    }
    return -1;
}

static int check_self_update(char *out_path, size_t size, char *version, size_t version_size) {
    if (!out_path || !size || !version || version_size < 64) return 0;
    out_path[0] = 0;
    snprintf(version, version_size, "%s", MENU_VERSION);
    DIR *root = opendir(PAYLOADS_STORAGE_DIR);
    if (!root) return 0;
    struct dirent *dir;
    while ((dir = readdir(root))) {
        if (strcasecmp(dir->d_name, "pldmgr") && strcasecmp(dir->d_name, "payload-manager")) continue;
        char folder[768];
        snprintf(folder, sizeof(folder), "%s/%s", PAYLOADS_STORAGE_DIR, dir->d_name);
        DIR *d = opendir(folder);
        if (!d) continue;
        struct dirent *file;
        while ((file = readdir(d))) {
            char path[1024], candidate[64];
            snprintf(path, sizeof(path), "%s/%s", folder, file->d_name);
            int fd = slot_open_self_update(path);
            if (fd < 0) continue;
            int valid = repository_self_update_version_fd(fd, candidate, sizeof(candidate));
            close(fd);
            if (!valid && slot_version_compare(candidate, version) > 0 && strlen(path) < size) {
                strcpy(version, candidate);
                strcpy(out_path, path);
            }
        }
        closedir(d);
    }
    closedir(root);
    return out_path[0] != 0;
}

int repository_check_self_update(char *out_path, size_t size, char *version, size_t version_size) {
    pthread_mutex_lock(&self_update_mutex);
    int found = check_self_update(out_path, size, version, version_size);
    pthread_mutex_unlock(&self_update_mutex);
    return found;
}

int repository_install_self_update(const RepoPayload *entry, const char *source_url, const char *part_path) {
    if (!repository_self_update_entry_valid(entry, source_url)) return -1;
    int fd = open(part_path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) return -1;
    char version[64], hash[65], want_ver[64];
    /* [CN-FORK] 仓库 version 可能带 -me/-ga 变体后缀；内嵌 PLDMGR_VER 永远是纯数字。
     * 比前先把变体后缀剖掉，做到「不管 me/ga，只要数字版本相等且更新就装」。 */
    strncpy(want_ver, entry->version, sizeof(want_ver) - 1);
    want_ver[sizeof(want_ver) - 1] = '\0';
    version_strip_variant(want_ver);
    int invalid = repository_self_update_version_fd(fd, version, sizeof(version)) ||
        slot_version_compare(version, want_ver) ||
        slot_version_compare(version, MENU_VERSION) <= 0 ||
        compute_sha256_file(part_path, hash) || strcasecmp(hash, entry->checksum) || fsync(fd);
    close(fd);
    if (invalid) return -1;
    pthread_mutex_lock(&self_update_mutex);
    char previous[1024], pending[64];
    check_self_update(previous, sizeof(previous), pending, sizeof(pending));
    int result = -1;
    if (slot_version_compare(version, pending) <= 0) goto done;
    const char *folder = PAYLOADS_STORAGE_DIR "/pldmgr";
    if (mkdir(folder, 0755) && errno != EEXIST) goto done;
    int parent_fd = open(PAYLOADS_STORAGE_DIR, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (parent_fd < 0) goto done;
    int parent_sync = fsync(parent_fd);
    close(parent_fd);
    if (parent_sync) goto done;
    int dir_fd = open(folder, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (dir_fd < 0) goto done;
    const char *v = version + (version[0] == 'v' || version[0] == 'V');
    char filename[128];
    snprintf(filename, sizeof(filename), "pldmgr_v%s.elf", v);
    if (renameat(AT_FDCWD, part_path, dir_fd, filename)) { close(dir_fd); goto done; }
    result = 0; /* Publication succeeded. Retain previous files if syncing fails. */
    if (fsync(dir_fd)) {
        pldmgr_log("[PLDMGR] Self-update published; directory sync failed.\n");
    } else {
        int scan_fd = dup(dir_fd);
        DIR *d = scan_fd >= 0 ? fdopendir(scan_fd) : NULL;
        if (!d && scan_fd >= 0) close(scan_fd);
        struct dirent *file;
        while (d && (file = readdir(d))) {
            const char *ext = strrchr(file->d_name, '.');
            if (strcmp(file->d_name, filename) && is_pldmgr_name(file->d_name) && ext &&
                (!strcasecmp(ext, ".elf") || !strcasecmp(ext, ".bin"))) {
                if (unlinkat(dir_fd, file->d_name, 0))
                    pldmgr_log("[PLDMGR] Could not remove previous self-update: %s\n", file->d_name);
            }
        }
        if (d) closedir(d);
        if (fsync(dir_fd)) pldmgr_log("[PLDMGR] Self-update cleanup sync failed.\n");
    }
    close(dir_fd);
done:
    pthread_mutex_unlock(&self_update_mutex);
    return result;
}

/* [CN-FORK] v0.6.1.191 手动上传「本程序」自身时，在普通导入之外「额外复制」一份进
 * 与在线自更新同一个 slot（/payloads/pldmgr/pldmgr_v<ver>.elf），让顶部「本程序」卡
 * 复用现成的「重启生效」干净交接（/apply_self_update 只认这个目录）。src_path 是刚落盘
 * 的普通 payload，本函数只读不消费（用 copy 而非 rename），普通卡与引导器登记照常保留。
 * 仅当 ELF 内嵌 PLDMGR_VER 严格高于 MENU_VERSION、且高于已 pending 的那份时才发布；否则
 * no-op（不比当前运行新就不动）。与 repository_install_self_update 同一发布块，区别只在：
 * 手动上传没有仓库条目、无 checksum 可校，故跳过校验和比对（文件是用户自己给的，
 * 信任边界等同▶启动）。 */
int repository_stage_uploaded_self_update(const char *src_path, char *out_version, size_t out_version_size) {
    if (!src_path || !*src_path) return -1;
    if (out_version && out_version_size > 0) out_version[0] = '\0';
    int fd = open(src_path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) return -1;
    char version[64];
    /* 内嵌 PLDMGR_VER 永远是纯数字（无 -me/-ga 后缀），只接受严格高于当前运行的上传。 */
    int bad = repository_self_update_version_fd(fd, version, sizeof(version)) ||
        slot_version_compare(version, MENU_VERSION) <= 0;
    close(fd);
    if (bad) return -1;
    pthread_mutex_lock(&self_update_mutex);
    char previous[1024], pending[64];
    check_self_update(previous, sizeof(previous), pending, sizeof(pending));
    int result = -1;
    if (slot_version_compare(version, pending) <= 0) goto staged_done;
    const char *folder = PAYLOADS_STORAGE_DIR "/pldmgr";
    if (mkdir(folder, 0755) && errno != EEXIST) goto staged_done;
    int parent_fd = open(PAYLOADS_STORAGE_DIR, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (parent_fd < 0) goto staged_done;
    int parent_sync = fsync(parent_fd);
    close(parent_fd);
    if (parent_sync) goto staged_done;
    int dir_fd = open(folder, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (dir_fd < 0) goto staged_done;
    const char *v = version + (version[0] == 'v' || version[0] == 'V');
    char filename[128];
    snprintf(filename, sizeof(filename), "pldmgr_v%s.elf", v);
    /* 复制而非 rename：src_path 是刚落盘的普通 payload（要留给普通卡与引导器），这里只是
     * 「额外」往自更新 slot 放一份。先写同目录 .part 再 rename，保证发布原子性。
     * 缓冲走堆 —— 真机连接线程栈只有 64KB（见 game_saves.c 的同类事故记录）。 */
    int src_fd = open(src_path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    if (src_fd < 0) { close(dir_fd); goto staged_done; }
    const char *part = "pldmgr_upl.part";
    int dst_fd = openat(dir_fd, part, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0644);
    if (dst_fd < 0) { close(src_fd); close(dir_fd); goto staged_done; }
    char *buf = malloc(16384);
    int copied = buf ? 0 : -1;
    ssize_t n = 0;
    while (copied == 0 && (n = read(src_fd, buf, 16384)) > 0) {
        ssize_t off = 0;
        while (off < n) {
            ssize_t w = write(dst_fd, buf + off, n - off);
            if (w <= 0) { copied = -1; break; }
            off += w;
        }
    }
    if (n < 0) copied = -1;
    free(buf);
    close(src_fd);
    if (copied != 0 || fsync(dst_fd)) {
        close(dst_fd);
        unlinkat(dir_fd, part, 0);
        close(dir_fd);
        goto staged_done;
    }
    close(dst_fd);
    if (renameat(dir_fd, part, dir_fd, filename)) {
        unlinkat(dir_fd, part, 0);
        close(dir_fd);
        goto staged_done;
    }
    result = 0;
    if (out_version && out_version_size > 0) {
        strncpy(out_version, version, out_version_size - 1);
        out_version[out_version_size - 1] = '\0';
    }
    if (fsync(dir_fd)) {
        pldmgr_log("[PLDMGR] Uploaded self-update published; directory sync failed.\n");
    } else {
        int scan_fd = dup(dir_fd);
        DIR *d = scan_fd >= 0 ? fdopendir(scan_fd) : NULL;
        if (!d && scan_fd >= 0) close(scan_fd);
        struct dirent *file;
        while (d && (file = readdir(d))) {
            const char *ext = strrchr(file->d_name, '.');
            if (strcmp(file->d_name, filename) && is_pldmgr_name(file->d_name) && ext &&
                (!strcasecmp(ext, ".elf") || !strcasecmp(ext, ".bin"))) {
                if (unlinkat(dir_fd, file->d_name, 0))
                    pldmgr_log("[PLDMGR] Could not remove previous self-update: %s\n", file->d_name);
            }
        }
        if (d) closedir(d);
        if (fsync(dir_fd)) pldmgr_log("[PLDMGR] Uploaded self-update cleanup sync failed.\n");
    }
    close(dir_fd);
staged_done:
    pthread_mutex_unlock(&self_update_mutex);
    return result;
}
