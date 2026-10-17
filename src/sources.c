#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <curl/curl.h>
#include <ctype.h>

#include "sources.h"
#include "repository.h"
#include "slot.h"
#include "sha256.h"
#include "json_helpers.h"
#include "config.h"
#include "pldmgr.h"
#include "payload_mgr.h"
#include "autoload.h"

/* ── Source entry type ─────────────────────────────────────── */

typedef struct {
    char id[64];
    char name[256];
    char url[1024];
    int  removable;
    /* [CN-FORK] v0.5.1.65 默认云端仓库开关：只有下标 0 的内置源可能为 0（已关闭），
     * 仍然占住 slot 0 以免用户源的数组下标（决定缓存文件名）发生错乱。
     * **不落盘** —— 每次 load_sources 从 DEFAULT_REPO_ENABLED 重算。 */
    int  enabled;
} SourceEntry;

/* ── Load / Save sources from SOURCES_CONFIG_PATH ──────────── */

static pthread_mutex_t sources_mutex = PTHREAD_MUTEX_INITIALIZER;
typedef struct { SourceEntry *entries; int count; int found; } SourceParse;
static void default_source(SourceEntry *out) {
    memset(out, 0, sizeof(SourceEntry) * MAX_SOURCES);
    snprintf(out[0].id, sizeof(out[0].id), "default");
    snprintf(out[0].name, sizeof(out[0].name), "Official Repository");
    snprintf(out[0].url, sizeof(out[0].url), "%s", REPOSITORY_SOURCE_URL);
    /* [CN-FORK] v0.5.1.65 默认云端仓库可以整体关掉（关掉后不再拉取、不列目录，
     * 管理来源页把它标成已关闭）。v0.5.2.67 起缺键时按「关」处理（与 config_read
     * 的默认值一致）。 */
    out[0].enabled = config_read_bool("DEFAULT_REPO_ENABLED", 0);
}
static int source_item(const char *start, const char *end, void *context) {
    SourceParse *parse = context;
    /* [CN-FORK] 用户自建源一律启用，不受默认仓库开关影响 */
    SourceEntry entry = {.removable = 1, .enabled = 1};
    if (json_extract_string(start, end, "id", entry.id, sizeof(entry.id)) ||
        json_extract_string(start, end, "name", entry.name, sizeof(entry.name)) ||
        json_extract_string(start, end, "url", entry.url, sizeof(entry.url)) ||
        !entry.id[0] || !entry.name[0] || !entry.url[0]) return -1;
    if (!strcmp(entry.id, "default")) return 0;
    if (is_legacy_official_repo_url(entry.url)) {
        snprintf(entry.url, sizeof(entry.url), "%s", REPOSITORY_SOURCE_URL);
    }
    if (parse->count >= MAX_SOURCES) return -1;
    for (int i = 0; i < parse->count; i++)
        if (!strcmp(parse->entries[i].id, entry.id)) return -1;
    parse->entries[parse->count++] = entry;
    return 0;
}
static int source_member(const char *key, const char *start, const char *end, void *context) {
    SourceParse *parse = context;
    if (strcmp(key, "sources")) return 0;
    if (parse->found++) return -1;
    return json_visit_array(start, end, source_item, context);
}
static int parse_sources(const char *json, size_t size, SourceEntry *out, int *out_count) {
    default_source(out);
    SourceParse parse = {.entries=out, .count=1};
    if (json_visit_object(json, json + size, source_member, &parse) || !parse.found) return -1;
    *out_count = parse.count; return 0;
}
static int load_sources(SourceEntry *out, int *out_count) {
    default_source(out); *out_count = 1;
    char *json = NULL; size_t size = 0;
    if (read_file_text(SOURCES_CONFIG_PATH, &json, &size)) return errno == ENOENT ? 0 : -1;
    int result = parse_sources(json, size, out, out_count);
    free(json); return result;
}

static int save_sources(SourceEntry *sources, int count) {
    size_t size = MAX_SOURCES * (6 * sizeof(SourceEntry) + 128) + 64;
    char *buf = malloc(size);
    if (!buf) return -1;
    JsonListBuilder jb = { buf, size, 0, 1, 0 };

    ensure_dir_recursive(BASE_DATA_DIR);

    json_append(&jb, "{\"sources\":[\n");
    for (int i = 0; i < count; i++) {
        char id_e[6 * 128], name_e[6 * 256], url_e[6 * 1024];
        json_escape_checked(&jb, sources[i].id, id_e, sizeof(id_e));
        json_escape_checked(&jb, sources[i].name, name_e, sizeof(name_e));
        json_escape_checked(&jb, sources[i].url, url_e, sizeof(url_e));
        if (json_append(&jb, "  {\"id\":\"%s\",\"name\":\"%s\",\"url\":\"%s\",\"removable\":%s}%s\n",
            id_e, name_e, url_e,
            sources[i].removable ? "true" : "false",
            (i < count - 1) ? "," : "") != 0) {
            break;
        }
    }
    json_append(&jb, "]}\n");

    int result = jb.error ? -1 : write_file_text(SOURCES_CONFIG_PATH, buf, jb.pos);
    free(buf);
    return result;
}

/* ── Public API ────────────────────────────────────────────── */

int sources_list_json(char *buf, size_t size) {
    if (!buf || !size) return -1;
    buf[0] = '\0';
    SourceEntry *sources = calloc(MAX_SOURCES, sizeof(SourceEntry));
    if (!sources) return -1;
    int count = 0;
    if (load_sources(sources, &count)) { free(sources); return -1; }

    JsonListBuilder jb = { buf, size, 0, 1, 0 };
    buf[0] = '\0';

    json_append(&jb, "{\"sources\":[\n");
    for (int i = 0; i < count; i++) {
        char id_e[6 * 128], name_e[6 * 256], url_e[6 * 1024];
        json_escape_checked(&jb, sources[i].id, id_e, sizeof(id_e));
        json_escape_checked(&jb, sources[i].name, name_e, sizeof(name_e));
        json_escape_checked(&jb, sources[i].url, url_e, sizeof(url_e));
        /* [CN-FORK] v0.5.1.65 多一个 enabled 字段，让管理来源页把已关闭的默认源灰显而不是隐行，
         * 下标保持与后端一致，上移/下移/删除的索引语义不变 */
        if (json_append(&jb, "  {\"id\":\"%s\",\"name\":\"%s\",\"url\":\"%s\",\"removable\":%s,\"enabled\":%s}%s\n",
            id_e, name_e, url_e,
            sources[i].removable ? "true" : "false",
            sources[i].enabled ? "true" : "false",
            (i < count - 1) ? "," : "") != 0) {
            break;
        }
    }
    json_append(&jb, "]}\n");
    free(sources);
    return jb.error ? -1 : 0;
}

int sources_save(const char *json, size_t len) {
    if (!json || !len) return CONFIG_INVALID;
    SourceEntry *sources = calloc(MAX_SOURCES, sizeof(SourceEntry));
    if (!sources) return -1;
    int count = 0;
    if (parse_sources(json, len, sources, &count)) { free(sources); return CONFIG_INVALID; }
    pthread_mutex_lock(&sources_mutex);
    int result = save_sources(sources, count);
    pthread_mutex_unlock(&sources_mutex);
    free(sources); return result;
}

