#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include "config.h"
#include "pldmgr.h"
#include "json_helpers.h"
#include "utils.h" /* [CN-FORK] pldmgr_utils_strip_config_line */
#include "cJSON.h" /* [CN-FORK] v0.5.2.145 取 ARCHIVE_GLOBAL_CONFIG 的字符串值 */

/* Covers read/modify/write across settings, favorites and autoload edits.
 * Never call loader/autoload callbacks with this lock held. */
static pthread_mutex_t config_mutex = PTHREAD_MUTEX_INITIALIZER;
#define LIST_CAPACITY 4096
#define CONFIG_LIMIT (64 * 1024)

/* [CN-FORK] v0.6.0.176 配置键表。
 *
 * 上游 v0.6.0 用「键名数组 + 平行指针数组」把解析/写回统一起来，本 fork 在它的
 * 基础上把键表扩成「键名 + 结构体偏移 + 类型 + 合法区间」三元组，这样：
 *   ① fork 自己的 6 个键（KILL_DISC_PLAYER_ON_STARTUP / DEFAULT_REPO_ENABLED /
 *      FAN_*）能挂进同一套机制，不再需要手写 strncmp 分支；
 *   ② 校验规则跟着键走 —— 布尔键只收 true/false，整型键收十进制并按区间夹，
 *      不会再出现「布尔键塞了 3600」这种脏值。
 * write_config 的「保留未知键」判定也用这张表，所以 fork 的键写回时不会被当成
 * 未知键重复追加。 */
typedef enum { SET_BOOL, SET_INT } SettingKind;
typedef struct {
    const char *key;
    size_t offset;
    SettingKind kind;
    long min, max;
} SettingKey;

static const SettingKey setting_keys[] = {
    {"AUTOLOAD_ENABLED",            offsetof(PldmgrConfig, autoload_enabled),      SET_BOOL, 0, 1},
    {"AUTO_BROWSER_OPEN",           offsetof(PldmgrConfig, auto_browser_open),     SET_BOOL, 0, 1},
    {"AUTOLOAD_DELAY",              offsetof(PldmgrConfig, autoload_delay),        SET_INT,  0, 3600},
    {"SCAN_USB_PAYLOADS",           offsetof(PldmgrConfig, scan_usb_payloads),     SET_BOOL, 0, 1},
    {"AUTO_INSTALL_APP",            offsetof(PldmgrConfig, auto_install_app),      SET_BOOL, 0, 1},
    {"MULTI_SOURCES_ENABLED",       offsetof(PldmgrConfig, multi_sources_enabled), SET_BOOL, 0, 1},
    /* ── 上游 v0.6.2 新增的两键（合并 upstream/main 时手工挂进本表）──────── */
    {"SHOW_DASHBOARD_ICONS",        offsetof(PldmgrConfig, show_dashboard_icons),   SET_BOOL, 0, 1},
    {"AUTOLOAD_RETURN_TO_DASHBOARD", offsetof(PldmgrConfig, autoload_return_to_dashboard), SET_BOOL, 0, 1},
    /* ── [CN-FORK] 以下 6 个键上游没有 ───────────────────── */
    {"KILL_DISC_PLAYER_ON_STARTUP", offsetof(PldmgrConfig, kill_disc_player),      SET_BOOL, 0, 1},
    {"DEFAULT_REPO_ENABLED",        offsetof(PldmgrConfig, default_repo_enabled),  SET_BOOL, 0, 1},
    {"FAN_CONTROL_ENABLED",         offsetof(PldmgrConfig, fan_enabled),           SET_BOOL, 0, 1},
    {"FAN_TARGET",                  offsetof(PldmgrConfig, fan_target),            SET_INT,  0, 1000},
    {"FAN_APPLIED",                 offsetof(PldmgrConfig, fan_applied),           SET_BOOL, 0, 1},
    {"FAN_INTERVAL",                offsetof(PldmgrConfig, fan_interval),          SET_INT,  0, 86400},
};
#define SETTING_COUNT ((int)(sizeof(setting_keys) / sizeof(setting_keys[0])))
/* 列表项在 Update 位图里的下标，紧跟设置键之后。 */
enum { IDX_AUTOLOAD_LIST = SETTING_COUNT, IDX_FAVORITES };
#define SETTING_MASK ((1u << SETTING_COUNT) - 1u)