static int merge_source(const char *url, const char *source_name, char *msg_buf, size_t msg_size) {
    SourceEntry *sources = calloc(MAX_SOURCES, sizeof(SourceEntry));
    if (!sources) {
        snprintf(msg_buf, msg_size, "Memory allocation failed");
        return -1;
    }
    int src_count = 0;
    if (load_sources(sources, &src_count)) { free(sources); snprintf(msg_buf, msg_size, "Failed to read sources"); return -1; }

    if (src_count >= MAX_SOURCES) {
        free(sources);
        snprintf(msg_buf, msg_size, "Maximum number of sources (%d) reached", MAX_SOURCES);
        return -1;
    }

    for (int i = 0; i < src_count; i++) {
        if (strcmp(sources[i].url, url) == 0) {
            free(sources);
            snprintf(msg_buf, msg_size, "Source already added");
            return -1;
        }
    }

    char new_id[64];
    char hash[16]; slot_source_hash(url, hash, sizeof(hash));
    snprintf(new_id, sizeof(new_id), "source_%s", hash);
    for (int suffix = 1;; suffix++) {
        int found = 0;
        for (int i = 0; i < src_count; i++) if (!strcmp(sources[i].id, new_id)) found = 1;
        if (!found) break;
        snprintf(new_id, sizeof(new_id), "source_%s_%d", hash, suffix);
    }

    strncpy(sources[src_count].id,   new_id,      sizeof(sources[src_count].id) - 1);
    strncpy(sources[src_count].name, source_name, sizeof(sources[src_count].name) - 1);
    strncpy(sources[src_count].url,  url,         sizeof(sources[src_count].url) - 1);
    sources[src_count].removable = 1;
    sources[src_count].enabled   = 1; /* [CN-FORK] 用户自建源一律启用 */
    src_count++;

    if (save_sources(sources, src_count) != 0) {
        free(sources);
        snprintf(msg_buf, msg_size, "Failed to save sources");
        return -1;
    }

    free(sources);
    snprintf(msg_buf, msg_size, "%s", source_name);
    return 0;
}

int sources_add(const char *url, char *msg_buf, size_t msg_size) {
    char tmp_path[512];
    char *json = NULL;
    size_t json_size = 0;
    RepoPayload *items = NULL;
    size_t count = 0;
    char source_name[256] = "";

    if (!url || !url[0]) {
        snprintf(msg_buf, msg_size, "URL is empty");
        return -1;
    }

    ensure_dir_recursive(BASE_DATA_DIR);
    snprintf(tmp_path, sizeof(tmp_path), "%s/.source-XXXXXX", BASE_DATA_DIR);
    int fd = mkstemp(tmp_path);
    if (fd < 0) { snprintf(msg_buf, msg_size, "Failed to create validation file"); return -1; }
    if (close(fd)) { remove(tmp_path); snprintf(msg_buf, msg_size, "Failed to close validation file"); return -1; }
    if (strlen(url) >= sizeof(((SourceEntry *)0)->url)) {
        remove(tmp_path); snprintf(msg_buf, msg_size, "URL is too long"); return -1;
    }

    if (download_to_file_limited(url, tmp_path, PLDMGR_MANIFEST_DOWNLOAD_LIMIT) != 0) {
        snprintf(msg_buf, msg_size, "URL unreachable or download failed");
        remove(tmp_path);
        return -1;
    }

    if (read_file_text(tmp_path, &json, &json_size) != 0 || !json || json_size == 0) {
        snprintf(msg_buf, msg_size, "Failed to read downloaded JSON");
        remove(tmp_path);
        if (json) free(json);
        return -1;
    }
    remove(tmp_path);

    /* Decode the actual top-level name, including escaped Unicode/quotes. */
    json_extract_string_prefix(json, json + json_size, "name", source_name, sizeof(source_name));
    if (!source_name[0]) {
        const char *host = strstr(url, "://");
        host = host ? host + 3 : url;
        size_t pos = 0;
        while (*host && *host != '/' && *host != ':' && pos < sizeof(source_name) - 1)
            source_name[pos++] = *host++;
        source_name[pos] = '\0';
    }

    if (parse_repository_payloads(json, &items, &count) != 0 || count == 0) {
        free(json);
        if (items) free(items);
        snprintf(msg_buf, msg_size, "No valid payloads found in source JSON");
        return -1;
    }
    free(items);
    free(json);

    pthread_mutex_lock(&sources_mutex);
    int result = merge_source(url, source_name, msg_buf, msg_size);
    pthread_mutex_unlock(&sources_mutex);
    return result;
}

static int remove_source(int index, char *msg_buf, size_t msg_size) {
    if (index <= 0) {
        snprintf(msg_buf, msg_size, "Cannot remove the default source");
        return -1;
    }

    SourceEntry *sources = calloc(MAX_SOURCES, sizeof(SourceEntry));
    if (!sources) {
        snprintf(msg_buf, msg_size, "Memory allocation failed");
        return -1;
    }
    int count = 0;
    if (load_sources(sources, &count)) { free(sources); snprintf(msg_buf, msg_size, "Failed to read sources"); return -1; }

    if (index >= count) {
        free(sources);
        snprintf(msg_buf, msg_size, "Invalid source index");
        return -1;
    }

    for (int i = index; i < count - 1; i++) {
        sources[i] = sources[i + 1];
    }
    count--;

    if (save_sources(sources, count) != 0) {
        free(sources);
        snprintf(msg_buf, msg_size, "Failed to save sources");
        return -1;
    }

    free(sources);
    snprintf(msg_buf, msg_size, "OK");
    return 0;
}

int sources_remove(int index, char *msg_buf, size_t msg_size) {
    pthread_mutex_lock(&sources_mutex);
    int result = remove_source(index, msg_buf, msg_size);
    pthread_mutex_unlock(&sources_mutex);
    return result;
}

/* [CN-FORK] v0.5.2.67 首次运行预置来源清单：内置官方源默认关掉，改成预置国内可达的
 * repo.630810.xyz 并启用。仅当 sources.json 尚不存在时写一次，之后完全交给用户在
 * 「管理来源」里增删。由 main.c 在开机序列早期调用。 */
void sources_seed_defaults(void) {
    if (access(SOURCES_CONFIG_PATH, F_OK) == 0)
        return; /* 已经初始化过（用户可能已在管理页增删/排序），不再覆盖 */

    SourceEntry *sources = calloc(MAX_SOURCES, sizeof(SourceEntry));
    if (!sources)
        return;

    /* slot 0：内置官方源（由 DEFAULT_REPO_ENABLED 控制，本版默认关） */
    snprintf(sources[0].id, sizeof(sources[0].id), "default");
    snprintf(sources[0].name, sizeof(sources[0].name), "Official Repository");
    snprintf(sources[0].url, sizeof(sources[0].url), "%s", REPOSITORY_SOURCE_URL);
    sources[0].removable = 0;
    sources[0].enabled   = 1; /* 落盘不记开关，启用状态每次由 load_sources 从配置重算 */

    /* slot 1：预置的国内镜像仓库，添加并启用，用户可删除 */
    /* [CN-FORK] v0.6.1.x 预置源改用 payloads_v2.json（对象包装 + 顶层 icons_url）：
     * 裸数组 payloads.json 无法声明 icons_url，后端 resolve_icons_url 直接 return，
     * 出厂自带这条源永远出不了图标。仓库侧已发布 v2，这里同步指向它。 */
    snprintf(sources[1].id, sizeof(sources[1].id), "source_cn");
    snprintf(sources[1].name, sizeof(sources[1].name), "repo.630810.xyz");
    snprintf(sources[1].url, sizeof(sources[1].url), "https://repo.630810.xyz/payloads_v2.json");
    sources[1].removable = 1;
    sources[1].enabled   = 1;

    if (save_sources(sources, 2) == 0)
        pldmgr_log("[PLDMGR] Seeded default sources: repo.630810.xyz added & enabled\n");
    free(sources);
}

/* ── Hash-based cache paths ────────────────────────────────── */

static void get_cache_paths(const char *url, char *cache_path, size_t csize,
                            char *ts_path, size_t tsize) {
    char h8[16];
    slot_source_hash(url, h8, sizeof(h8));
    snprintf(cache_path, csize, "%s/repo_cache_%s.json", BASE_DATA_DIR, h8);
    snprintf(ts_path, tsize, "%s/repo_cache_%s.json.ts", BASE_DATA_DIR, h8);
}

static pthread_mutex_t repository_cache_mutex = PTHREAD_MUTEX_INITIALIZER;

static int ensure_catalog_fresh(const char *url, int force_refresh) {
    if (!url || !url[0]) return -1;

    char cache_path[512], ts_path[512];
    get_cache_paths(url, cache_path, sizeof(cache_path), ts_path, sizeof(ts_path));

    long last_update = 0;
    time_t now = time(NULL);

    ensure_dir_recursive(BASE_DATA_DIR);

    FILE *tf = fopen(ts_path, "r");
    if (tf) {
        fscanf(tf, "%ld", &last_update);
        fclose(tf);
    }

    if (!force_refresh && access(cache_path, F_OK) == 0) {
        long delta = (long)now - last_update;
        if (delta >= 0 && delta < REPOSITORY_REFRESH_INTERVAL_SEC) {
            return 0;
        }
    }

    char tmp_path[512];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp-XXXXXX", cache_path);
    int fd = mkstemp(tmp_path);
    if (fd < 0) return -1;
    if (close(fd)) { remove(tmp_path); return -1; }
    if (download_to_file_limited(url, tmp_path, PLDMGR_MANIFEST_DOWNLOAD_LIMIT) != 0) {
        remove(tmp_path); return -1;
    }

    char *json = NULL;
    size_t json_size = 0;
    if (read_file_text(tmp_path, &json, &json_size) != 0 || !json || json_size == 0) {
        if (json) free(json);
        remove(tmp_path);
        return -1;
    }

    RepoPayload *items = NULL;
    size_t count = 0;
    if (parse_repository_payloads(json, &items, &count) != 0 || count == 0) {
        free(items);
        free(json);
        remove(tmp_path);
        return -1;
    }
    free(items);
    free(json);

    if (rename(tmp_path, cache_path) != 0) {
        remove(tmp_path);
        return -1;
    }

    char timestamp[64];
    int timestamp_size = snprintf(timestamp, sizeof(timestamp), "%ld", (long)now);
    if (timestamp_size < 0 || (size_t)timestamp_size >= sizeof(timestamp) ||
        write_file_text(ts_path, timestamp, (size_t)timestamp_size)) return -1;
    if (!strcmp(url, REPOSITORY_SOURCE_URL) && config_write_last_update((long)now)) return -1;

    return 0;
}

#define ICON_CATALOG_LIMIT ((size_t)4 * 1024 * 1024 - 1024)
#define ICON_REFRESH_SECONDS (7L * 86400)
#define ICON_RETRY_SECONDS 3600L

typedef struct {
    char (*keys)[128];
    size_t count;
    const char *map_start, *map_end;
    int found;
} IconCatalog;

static int valid_icon(const char *value) {
    const char *prefixes[] = {"data:image/png;base64,", "data:image/jpeg;base64,", "data:image/svg+xml;base64,"};
    const char *data = NULL;
    for (size_t i = 0; i < sizeof(prefixes)/sizeof(prefixes[0]); i++)
        if (!strncmp(value, prefixes[i], strlen(prefixes[i]))) data = value + strlen(prefixes[i]);
    if (!data || !*data || strlen(value) > 12288 || strlen(data) % 4) return 0;
    size_t n = strlen(data), padding = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)data[i];
        if (c == '=') { if (i < n - 2 || ++padding > 2) return 0; }
        else if (padding || !((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '+' || c == '/')) return 0;
    }
    return 1;
}
static int icon_member(const char *key, const char *start, const char *end, void *context) {
    IconCatalog *catalog = context; char value[12289];
    if (!key[0] || strlen(key) >= 128 || catalog->count >= 4096 ||
        json_decode_string(start, end, value, sizeof(value)) || !valid_icon(value)) return -1;
    for (size_t i = 0; i < catalog->count; i++) if (!strcmp(catalog->keys[i], key)) return -1;
    strcpy(catalog->keys[catalog->count++], key);
    return 0;
}
static int icon_wrapper(const char *key, const char *start, const char *end, void *context) {
    IconCatalog *catalog = context;
    if (strcmp(key, "icons")) return 0;
    if (catalog->found++) return -1;
    catalog->map_start = start; catalog->map_end = end;
    return json_visit_object(start, end, icon_member, catalog);
}
static int parse_icons(const char *json, size_t size, IconCatalog *catalog) {
    memset(catalog, 0, sizeof(*catalog));
    if (size > ICON_CATALOG_LIMIT) return -1;
    catalog->keys = calloc(4096, sizeof(*catalog->keys));
    if (!catalog->keys) return -1;
    int result = json_visit_object(json, json + size, icon_wrapper, catalog) || !catalog->found;
    if (result) { free(catalog->keys); catalog->keys = NULL; }
    return result ? -1 : 0;
}
static void digest_text(const char *text, char hex[65]) {
    SHA256_CTX ctx; unsigned char hash[32];
    sha256_init(&ctx); sha256_update(&ctx, (const unsigned char *)text, strlen(text)); sha256_final(&ctx, hash);
    for (size_t i = 0; i < 32; i++) snprintf(hex + 2*i, 3, "%02x", hash[i]);
}
static void icon_paths(const char *url, char path[512], char meta[512]) {
    char hex[65]; digest_text(url, hex);
    snprintf(path, 512, "%s/repo_icons_%s.json", BASE_DATA_DIR, hex);
    snprintf(meta, 512, "%s/repo_icons_%s.meta", BASE_DATA_DIR, hex);
}
static int read_icons(const char *path, char **text, size_t *size, IconCatalog *catalog) {
    struct stat st;
    *text = NULL;
    if (stat(path, &st) || !S_ISREG(st.st_mode) || st.st_size < 0 || (size_t)st.st_size > ICON_CATALOG_LIMIT ||
        read_file_text(path, text, size)) return -1;
    if (parse_icons(*text, *size, catalog)) { free(*text); *text = NULL; return -1; }
    return 0;
}
static int resolve_icons_url(const char *base, const char *catalog, char out[1024]) {
    char relative[1024]; out[0] = 0;
    if (!catalog || json_extract_string(catalog, catalog + strlen(catalog), "icons_url", relative, sizeof(relative)) || !relative[0]) return -1;
    CURLU *url = curl_url(); char *resolved = NULL, *scheme = NULL, *user = NULL;
    if (!url) return -1;
    int ok = !curl_url_set(url, CURLUPART_URL, base, 0) && !curl_url_set(url, CURLUPART_URL, relative, 0) &&
        !curl_url_get(url, CURLUPART_SCHEME, &scheme, 0) && (!strcmp(scheme, "http") || !strcmp(scheme, "https")) &&
        (strncasecmp(base, "https://", 8) || !strcmp(scheme, "https")) &&
        curl_url_get(url, CURLUPART_USER, &user, 0) == CURLUE_NO_USER &&
        !curl_url_set(url, CURLUPART_FRAGMENT, NULL, 0) && !curl_url_get(url, CURLUPART_URL, &resolved, 0) && strlen(resolved) < 1024;
    if (ok) strcpy(out, resolved);
    curl_free(user); curl_free(scheme); curl_free(resolved); curl_url_cleanup(url);
    return ok ? 0 : -1;
}
static long meta_time(const char *json, const char *key) {
    char value[32], *end;
    if (json_extract_string(json, json + strlen(json), key, value, sizeof(value))) return 0;
    errno = 0; long number = strtol(value, &end, 10);
    return errno || end == value || *end || number < 0 ? 0 : number;
}
static int missing_compare(const void *a, const void *b) { return strcmp(a, b); }
static int missing_icons(IconCatalog *icons, RepoPayload *items, size_t count, char digest[65]) {
    char (*missing)[128] = calloc(count ? count : 1, 128);
    if (!missing) return -1;
    size_t n = 0;
    for (size_t i = 0; i < count; i++) {
        if (!items[i].icon_key[0]) continue;
        size_t k = 0;
        while (k < icons->count && strcmp(items[i].icon_key, icons->keys[k])) k++;
        if (k == icons->count) strcpy(missing[n++], items[i].icon_key);
    }
    qsort(missing, n, 128, missing_compare);
    SHA256_CTX ctx; unsigned char hash[32]; sha256_init(&ctx);
    for (size_t i = 0; i < n; i++)
        if (!i || strcmp(missing[i], missing[i-1])) sha256_update(&ctx, (unsigned char *)missing[i], strlen(missing[i]) + 1);
    sha256_final(&ctx, hash);
    for (size_t i = 0; i < 32; i++) snprintf(digest + 2*i, 3, "%02x", hash[i]);
    free(missing); return n ? 1 : 0;
}
/* [CN-FORK] v0.6.0.176 ensure_icons 写 icons 元数据 JSON 用的一组缓冲（合计 28.6KB），
 * 搬堆以压栈帧，见函数内注释。 */