static int setting_lookup(const char *key) {
    for (int i = 0; i < SETTING_COUNT; i++)
        if (!strcmp(key, setting_keys[i].key)) return i;
    return -1;
}

static void defaults(PldmgrConfig *cfg) {
    *cfg = (PldmgrConfig){
        .auto_browser_open = 1, .autoload_delay = 5, .auto_install_app = 1,
        /* [CN-FORK] v0.5.2.67 默认开启多来源：内置官方源默认关掉后，
         * 靠多来源模式才能显示预置的 repo.630810.xyz。 */
        .multi_sources_enabled = 1,
        /* [CN-FORK] v0.5.2.67 内置官方源（itsPLK GitHub 镜像）国内经常拉不动，
         * 默认关掉，开机不再拉 payloads.json。 */
        .default_repo_enabled = 0,
        /* [CN-FORK] BD-JB 用户开机杀光盘播放器默认开。 */
        .kill_disc_player = 1,
        /* 上游 v0.6.2：仪表盘 payload 图标默认显示（关掉只是省地方，卡片尺寸不变）。 */
        .show_dashboard_icons = 1,
        /* 上游 v0.6.2：自动加载全部成功后回仪表盘，默认关（用户口径：结果要多停一会儿）。 */
        .autoload_return_to_dashboard = 0,
        /* [CN-FORK] 风扇温控（模型 A）默认关，目标温度 72°C。 */
        .fan_enabled = 0, .fan_target = 72, .fan_applied = 0, .fan_interval = 7,
    };
}
static int read_config(PldmgrConfig *cfg) {
    defaults(cfg);
    char *text = NULL; size_t size = 0;
    if (read_file_text(PLDMGR_CONFIG_PATH, &text, &size)) return errno == ENOENT ? 0 : -1;
    if (size > CONFIG_LIMIT || memchr(text, 0, size)) { free(text); return -1; }
    char *save = NULL;
    for (char *line = strtok_r(text, "\r\n", &save); line; line = strtok_r(NULL, "\r\n", &save)) {
        char *equal = strchr(line, '=');
        if (!equal) continue;
        *equal++ = 0;
        char *end; errno = 0;
        long value = strtol(equal, &end, 10);
        if (errno || *end || end == equal) continue;
        if (!strcmp(line, "LAST_REPOSITORY_UPDATE")) { if (value >= 0) cfg->last_repository_update = value; continue; }
        int index = setting_lookup(line);
        if (index < 0) continue;
        /* 旧文件里可能留着越界/非布尔值（历史手改），一律忽略、保留默认。 */
        const SettingKey *setting = &setting_keys[index];
        if (value < setting->min || value > setting->max) continue;
        *(int *)((char *)cfg + setting->offset) = (int)value;
    }
    free(text); return 0;
}
void config_read(PldmgrConfig *cfg) { if (cfg) read_config(cfg); }
static int write_config(const PldmgrConfig *cfg) {
    /* [CN-FORK] 键数由 7 增至 13，512 字节的栈缓冲不够放（含 last_update 的 20 位数字），
     * 提到 1024 并保留长度检查。 */
    char text[1024];
    int length = snprintf(text, sizeof(text),
        "AUTOLOAD_ENABLED=%d\nLAST_REPOSITORY_UPDATE=%ld\nAUTO_BROWSER_OPEN=%d\n"
        "AUTOLOAD_DELAY=%d\nKILL_DISC_PLAYER_ON_STARTUP=%d\nSCAN_USB_PAYLOADS=%d\n"
        "AUTO_INSTALL_APP=%d\nMULTI_SOURCES_ENABLED=%d\nDEFAULT_REPO_ENABLED=%d\n"
        "FAN_CONTROL_ENABLED=%d\nFAN_TARGET=%d\nFAN_APPLIED=%d\nFAN_INTERVAL=%d\n",
        !!cfg->autoload_enabled, cfg->last_repository_update, !!cfg->auto_browser_open,
        cfg->autoload_delay, !!cfg->kill_disc_player, !!cfg->scan_usb_payloads,
        !!cfg->auto_install_app, !!cfg->multi_sources_enabled, !!cfg->default_repo_enabled,
        !!cfg->fan_enabled, cfg->fan_target, !!cfg->fan_applied, cfg->fan_interval);
    if (length < 0 || (size_t)length >= sizeof(text) || ensure_dir_recursive(BASE_DATA_DIR)) return -1;
    char *old = NULL; size_t size = 0;
    if (read_file_text(PLDMGR_CONFIG_PATH, &old, &size) && errno != ENOENT) return -1;
    if (size > CONFIG_LIMIT || (old && memchr(old, 0, size))) { free(old); return -1; }
    char *complete = malloc((size_t)length + size + 2);
    if (!complete) { free(old); return -1; }
    memcpy(complete, text, (size_t)length);
    size_t pos = (size_t)length;
    const char *line = old;
    while (line && line < old + size) {
        const char *end = memchr(line, '\n', (size_t)(old + size - line));
        if (!end) end = old + size;
        size_t n = (size_t)(end - line);
        int known = n >= 23 && !memcmp(line, "LAST_REPOSITORY_UPDATE=", 23);
        for (int i = 0; i < SETTING_COUNT && !known; i++) {
            size_t k = strlen(setting_keys[i].key);
            if (n > k && !memcmp(line, setting_keys[i].key, k) && line[k] == '=') known = 1;
        }
        if (!known) { memcpy(complete + pos, line, n); pos += n; complete[pos++] = '\n'; }
        line = end < old + size ? end + 1 : old + size;
    }
    int result = pos > CONFIG_LIMIT ? -1 : write_file_text(PLDMGR_CONFIG_PATH, complete, pos);
    free(complete); free(old); return result;
}
int config_write(const PldmgrConfig *cfg) {
    if (!cfg || cfg->autoload_delay < 0 || cfg->autoload_delay > 3600 || cfg->last_repository_update < 0) return -1;
    pthread_mutex_lock(&config_mutex);
    int result = write_config(cfg);
    pthread_mutex_unlock(&config_mutex); return result;
}
int config_read_bool(const char *key, int default_val) {
    if (!key) return default_val;
    char *text = NULL; size_t size = 0;
    if (read_file_text(PLDMGR_CONFIG_PATH, &text, &size)) return default_val;
    int result = default_val;
    if (size <= CONFIG_LIMIT && !memchr(text, 0, size)) {
        char *save = NULL;
        for (char *line = strtok_r(text, "\r\n", &save); line; line = strtok_r(NULL, "\r\n", &save)) {
            size_t length = strlen(key);
            if (strlen(line) > length && !strncmp(line, key, length) && line[length] == '=') {
                if (!strcmp(line + length + 1, "0")) result = 0;
                else if (!strcmp(line + length + 1, "1")) result = 1;
                break;
            }
        }
    }
    free(text); return result;
}
int config_upsert_value(const char *key, const char *value) {
    if (!key || !key[0] || !value || strlen(key) > 255 || strlen(value) > CONFIG_LIMIT ||
        strpbrk(key, "=\r\n") || strpbrk(value, "\r\n")) return -1;
    pthread_mutex_lock(&config_mutex);
    char *old = NULL; size_t size = 0;
    int result = -1;
    if (read_file_text(PLDMGR_CONFIG_PATH, &old, &size) && errno != ENOENT) goto done;
    if (size > CONFIG_LIMIT || (old && memchr(old, 0, size))) goto done;
    size_t capacity = size + strlen(key) + strlen(value) + 4;
    char *text = malloc(capacity);
    if (!text) goto done;
    size_t pos = 0;
    /* Preserve unrelated and unknown keys without fixed-line truncation. */
    const char *line = old;
    while (line && line < old + size) {
        const char *end = memchr(line, '\n', (size_t)(old + size - line));
        if (!end) end = old + size;
        size_t n = (size_t)(end - line), key_size = strlen(key);
        if (!(n > key_size && !memcmp(line, key, key_size) && line[key_size] == '=')) {
            memcpy(text + pos, line, n); pos += n; text[pos++] = '\n';
        }
        line = end < old + size ? end + 1 : old + size;
    }
    int count = snprintf(text + pos, capacity - pos, "%s=%s\n", key, value);
    if (count >= 0 && (size_t)count < capacity - pos && !ensure_dir_recursive(BASE_DATA_DIR))
        result = write_file_text(PLDMGR_CONFIG_PATH, text, pos + (size_t)count);
    free(text);
done:
    free(old); pthread_mutex_unlock(&config_mutex); return result;
}
int config_read_last_update(long *out_ts) {
    if (!out_ts) return -1;
    PldmgrConfig cfg; int result = read_config(&cfg); *out_ts = cfg.last_repository_update; return result;
}
int config_write_last_update(long ts) {
    if (ts < 0) return -1;
    char value[64]; snprintf(value, sizeof(value), "%ld", ts);
    return config_upsert_value("LAST_REPOSITORY_UPDATE", value);
}