typedef struct {
    char escaped[6 * 1024], cached_url[6 * 1024], output[16384];
} IconMetaJsonScratch;

/* [CN-FORK] v0.6.0.176 栈帧红线：本函数自带 ~28.6KB 栈缓冲（escaped/cached_url/output），
 * 唯一调用点 repository_list_locked 在 -Os 下会把它整个内联进去，两边的缓冲叠加后
 * 那一个栈帧就有 66584 B（真机 MHD 每条连接线程只有 64KB 栈，必 SIGSEGV）。
 * 加 noinline 让它保持独立帧（28.6KB，本身在 32KB 红线内），调用链总深 ~38KB。 */
__attribute__((noinline))
static void ensure_icons(const char *source, const char *catalog, RepoPayload *items, size_t count, char revision[65]) {
    char url[1024], path[512], meta[512]; revision[0] = 0;
    if (resolve_icons_url(source, catalog, url)) return;
    icon_paths(source, path, meta);
    char *old = NULL, *state = NULL; size_t size = 0, state_size = 0;
    IconCatalog icons = {0};
    int cached = !read_icons(path, &old, &size, &icons);
    read_file_text(meta, &state, &state_size);
    const char *metadata = state ? state : "{}";
    char old_url[1024] = "", attempt_url[1024] = "", old_missing[65] = "", missing[65] = "";
    json_extract_string(metadata, metadata + strlen(metadata), "url", old_url, sizeof(old_url));
    json_extract_string(metadata, metadata + strlen(metadata), "attempt_url", attempt_url, sizeof(attempt_url));
    json_extract_string(metadata, metadata + strlen(metadata), "missing", old_missing, sizeof(old_missing));
    long updated = meta_time(metadata, "updated"), attempted = meta_time(metadata, "attempted"), now = (long)time(NULL);
    int unknown = missing_icons(&icons, items, count, missing);
    int changed = strcmp(old_url, url) != 0;
    int due = !cached || changed || now < updated || now - updated >= ICON_REFRESH_SECONDS || unknown > 0;
    int retry = strcmp(attempt_url, url) || strcmp(old_missing, missing) || now < attempted || now - attempted >= ICON_RETRY_SECONDS;
    if (unknown >= 0 && due && retry) {
        char temporary[544]; snprintf(temporary, sizeof(temporary), "%s.tmp-XXXXXX", path);
        int fd = mkstemp(temporary);
        if (fd >= 0) {
            int ready = !close(fd);
            char *fresh = NULL; size_t fresh_size = 0; IconCatalog downloaded = {0};
            if (ready && !download_to_file_limited(url, temporary, ICON_CATALOG_LIMIT) &&
                !read_icons(temporary, &fresh, &fresh_size, &downloaded) && !rename(temporary, path)) {
                updated = now; strcpy(old_url, url);
            }
            free(downloaded.keys); free(fresh); remove(temporary);
        }
        /* [CN-FORK] v0.6.0.176 栈帧红线：这三个缓冲合计 28.6KB，原来是 ensure_icons
         * 独立帧的全部来源（33720 B，越 32KB 红线）。搬堆后本函数栈帧 ~1KB。 */
        IconMetaJsonScratch *msc = malloc(sizeof(*msc));
        if (msc) {
            JsonListBuilder builder = {msc->output, sizeof(msc->output), 0, 1, 0};
            json_escape_checked(&builder, url, msc->escaped, sizeof(msc->escaped));
            json_escape_checked(&builder, old_url, msc->cached_url, sizeof(msc->cached_url));
            json_append(&builder, "{\"url\":\"%s\",\"attempt_url\":\"%s\",\"updated\":\"%ld\",\"attempted\":\"%ld\",\"missing\":\"%s\"}", msc->cached_url, msc->escaped, updated, now, missing);
            if (!builder.error) write_file_text(meta, msc->output, builder.pos);
            free(msc);
        }
    }
    free(icons.keys); free(old); free(state);
    IconCatalog current = {0}; char *text = NULL; size_t current_size;
    if (!read_icons(path, &text, &current_size, &current)) compute_sha256_file(path, revision);
    free(current.keys); free(text);
}