/* ── [CN-FORK] v0.5.2.145 全局存档配置（单独文件）──────────────────────────
 * 见 include/config.h 的语义说明。这两条刻意不做任何校验：存进来什么就原样落盘，
 * 读出来什么就原样交给调用方 —— 校验（哪些键合法、数值夹到多少）在解析端
 * 各自做（game_auto.c / game_saves.c），与 per-game 那份同一套代码。
 * 在这里"顺便"校验一次只会多一份可能跑偏的规则。 */
int config_write_archive_global(const char *json) {
    if (!json) return -1;
    ensure_dir_recursive(BASE_DATA_DIR);
    FILE *f = fopen(ARCHIVE_GLOBAL_PATH, "w");
    if (!f) return -1;
    size_t n = strlen(json);
    size_t w = fwrite(json, 1, n, f);
    fclose(f);
    return (w == n) ? 0 : -1;
}

int config_read_archive_global(char *out, size_t osz) {
    if (!out || osz < 2) return -1;
    out[0] = 0;
    FILE *f = fopen(ARCHIVE_GLOBAL_PATH, "r");
    if (!f) return -1;
    size_t n = fread(out, 1, osz - 1, f);
    fclose(f);
    out[n] = 0;
    /* 空文件 / 只有空白 = 没配置，当作没有（否则调用方要自己再判一次）。 */
    int blank = 1;
    for (size_t i = 0; i < n; i++)
        if (out[i] != ' ' && out[i] != '\n' && out[i] != '\r' && out[i] != '\t') { blank = 0; break; }
    if (blank) { out[0] = 0; return -1; }
    return (int)n;
}

/* [CN-FORK] v0.6.0.177 多媒体存档配置的落盘 / 读取。与上面的 archive_global 那对
 * 一模一样（单独一份 JSON、不校验、不进 key=value 行式文件），差异只在路径。 */
int config_write_media_archive(const char *json) {
    if (!json) return -1;
    ensure_dir_recursive(BASE_DATA_DIR);
    FILE *f = fopen(MEDIA_ARCHIVE_PATH, "w");
    if (!f) return -1;
    size_t n = strlen(json);
    size_t w = fwrite(json, 1, n, f);
    fclose(f);
    return (w == n) ? 0 : -1;
}

int config_read_media_archive(char *out, size_t osz) {
    if (!out || osz < 2) return -1;
    out[0] = 0;
    FILE *f = fopen(MEDIA_ARCHIVE_PATH, "r");
    if (!f) return -1;
    size_t n = fread(out, 1, osz - 1, f);
    fclose(f);
    out[n] = 0;
    /* 空文件 / 只有空白 = 没配过。返回 -1 让调用方按「未启用」处理，
     * 不要把空白当成一份合法配置读出去。 */
    int blank = 1;
    for (size_t i = 0; i < n; i++)
        if (out[i] != ' ' && out[i] != '\n' && out[i] != '\r' && out[i] != '\t') { blank = 0; break; }
    if (blank) { out[0] = 0; return -1; }
    return (int)n;
}