int sources_ensure_fresh(const char *url, int force_refresh) {
    pthread_mutex_lock(&repository_cache_mutex);
    int result = ensure_catalog_fresh(url, force_refresh);
    pthread_mutex_unlock(&repository_cache_mutex);
    return result;
}

size_t sources_icons_json(const char *source_id, char *buf, size_t size) {
    if (!source_id || !buf || !size) return 0;
    buf[0] = 0; size_t result = 0;
    pthread_mutex_lock(&repository_cache_mutex);
    SourceEntry *sources = calloc(MAX_SOURCES, sizeof(*sources)); int count = 0;
    if (sources && !load_sources(sources, &count)) for (int i = 0; i < count; i++) {
        if (strcmp(sources[i].id, source_id)) continue;
        char path[512], meta[512], *text = NULL; size_t length = 0; IconCatalog icons = {0};
        icon_paths(sources[i].url, path, meta);
        if (!read_icons(path, &text, &length, &icons) && length < size) { memcpy(buf, text, length); buf[length] = 0; result = length; }
        free(icons.keys); free(text); break;
    }
    free(sources);
    pthread_mutex_unlock(&repository_cache_mutex);
    return result;
}
static int cache_filename(const char *name) {
    size_t prefix, digits;
    if (!strncmp(name, "repo_cache_", 11)) { prefix = 11; digits = 8; }
    else if (!strncmp(name, "repo_icons_", 11)) { prefix = 11; digits = 64; }
    else return 0;
    if (strlen(name) < prefix + digits) return 0;
    for (size_t i = prefix; i < prefix + digits; i++) if (!isxdigit((unsigned char)name[i])) return 0;
    const char *suffix = name + prefix + digits;
    if (!strcmp(suffix, ".json") || (digits == 8 && !strcmp(suffix, ".json.ts")) || (digits == 64 && !strcmp(suffix, ".meta"))) return 1;
    const char *temporary = NULL;
    if (!strncmp(suffix, ".json.tmp-", 10)) temporary = suffix + 10;
    if (digits == 8 && !strncmp(suffix, ".json.ts.tmp-", 13)) temporary = suffix + 13;
    if (digits == 64 && !strncmp(suffix, ".meta.tmp-", 10)) temporary = suffix + 10;
    if (!temporary || strlen(temporary) != 6) return 0;
    for (size_t i = 0; i < 6; i++) if (!isalnum((unsigned char)temporary[i])) return 0;
    return 1;
}
int sources_clear_cache(void) {
    pthread_mutex_lock(&repository_cache_mutex);
    DIR *directory = opendir(BASE_DATA_DIR); int result = directory ? 0 : (errno == ENOENT ? 0 : -1);
    if (directory) {
        struct dirent *entry;
        while ((entry = readdir(directory))) if (cache_filename(entry->d_name)) {
            if (unlinkat(dirfd(directory), entry->d_name, 0) && errno != ENOENT) result = -1;
        }
        closedir(directory);
    }
    pthread_mutex_unlock(&repository_cache_mutex);
    return result;
}

static void update_source_catalog_name(SourceEntry *source, const char *json, size_t json_size) {
    if (!source || !source->removable || !strcmp(source->id, "default") || !json || !json_size) return;
    char fresh_name[sizeof(source->name)] = "";
    int rc = json_extract_string_prefix(json, json + json_size, "name", fresh_name, sizeof(fresh_name));
    if (rc < 0 || fresh_name[0] == '\0' || strcmp(fresh_name, source->name) == 0) {
        return;
    }

    pthread_mutex_lock(&sources_mutex);
    SourceEntry *latest_sources = calloc(MAX_SOURCES, sizeof(SourceEntry));
    int latest_count = 0;
    int matched = 0;
    if (latest_sources && load_sources(latest_sources, &latest_count) == 0) {
        int updated = 0;
        for (int i = 0; i < latest_count; i++) {
            if (strcmp(latest_sources[i].id, source->id) == 0 ||
                strcmp(latest_sources[i].url, source->url) == 0) {
                matched = 1;
                if (strcmp(latest_sources[i].name, fresh_name) != 0) {
                    strncpy(latest_sources[i].name, fresh_name, sizeof(latest_sources[i].name) - 1);
                    latest_sources[i].name[sizeof(latest_sources[i].name) - 1] = '\0';
                    updated = 1;
                }
                break;
            }
        }
        if (updated) {
            save_sources(latest_sources, latest_count);
        }
    }
    free(latest_sources);
    pthread_mutex_unlock(&sources_mutex);

    if (matched) {
        strncpy(source->name, fresh_name, sizeof(source->name) - 1);
        source->name[sizeof(source->name) - 1] = '\0';
    }
}

/* [CN-FORK] v0.6.0.176 栈帧红线：payload 循环里那 17 个「6 倍最坏转义」缓冲合计 ~52KB，
 * 原来全放栈上（叠加内联的 ensure_icons 后整帧 66584 B）。真机 64KB 线程栈必死。
 * 整块搬堆上：进 si 循环前 malloc 一次、出循环或提前返回时 free（方向同 gs_cp_t）。 */
typedef struct {
    char id_pe[6 * 128], name_pe[6 * 256], filename_e[6 * 256], desc_e[6 * 1024];
    char ver_e[6 * 64], url_e[6 * 1024], src_id_e[6 * 128], src_name_e[6 * 256];
    char last_upd_e[6 * 64], cat_e[6 * 128], csum_e[6 * 65];
    char auth_e[6 * 128], det_url_e[6 * 1024], rn_e[6 * 2048], icon_e[6 * 128];
    char src_pe[6 * 1024], sd_pe[6 * 1024];
} RepoPayloadJsonScratch;