/* [CN-FORK] v0.5.2.145 /set_config 里的 ARCHIVE_GLOBAL_CONFIG 单独落盘。
 *
 * 这里**必须**用 cJSON 取，不能用 strstr 找引号的老办法：这个值本身就是一个 JSON
 * **字符串**（内容是转义过的 JSON），里面全是 \" 与 \\，手写扫描极容易在第一个内层
 * 引号处截断，存进去就是半个 JSON。cJSON 会替我们做完整的反转义。
 * 值以对象或字符串形式给都认（前端给的是 JSON.stringify 出来的字符串）。
 *
 * 上游 v0.6.0 的 update_member 对未知键一律返回 0（忽略），所以这个键不会让
 * 整份 /set_config 变成 CONFIG_INVALID —— 单独在这里补一次即可。 */
static void config_apply_archive_global(const char *json_data) {
    cJSON *root = cJSON_Parse(json_data);
    if (!root) return;
    cJSON *ag = cJSON_GetObjectItem(root, "ARCHIVE_GLOBAL_CONFIG");
    if (cJSON_IsString(ag) && ag->valuestring && ag->valuestring[0]) {
        if (config_write_archive_global(ag->valuestring) == 0)
            pldmgr_log("[PLDMGR] Saved global archive config to %s\n", ARCHIVE_GLOBAL_PATH);
        else
            pldmgr_log("[PLDMGR] 全局存档配置写入失败（%s）\n", ARCHIVE_GLOBAL_PATH);
    } else if (cJSON_IsObject(ag)) {
        char *s = cJSON_PrintUnformatted(ag);
        if (s) {
            if (config_write_archive_global(s) == 0)
                pldmgr_log("[PLDMGR] Saved global archive config to %s\n", ARCHIVE_GLOBAL_PATH);
            free(s);
        }
    }
    cJSON_Delete(root);
}

/* [CN-FORK] v0.6.0.177 /set_config 里 MEDIA_ARCHIVE_CONFIG 这个键的处理。
 * 与 config_apply_archive_global 完全同构（那份是存档的，这份是多媒体的），
 * 只是落点换成 MEDIA_ARCHIVE_PATH。为什么复制而不抽成一个带路径参数的函数：
 * 两处的失败日志与将来的分支（媒体这份可能还要顺手踢一次自动备份）都会长得不一样，
 * 抽参数省下的三行换不来可读性。 */
static void config_apply_media_archive(const char *json_data) {
    cJSON *root = cJSON_Parse(json_data);
    if (!root) return;
    cJSON *ma = cJSON_GetObjectItem(root, "MEDIA_ARCHIVE_CONFIG");
    if (cJSON_IsString(ma) && ma->valuestring && ma->valuestring[0]) {
        if (config_write_media_archive(ma->valuestring) == 0)
            pldmgr_log("[PLDMGR] Saved global media archive config to %s\n", MEDIA_ARCHIVE_PATH);
        else
            pldmgr_log("[PLDMGR] 多媒体存档配置写入失败（%s）\n", MEDIA_ARCHIVE_PATH);
    } else if (cJSON_IsObject(ma)) {
        char *s = cJSON_PrintUnformatted(ma);
        if (s) {
            if (config_write_media_archive(s) == 0)
                pldmgr_log("[PLDMGR] Saved global media archive config to %s\n", MEDIA_ARCHIVE_PATH);
            free(s);
        }
    }
    cJSON_Delete(root);
}

/* CSV is a legacy wire format; commas separate entries, never occur inside one.
 * Newlines/control characters cannot be represented safely in line files. */
static int csv_lines(const char *csv, char *out, size_t capacity) {
    size_t pos = 0;
    if (!csv) csv = "";
    const char *start = csv;
    while (*start) {
        const char *end = strchr(start, ','); if (!end) end = start + strlen(start);
        const char *next = *end ? end + 1 : end;
        while (start < end && (*start == ' ' || *start == '\t')) start++;
        while (end > start && (end[-1] == ' ' || end[-1] == '\t')) end--;
        size_t length = (size_t)(end - start);
        for (const char *p = start; p < end; p++) if ((unsigned char)*p < 32 || *p == 127) return -1;
        if (length) {
            if (length + 1 >= capacity - pos) return -1;
            memcpy(out + pos, start, length); pos += length; out[pos++] = '\n';
        }
        start = next;
    }
    out[pos] = 0; return 0;
}
static int save_csv(const char *path, const char *csv) {
    char lines[LIST_CAPACITY];
    if (csv_lines(csv, lines, sizeof(lines)) || ensure_dir_recursive(BASE_DATA_DIR)) return -1;
    return write_file_text(path, lines, strlen(lines));
}

/* [CN-FORK] v0.5.2.154 / v0.5.2.155 AUTOLOAD_LIST 专用写回。
 *
 * 上游 v0.6.0 用通用 save_csv 写回（一个 token 一行、不补任何东西）。本 fork 保留
 * 自己那套「旧文件里从没出现过的载荷名，前面补一条 `!1000`」的兜底 —— 那是 .60
 * 以来的格式约定，新增条目自带 1 秒间隔。
 *
 * 用户口径（原文）：「自动加载页面，自己多次增加 1 秒延时」。这个坑有两半：
 *   ① `.154` 修的：`/get_config` 把 autoload.txt 的**每一行**（含 `!1000`）逗号拼接
 *      回传，旧写回逻辑给**每个** token 都补一行默认延时 → token `!1000` 自己被补成
 *      两行，一次保存行数翻倍。
 *   ② `.155` 修的（真机实测残留）：只按「上一条是不是延时行」判断，于是**两个载荷名
 *      相邻**时仍会补一条。真机复现：写 `!1000,kstuff,shadowmountplus` 读回
 *      `!1000,kstuff,!1000,shadowmountplus`。
 *
 * 现在只给「旧文件里从没出现过的载荷名」补延时行 —— 那种名字只可能来自「用户刚从
 * 可用列表里加进来」，它前面天然没有延时；已存在于旧文件的载荷名一律不补，用户删了
 * 就是删了、挪了就是挪了。于是列表与文件严格一一对应，来回存多少次都不变。
 *
 * 缓冲区一律 static：本函数在 config_mutex 保护下调用，且真机连接线程栈只有 64KB
 * （见 tools/check_stack_frames.sh 那道静态守卫）。 */