static size_t repository_list_locked(char *buf, size_t size, int force_refresh) {
    if (!buf || !size) return 0;
    SourceEntry *sources = calloc(MAX_SOURCES, sizeof(SourceEntry));
    if (!sources) {
        if (buf && size > 0) buf[0] = '\0';
        return 0;
    }
    int src_count = 0;
    if (load_sources(sources, &src_count)) { free(sources); buf[0] = 0; return 0; }

    /* Single-source mode: if multi_sources is off, only use source 0 */
    PldmgrConfig cfg;
    config_read(&cfg);
    if (!cfg.multi_sources_enabled && src_count > 1) {
        src_count = 1;
    }

    JsonListBuilder jb = { buf, size, 0, 1, 0 };
    buf[0] = '\0';

    json_append(&jb, "{\"sources\":[\n");

    /* [CN-FORK] v0.6.0.176 payload 转义缓冲（~52KB）走堆，见 RepoPayloadJsonScratch 注释 */
    RepoPayloadJsonScratch *sc = malloc(sizeof(*sc));
    if (!sc) { free(sources); json_builder_fail(&jb); return 0; }

    for (int si = 0; si < src_count; si++) {
        /* [CN-FORK] v0.5.1.65 默认云端仓库已关闭：整组不输出（不拉取也不列目录），
         * 用户自建源不受影响。 */
        if (!sources[si].enabled)
            continue;

        int fetch_ok = (ensure_catalog_fresh(sources[si].url, force_refresh) == 0);

        char cache_path[512], ts_path[512];
        get_cache_paths(sources[si].url, cache_path, sizeof(cache_path), ts_path, sizeof(ts_path));

        long last_update = 0;
        FILE *tf = fopen(ts_path, "r");
        if (tf) { fscanf(tf, "%ld", &last_update); fclose(tf); }

        RepoPayload *items = NULL; size_t count = 0; char icons_revision[65] = "";
        int have_catalog = fetch_ok || access(cache_path, F_OK) == 0;
        if (have_catalog) {
            char *cached = NULL;
            size_t cached_size = 0;

            if (read_file_text(cache_path, &cached, &cached_size) != 0 || !cached) {
                free(cached); free(sources); free(sc);
                json_builder_fail(&jb);
                return 0;
            }
            int parsed = parse_repository_payloads(cached, &items, &count);
            if (parsed != 0) {
                free(cached); free(items); free(sources); free(sc);
                json_builder_fail(&jb);
                return 0;
            }

            update_source_catalog_name(&sources[si], cached, cached_size);

            ensure_icons(sources[si].url, cached, items, count, icons_revision);
            free(cached);
        }

        char id_e[6 * 128], name_e[6 * 256], url_e[6 * 1024];
        json_escape_checked(&jb, sources[si].id, id_e, sizeof(id_e));
        json_escape_checked(&jb, sources[si].name, name_e, sizeof(name_e));
        json_escape_checked(&jb, sources[si].url, url_e, sizeof(url_e));

        if (json_append(&jb, "  {\"id\":\"%s\",\"name\":\"%s\",\"url\":\"%s\",\"last_update\":%ld,\"error\":%s,\"icons_revision\":\"%s\",\"payloads\":[\n",
            id_e, name_e, url_e, last_update, fetch_ok ? "false" : "true", icons_revision) != 0) {
            free(items); break;
        }

        if (have_catalog) {
            int first_payload = 1;
            for (size_t pi = 0; pi < count; pi++) {
                if ((is_pldmgr_name(items[pi].id) ||
                     is_pldmgr_name(items[pi].filename) || is_pldmgr_name(items[pi].name)) &&
                    !repository_self_update_entry_valid(&items[pi], sources[si].url)) continue;

                json_escape_checked(&jb, items[pi].id, sc->id_pe, sizeof(sc->id_pe));
                json_escape_checked(&jb, items[pi].name, sc->name_pe, sizeof(sc->name_pe));
                json_escape_checked(&jb, items[pi].filename, sc->filename_e, sizeof(sc->filename_e));
                json_escape_checked(&jb, items[pi].description, sc->desc_e, sizeof(sc->desc_e));
                json_escape_checked(&jb, items[pi].version, sc->ver_e, sizeof(sc->ver_e));
                json_escape_checked(&jb, items[pi].url, sc->url_e, sizeof(sc->url_e));
                json_escape_checked(&jb, items[pi].source, sc->src_pe, sizeof(sc->src_pe));
                json_escape_checked(&jb, items[pi].source_direct, sc->sd_pe, sizeof(sc->sd_pe));
                json_escape_checked(&jb, items[pi].checksum, sc->csum_e, sizeof(sc->csum_e));
                json_escape_checked(&jb, items[pi].last_update, sc->last_upd_e, sizeof(sc->last_upd_e));
                json_escape_checked(&jb, items[pi].category, sc->cat_e, sizeof(sc->cat_e));
                json_escape_checked(&jb, sources[si].id, sc->src_id_e, sizeof(sc->src_id_e));
                json_escape_checked(&jb, sources[si].name, sc->src_name_e, sizeof(sc->src_name_e));
                json_escape_checked(&jb, items[pi].author, sc->auth_e, sizeof(sc->auth_e));
                json_escape_checked(&jb, items[pi].details_url, sc->det_url_e, sizeof(sc->det_url_e));
                json_escape_checked(&jb, items[pi].release_notes, sc->rn_e, sizeof(sc->rn_e));
                json_escape_checked(&jb, items[pi].icon_key, sc->icon_e, sizeof(sc->icon_e));

                if (json_append(&jb, "%s    {\"id\":\"%s\",\"name\":\"%s\",\"filename\":\"%s\",\"description\":\"%s\","
                    "\"version\":\"%s\",\"checksum\":\"%s\",\"last_update\":\"%s\",\"url\":\"%s\","
                    "\"source\":\"%s\",\"source_direct\":\"%s\","
                    "\"source_id\":\"%s\",\"source_name\":\"%s\",\"category\":\"%s\","
                    "\"author\":\"%s\",\"details_url\":\"%s\",\"release_notes\":\"%s\",\"icon_key\":\"%s\"}",
                    first_payload ? "" : ",\n",
                    sc->id_pe, sc->name_pe, sc->filename_e, sc->desc_e, sc->ver_e, sc->csum_e, sc->last_upd_e, sc->url_e,
                    sc->src_pe, sc->sd_pe,
                    sc->src_id_e, sc->src_name_e, sc->cat_e, sc->auth_e, sc->det_url_e, sc->rn_e, sc->icon_e) != 0) {
                    break;
                }
                first_payload = 0;
            }
            if (!first_payload) {
                json_append(&jb, "\n");
            }

            if (items) free(items);
        }

        /* [CN-FORK] v0.5.1.65 尾部逗号不能再按「是不是最后一个下标」判断：被跳过的默认源
         * 若排在最后会产出 `...,]}` 这种非法 JSON，必须看后面还有没有启用源。 */
        int more = 0;
        for (int k = si + 1; k < src_count; k++) {
            if (sources[k].enabled) { more = 1; break; }
        }
        if (json_append(&jb, "  ]}%s\n", more ? "," : "") != 0) {
            break;
        }
    }

    json_append(&jb, "]}\n");
    free(sources);
    free(sc);
    return jb.pos;
}

size_t sources_multi_repository_list_json(char *buf, size_t size, int force_refresh) {
    pthread_mutex_lock(&repository_cache_mutex);
    size_t result = repository_list_locked(buf, size, force_refresh);
    pthread_mutex_unlock(&repository_cache_mutex);
    return result;
}

int sources_repo_install(const char *source_url, const char *id,
                         const char *mode, const char *replace_slot,
                         char *out_json, size_t out_size) {
    JsonListBuilder jb = { out_json, out_size, 0, 1, 0 };
    if (!slot_mode_valid(mode)) {
        json_append(&jb, "{\"ok\":false,\"message\":\"Invalid storage mode\"}"); return -1;
    }
    char safe_id[128];
    slot_slug(id, safe_id, sizeof(safe_id));
    if (safe_id[0] == '\0') {
        json_append(&jb, "{\"ok\":false,\"message\":\"Missing payload id\"}");
        return -1;
    }

    SourceEntry *sources = calloc(MAX_SOURCES, sizeof(SourceEntry));
    if (!sources) {
        json_append(&jb, "{\"ok\":false,\"message\":\"Memory error\"}");
        return -1;
    }
    int src_count = 0;
    if (load_sources(sources, &src_count)) { free(sources); json_append(&jb, "{\"ok\":false,\"message\":\"Failed to read sources\"}"); return -1; }

    /* Match source */
    SourceEntry src;
    memset(&src, 0, sizeof(src));
    if (source_url && source_url[0]) {
        int found = 0;
        char h_req[16];
        slot_source_hash(source_url, h_req, sizeof(h_req));
        for (int i = 0; i < src_count; i++) {
            if (strcmp(sources[i].url, source_url) == 0 || strcmp(sources[i].id, source_url) == 0) {
                src = sources[i];
                found = 1;
                break;
            }
            char h_src[16];
            slot_source_hash(sources[i].url, h_src, sizeof(h_src));
            if (strcmp(h_req, h_src) == 0) {
                src = sources[i];
                found = 1;
                break;
            }
        }
        if (!found) {
            strncpy(src.id, "custom", sizeof(src.id) - 1);
            strncpy(src.name, "Custom Repository", sizeof(src.name) - 1);
            strncpy(src.url, source_url, sizeof(src.url) - 1);
        }
    } else {
        src = sources[0];
    }
    free(sources);

    int self_update = !strcmp(safe_id, "pldmgr") && is_pldmgr_official_source(src.url);
    if (is_pldmgr_name(safe_id) && (!self_update ||
        (source_url && source_url[0] && !is_pldmgr_official_source(source_url) && strcmp(source_url, src.id)))) {
        json_append(&jb, "{\"ok\":false,\"message\":\"Payload Manager updates require the official repository\"}"); return -1;
    }
    if (self_update && ((mode && mode[0]) || (replace_slot && replace_slot[0]))) {
        json_append(&jb, "{\"ok\":false,\"message\":\"Self-updates do not support storage modes\"}"); return -1;
    }

    char h8[16];
    slot_source_hash(src.url, h8, sizeof(h8));

    char target_slot[128];
    slot_repo_key(safe_id, src.url, target_slot, sizeof(target_slot));

    /* Conflict check if mode not specified */
    int is_mode_replace = (mode && strcmp(mode, "replace") == 0);
    int is_mode_keep_both = (mode && strcmp(mode, "keep_both") == 0);

    if (!self_update && !is_mode_replace && !is_mode_keep_both) {
        SlotMeta *existing_slots = NULL;
        size_t existing_count = 0;
        if (slot_list_all(&existing_slots, &existing_count) != 0) {
            json_append(&jb, "{\"ok\":false,\"message\":\"Failed to scan installed slots\"}");
            return -1;
        }
        if (existing_slots) {
            for (size_t i = 0; i < existing_count; i++) {
                if (strcmp(existing_slots[i].kind, SLOT_KIND_REPO) == 0 &&
                    strcmp(existing_slots[i].id, safe_id) == 0 &&
                    strcmp(existing_slots[i].slot_key, target_slot) != 0) {
                    char existing_hash[16];
                    slot_source_hash(existing_slots[i].source_url, existing_hash, sizeof(existing_hash));
                    if (!strcmp(existing_hash, h8)) continue;
                    /* Conflict found with another source! */
                    char c_slot[6 * 128], c_name[6 * 256], c_ver[6 * 64];
                    json_escape_checked(&jb, existing_slots[i].slot_key, c_slot, sizeof(c_slot));
                    json_escape_checked(&jb, existing_slots[i].source_name, c_name, sizeof(c_name));
                    json_escape_checked(&jb, existing_slots[i].version, c_ver, sizeof(c_ver));

                    json_append(&jb,
                             "{\"ok\":false,\"conflict\":{\"slot\":\"%s\",\"source_name\":\"%s\",\"version\":\"%s\"}}",
                             c_slot, c_name, c_ver);
                    slot_free_all(existing_slots);
                    return jb.error ? -1 : 0; /* Handled conflict, return 200 JSON with conflict object */
                }
            }
            slot_free_all(existing_slots);
        }
    }

    /* Load cached repository */
    char cache_path[512], ts_path[512];
    get_cache_paths(src.url, cache_path, sizeof(cache_path), ts_path, sizeof(ts_path));

    RepoPayload *items = NULL;
    size_t count = 0;
    int refreshed = 0;

    char *cached = NULL;
    size_t csize = 0;
    if (read_file_text(cache_path, &cached, &csize) == 0 && cached) {
        int parsed = parse_repository_payloads(cached, &items, &count);
        free(cached);
        if (parsed != 0) {
            json_append(&jb, "{\"ok\":false,\"message\":\"Failed to parse repository\"}");
            return -1;
        }
    }

    int found_idx = -1;
    for (size_t i = 0; i < count; i++) {
        if (strcmp(items[i].id, safe_id) == 0) {
            found_idx = (int)i;
            break;
        }
    }

    if (found_idx < 0) {
        /* Missing from cache, force refresh */
        if (items) free(items);
        items = NULL;
        count = 0;
        sources_ensure_fresh(src.url, 1);
        refreshed = 1;

        if (read_file_text(cache_path, &cached, &csize) == 0 && cached) {
            int parsed = parse_repository_payloads(cached, &items, &count);
            free(cached);
            if (parsed != 0) {
                json_append(&jb, "{\"ok\":false,\"message\":\"Failed to parse repository\"}");
                return -1;
            }
            for (size_t i = 0; i < count; i++) {
                if (strcmp(items[i].id, safe_id) == 0) {
                    found_idx = (int)i;
                    break;
                }
            }
        }
    }

    if (found_idx < 0) {
        if (items) free(items);
        json_append(&jb, "{\"ok\":false,\"message\":\"Payload no longer in repository\"}");
        return -1;
    }

    RepoPayload *entry = (RepoPayload *)calloc(1, sizeof(RepoPayload));
    if (!entry) {
        if (items) free(items);
        json_append(&jb, "{\"ok\":false,\"message\":\"Out of memory\"}");
        return -1;
    }
    *entry = items[found_idx];
    free(items);
    items = NULL;

    /* Block installing pldmgr as a payload */
    if (!self_update && (is_pldmgr_name(safe_id) || is_pldmgr_name(entry->id) ||
        is_pldmgr_name(entry->filename) || is_pldmgr_name(entry->name))) {
        free(entry);
        json_append(&jb,
                 "{\"ok\":false,\"message\":\"Payload Manager cannot be installed as a payload\"}");
        return -1;
    }

    /* Validate filename */
    if (entry->filename[0] == '\0' || strstr(entry->filename, "/") || strstr(entry->filename, "..")) {
        free(entry);
        json_append(&jb, "{\"ok\":false,\"message\":\"Invalid payload filename in repository\"}");
        return -1;
    }
    const char *ext = strrchr(entry->filename, '.');
    if (!ext || (strcasecmp(ext, ".elf") != 0 && strcasecmp(ext, ".bin") != 0)) {
        free(entry);
        json_append(&jb, "{\"ok\":false,\"message\":\"Unsupported file extension in repository\"}");
        return -1;
    }

    /* Download into a unique private file, never an installed slot. */
    if (ensure_dir_recursive(PAYLOADS_STORAGE_DIR)) {
        free(entry);
        json_append(&jb, "{\"ok\":false,\"message\":\"Failed to create storage directory\"}"); return -1;
    }
    char part_path[768];
    snprintf(part_path, sizeof(part_path), "%s/.download-XXXXXX", PAYLOADS_STORAGE_DIR);
    int part_fd = mkstemp(part_path);
    if (part_fd < 0) {
        free(entry); json_append(&jb, "{\"ok\":false,\"message\":\"Failed to stage download\"}"); return -1;
    }
    if (close(part_fd)) {
        remove(part_path); free(entry);
        json_append(&jb, "{\"ok\":false,\"message\":\"Failed to close staging file\"}"); return -1;
    }

    /* At most two attempts. Every entry, including a refreshed replacement,
     * is validated before downloading and verifies its own supplied checksum. */
    int installed_bytes_verified = 0;
    for (int attempt = 0; attempt < 2; attempt++) {
        if ((self_update ? repository_self_update_entry_valid(entry, src.url) : repository_entry_valid(entry)) &&
            download_to_file(entry->url, part_path) == 0) {
            char calc[65];
            if (!entry->checksum[0] ||
                (!compute_sha256_file(part_path, calc) && !strcasecmp(calc, entry->checksum))) {
                installed_bytes_verified = 1; break;
            }
        }
        if (refreshed || attempt == 1 || sources_ensure_fresh(src.url, 1)) break;
        refreshed = 1;
        cached = NULL; items = NULL; count = 0;
        int ref_found = 0;
        if (!read_file_text(cache_path, &cached, &csize) && cached &&
            !parse_repository_payloads(cached, &items, &count)) {
            for (size_t i = 0; i < count; i++) {
                if (!strcmp(items[i].id, safe_id)) { *entry = items[i]; ref_found = 1; break; }
            }
        }
        free(cached); free(items); cached = NULL; items = NULL;
        if (!ref_found) break;
    }
    if (!installed_bytes_verified) {
        free(entry); remove(part_path);
        json_append(&jb, "{\"ok\":false,\"message\":\"Download or integrity verification failed\"}"); return -1;
    }

    if (self_update) {
        int result = repository_install_self_update(entry, src.url, part_path);
        free(entry); remove(part_path);
        if (result) {
            json_append(&jb, "{\"ok\":false,\"message\":\"Self-update must be a newer versioned ELF matching the repository\"}"); return -1;
        }
        json_append(&jb, "{\"ok\":true,\"self_update\":true,\"restart_required\":true,\"refreshed\":%s}", refreshed ? "true" : "false");
        return jb.error ? -1 : 0;
    }

    /* Write meta.json */
    SlotMeta *meta = (SlotMeta *)calloc(1, sizeof(SlotMeta));
    if (!meta) {
        free(entry); remove(part_path);
        json_append(&jb, "{\"ok\":false,\"message\":\"Out of memory\"}");
        return -1;
    }
    meta->schema = PAYLOAD_SLOT_SCHEMA;
    strcpy(meta->kind, SLOT_KIND_REPO);
    strncpy(meta->slot_key, target_slot, sizeof(meta->slot_key) - 1);
    strncpy(meta->filename, entry->filename, sizeof(meta->filename) - 1);

    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    strftime(meta->installed_at, sizeof(meta->installed_at), "%Y-%m-%dT%H:%M:%S%z", &tmv);

    strncpy(meta->id, safe_id, sizeof(meta->id) - 1);
    strncpy(meta->source_url, src.url, sizeof(meta->source_url) - 1);
    strncpy(meta->source_name, src.name, sizeof(meta->source_name) - 1);
    strncpy(meta->name, entry->name, sizeof(meta->name) - 1);
    strncpy(meta->version, entry->version, sizeof(meta->version) - 1);
    strncpy(meta->checksum, entry->checksum, sizeof(meta->checksum) - 1);
    strncpy(meta->last_update, entry->last_update, sizeof(meta->last_update) - 1);
    strncpy(meta->url, entry->url, sizeof(meta->url) - 1);
    strncpy(meta->source, entry->source, sizeof(meta->source) - 1);
    strncpy(meta->source_direct, entry->source_direct, sizeof(meta->source_direct) - 1);
    strncpy(meta->category, entry->category, sizeof(meta->category) - 1);
    strncpy(meta->description, entry->description, sizeof(meta->description) - 1);
    strncpy(meta->author, entry->author, sizeof(meta->author) - 1);
    strncpy(meta->details_url, entry->details_url, sizeof(meta->details_url) - 1);

    char committed_slot[128];
    char committed_payload[640] = "";
    /* An omitted mode preserves the established reinstall/update behavior;
     * explicit new is exclusive, and keep_both also works within one source. */
    const char *commit_mode = mode && mode[0] ? mode : "replace";
    int committed = slot_commit_file(target_slot, part_path, commit_mode, meta, !is_mode_replace && !is_mode_keep_both,
                                     committed_slot, sizeof(committed_slot));
    if (!committed && meta->payload_path[0])
        snprintf(committed_payload, sizeof(committed_payload), "%s", meta->payload_path);
    free(meta); remove(part_path);
    if (committed) {
        free(entry);
        json_append(&jb, "{\"ok\":false,\"message\":\"Storage commit failed (conflict or I/O error)\"}"); return -1;
    }
    /* [CN-FORK] v0.6.0.178 仓库安装同样是一次「装载荷」：引导器目录 /data/ps5_autoloader/
     * 里有这个工具时要换成新版本（本工具没登记也要登记）。此前只有网页上传那条路会同步，
     * 从仓库/自定义源装完新版之后引导器停在旧版本，就是这条路径漏了。 */
    if (committed_payload[0] && access(committed_payload, F_OK) == 0)
        payload_mgr_autoloader_sync(entry->filename, committed_payload,
                                    payload_mgr_is_self_payload(entry->filename));
    snprintf(target_slot, sizeof(target_slot), "%s", committed_slot);
    int old_cleanup_failed = 0;
    /* Retire a conflicting source only after the new slot is usable. */
    if (is_mode_replace && replace_slot && replace_slot[0] && strcmp(replace_slot, target_slot)) {
        int autoload_result = pldmgr_autoload_update_config_entry(replace_slot, target_slot);
        int favorites_result = pldmgr_favorites_update_entry(replace_slot, target_slot);
        if (autoload_result || favorites_result || slot_delete(replace_slot)) {
            old_cleanup_failed = 1;
            pldmgr_log("[PLDMGR] New repository slot committed; old slot cleanup incomplete: %s\n", replace_slot);
        }
    }

    char sk_e[6 * 128], id_e[6 * 128], ver_e[6 * 64], fn_e[6 * 256];
    json_escape_checked(&jb, target_slot, sk_e, sizeof(sk_e));
    json_escape_checked(&jb, safe_id, id_e, sizeof(id_e));
    json_escape_checked(&jb, entry->version, ver_e, sizeof(ver_e));
    json_escape_checked(&jb, entry->filename, fn_e, sizeof(fn_e));

    json_append(&jb,
             "{\"ok\":true,\"slot\":\"%s\",\"id\":\"%s\",\"version\":\"%s\",\"filename\":\"%s\",\"refreshed\":%s%s}",
             sk_e, id_e, ver_e, fn_e, refreshed ? "true" : "false",
             old_cleanup_failed ? ",\"warning\":\"New payload installed; previous slot cleanup incomplete\"" : "");

    pldmgr_log("[PLDMGR] Repo install success: slot=%s id=%s ver=%s\n", target_slot, id, entry->version);
    free(entry);
    return jb.error ? -1 : 0;
}