static int save_autoload_csv(const char *csv) {
    static char lines[LIST_CAPACITY];
    static char out[LIST_CAPACITY * 2];
    static char old_names[100][256];

    if (csv_lines(csv, lines, sizeof(lines))) return -1;

    int old_cnt = 0;
    char *old = NULL; size_t old_size = 0;
    if (read_file_text(AUTOLOAD_CONFIG_PATH, &old, &old_size) == 0 && old) {
        char *save = NULL;
        for (char *line = strtok_r(old, "\r\n", &save); line && old_cnt < 100;
             line = strtok_r(NULL, "\r\n", &save)) {
            pldmgr_utils_strip_config_line(line);
            if (!line[0] || line[0] == '!') continue;
            strncpy(old_names[old_cnt], line, sizeof(old_names[0]) - 1);
            old_names[old_cnt][sizeof(old_names[0]) - 1] = '\0';
            old_cnt++;
        }
    }
    free(old);

    size_t pos = 0;
    int prev_is_delay = 0;
    char *save = NULL;
    for (char *token = strtok_r(lines, "\n", &save); token; token = strtok_r(NULL, "\n", &save)) {
        pldmgr_utils_strip_config_line(token);
        if (!token[0]) continue; /* 空 token 不产生空行 */
        if (token[0] == '!') {
            prev_is_delay = 1;
        } else {
            if (!prev_is_delay) {
                int known = 0;
                for (int i = 0; i < old_cnt; i++)
                    if (!strcmp(old_names[i], token)) { known = 1; break; }
                if (!known) {
                    if (pos + 7 >= sizeof(out)) return -1;
                    memcpy(out + pos, "!1000\n", 6);
                    pos += 6;
                }
            }
            prev_is_delay = 0;
        }
        size_t n = strlen(token);
        if (pos + n + 2 >= sizeof(out)) return -1;
        memcpy(out + pos, token, n);
        pos += n;
        out[pos++] = '\n';
    }
    if (ensure_dir_recursive(BASE_DATA_DIR)) return -1;
    return write_file_text(AUTOLOAD_CONFIG_PATH, out, pos);
}
typedef struct {
    unsigned seen;
    int values[SETTING_COUNT];
    char autoload[LIST_CAPACITY], favorites[LIST_CAPACITY];
} Update;
static int favorite_item(const char *start, const char *end, void *context) {
    Update *update = context;
    char item[LIST_CAPACITY];
    if (json_decode_string(start, end, item, sizeof(item)) || strchr(item, ',') || strpbrk(item, "\r\n")) return -1;
    size_t used = strlen(update->favorites), count = strlen(item);
    if (used + count + (used && count ? 1 : 0) >= sizeof(update->favorites)) return -1;
    if (count) { if (used) update->favorites[used++] = ','; memcpy(update->favorites + used, item, count + 1); }
    return 0;
}
static int update_member(const char *key, const char *start, const char *end, void *context) {
    Update *update = context;
    int index = setting_lookup(key);
    if (index < 0) {
        if (!strcmp(key, "AUTOLOAD_LIST")) index = IDX_AUTOLOAD_LIST;
        else if (!strcmp(key, "FAVORITES")) index = IDX_FAVORITES;
        else return 0; /* read-only/unknown fields（含 fork 的 ARCHIVE_GLOBAL_CONFIG，单独处理） */
    }
    unsigned bit = 1u << index;
    if (update->seen & bit) return -1;
    update->seen |= bit;
    size_t size = (size_t)(end - start);
    if (index < SETTING_COUNT) {
        const SettingKey *setting = &setting_keys[index];
        long value;
        if (setting->kind == SET_BOOL) {
            if (size == 4 && !memcmp(start, "true", 4)) value = 1;
            else if (size == 5 && !memcmp(start, "false", 5)) value = 0;
            else return -1;
        } else {
            if (!size || size > 5) return -1;
            value = 0;
            for (const char *p = start; p < end; p++) {
                if (*p < '0' || *p > '9') return -1;
                value = value * 10 + *p - '0';
            }
            if (value < setting->min || value > setting->max) return -1;
        }
        update->values[index] = (int)value;
    } else {
        char *csv = index == IDX_AUTOLOAD_LIST ? update->autoload : update->favorites;
        if (index == IDX_FAVORITES && *start == '[') {
            if (json_visit_array(start, end, favorite_item, update)) return -1;
        } else if (json_decode_string(start, end, csv, LIST_CAPACITY)) return -1;
        char lines[LIST_CAPACITY];
        if (csv_lines(csv, lines, sizeof(lines))) return -1;
    }
    return 0;
}
int config_handle_set_json(const char *json) {
    Update update = {0};
    if (!json || json_visit_object(json, json + strlen(json), update_member, &update)) return CONFIG_INVALID;
    pthread_mutex_lock(&config_mutex);
    PldmgrConfig cfg;
    int result = read_config(&cfg);
    for (int i = 0; i < SETTING_COUNT; i++)
        if (update.seen & (1u << i))
            *(int *)((char *)&cfg + setting_keys[i].offset) = update.values[i];
    /* Validate the entire request before any write. Individual files publish
     * atomically; on I/O failure prior successful file publications remain.
     * Lists go first, enabled/settings last: never enable an unsaved new list. */
    if (!result && (update.seen & (1u << IDX_AUTOLOAD_LIST))) result = save_autoload_csv(update.autoload);
    if (!result && (update.seen & (1u << IDX_FAVORITES))) result = save_csv(FAVORITES_CONFIG_PATH, update.favorites);
    if (!result && (update.seen & SETTING_MASK)) result = write_config(&cfg);
    pthread_mutex_unlock(&config_mutex);
    if (!result && (update.seen & 1u) && !update.values[0]) pldmgr_autoload_abort();
    if (!result) config_apply_archive_global(json);
    /* 多媒体存档配置与存档那份互不影响，所以**无论上面的开关改没改**都要扫一遍
     * —— 弹窗保存时 body 里往往只有 MEDIA_ARCHIVE_CONFIG 这一个键。 */
    if (!result) config_apply_media_archive(json);
    return result;
}

int config_get_favorites_csv(char *out_buf, size_t max_size, int *out_file_exists) {
    if (!out_buf || !max_size) return -1;
    out_buf[0] = '\0';
    if (out_file_exists) *out_file_exists = 0;
    FILE *f = fopen(FAVORITES_CONFIG_PATH, "r");
    if (!f) return errno == ENOENT ? 0 : -1;
    if (out_file_exists) *out_file_exists = 1;
    char *line = malloc(max_size);
    if (!line) { fclose(f); return -1; }
    size_t length = 0, written = 0;
    int first = 1;
    for (;;) {
        int c = fgetc(f);
        if (c == 0 || (c == EOF && ferror(f))) goto failed;
        if (c == EOF || c == '\r' || c == '\n') {
            line[length] = '\0';
            char *start = line;
            while (*start && isspace((unsigned char)*start)) start++;
            char *end = line + length;
            while (end > start && isspace((unsigned char)end[-1])) end--;
            size_t count = (size_t)(end - start);
            if (count) {
                if (!first) {
                    if (written + 1 >= max_size) goto failed;
                    out_buf[written++] = ',';
                }
                if (count >= max_size - written) goto failed;
                memcpy(out_buf + written, start, count);
                written += count;
                out_buf[written] = '\0';
                first = 0;
            }
            length = 0;
            if (c == EOF) break;
        } else {
            if (length + 1 >= max_size) goto failed;
            line[length++] = (char)c;
        }
    }
    free(line);
    if (fclose(f) == 0) return 0;
    out_buf[0] = '\0';
    return -1;
failed:
    free(line);
    fclose(f);
    out_buf[0] = '\0';
    return -1;
}


int config_save_favorites_csv(const char *csv) {
    pthread_mutex_lock(&config_mutex);
    int result = save_csv(FAVORITES_CONFIG_PATH, csv);
    pthread_mutex_unlock(&config_mutex); return result;
}
int config_update_list_entry(const char *path, const char *old_key, const char *new_key) {
    if (!path || !old_key || !old_key[0] || strpbrk(old_key, ",\r\n") ||
        strlen(old_key) >= LIST_CAPACITY ||
        (new_key && (strlen(new_key) >= LIST_CAPACITY || strpbrk(new_key, ",\r\n")))) return -1;
    pthread_mutex_lock(&config_mutex);
    char *old = NULL; size_t size = 0; int result = -1;
    if (read_file_text(path, &old, &size)) { result = errno == ENOENT ? 0 : -1; goto done; }
    if (size > CONFIG_LIMIT || memchr(old, 0, size)) goto done;
    size_t capacity = size + 1;
    /* Each matching line may grow. Bound allocation even for legacy big files. */
    if (new_key) capacity += (size + 1) * (strlen(new_key) + 1);
    if (capacity > 16 * 1024 * 1024) goto done;
    char *text = malloc(capacity);
    if (!text) goto done;
    size_t pos = 0; int changed = 0; char *save = NULL;
    for (char *line = strtok_r(old, "\r\n", &save); line; line = strtok_r(NULL, "\r\n", &save)) {
        while (*line && isspace((unsigned char)*line)) line++;
        char *end = line + strlen(line);
        while (end > line && isspace((unsigned char)end[-1])) *--end = 0;
        if (!strcmp(line, old_key)) { changed = 1; line = (char *)new_key; }
        if (line && *line) { size_t n = strlen(line); memcpy(text + pos, line, n); pos += n; text[pos++] = '\n'; }
    }
    result = changed ? (pos >= LIST_CAPACITY ? -1 : write_file_text(path, text, pos)) : 0;
    free(text);
done:
    free(old); pthread_mutex_unlock(&config_mutex); return result;
}
int pldmgr_favorites_update_entry(const char *old_key, const char *new_key) {
    int result = config_update_list_entry(FAVORITES_CONFIG_PATH, old_key, new_key);
    if (result) pldmgr_log("[Favorites] Failed to update entry: %s\n", old_key ? old_key : "");
    return result;
}
