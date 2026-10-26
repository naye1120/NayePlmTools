#ifndef CONFIG_H
#define CONFIG_H

#include <stddef.h>

/* Consolidated configuration state for pldmgr_config.txt */
typedef struct PldmgrConfig {
    int  autoload_enabled;       /* Default: 0  (off) */
    long last_repository_update; /* Default: 0 */
    int  auto_browser_open;      /* Default: 1  (on) */
    int  autoload_return_to_dashboard; /* Default: 0 (off) */
    int  autoload_delay;         /* Default: 5  (seconds) */
    /* [CN-FORK] BD-JB 用户开机杀光盘播放器。上游 v0.6.0 已整体删除该功能，
     * 本 fork 的设置页仍有开关、main.c 仍在用，故保留（见 CHANGELOG .176）。 */
    int  kill_disc_player;       /* Default: 1  (on) */
    int  scan_usb_payloads;      /* Default: 0  (off) */
    int  auto_install_app;       /* Default: 1  (on) */
    int  show_dashboard_icons;   /* Default: 1  (on) */
    int  multi_sources_enabled;  /* Default: 0  (off) */
    /* [CN-FORK] v0.5.1.65 内置默认云端仓库（Official Repository / REPOSITORY_SOURCE_URL）总开关 */
    int  default_repo_enabled;   /* Default: 1  (on)  */
    /* [CN-FORK] 风扇温控（模型 A）：是否启用自定义目标温度 + 目标温度(°C，安全区间 60~85) */
    int  fan_enabled;            /* Default: 0  (off)  开关：只要不手动关就一直启用 */
    int  fan_target;             /* Default: 72 (°C)   上次「应用」下发的目标温度 */
    int  fan_applied;            /* Default: 0  (off)  是否已点「应用」真正生效（开启后默认失效） */
    int  fan_interval;           /* Default: 7  (秒)   守护重发周期（夹 5~3600） */
} PldmgrConfig;

/* Read all config values from PLDMGR_CONFIG_PATH.
 * Missing file / keys → defaults above. */
void config_read(PldmgrConfig *cfg);

/* Write all config values to PLDMGR_CONFIG_PATH.
 * Returns 0 on success, -1 on failure. */
int config_write(const PldmgrConfig *cfg);

/* Read a single boolean (0/1) key from the config file.
 * Returns default_val if the key or file is missing. */
int config_read_bool(const char *key, int default_val);

enum { CONFIG_INVALID = -2 };

/* Apply a JSON config update sent from the frontend /set_config route.
 * Handles AUTOLOAD_ENABLED, AUTO_BROWSER_OPEN, AUTOLOAD_DELAY,
 * KILL_DISC_PLAYER_ON_STARTUP, SCAN_USB_PAYLOADS, AUTO_INSTALL_APP,
 * MULTI_SOURCES_ENABLED, DEFAULT_REPO_ENABLED, SHOW_DASHBOARD_ICONS,
 * AUTOLOAD_RETURN_TO_DASHBOARD, and AUTOLOAD_LIST.
 * Booleans must be JSON booleans; delay is an integer from 0 to 3600 seconds.
 * Returns 0, CONFIG_INVALID for bad input, or -1 for persistence failure.
 * All input is validated first; each file publishes atomically, but a multi-file
 * I/O failure may retain earlier publications. */
int config_handle_set_json(const char *json_data);

/* Upsert a single key=value line in the config file.
 * Returns 0 on success, -1 on failure. */
int config_upsert_value(const char *key, const char *value);

/* Read the LAST_REPOSITORY_UPDATE timestamp from the config file.
 * Sets *out_ts to 0 if missing; returns -1 on read failure. */
int config_read_last_update(long *out_ts);

/* Write the LAST_REPOSITORY_UPDATE timestamp to the config file. */
int config_write_last_update(long ts);

/* [CN-FORK] v0.5.2.145 全局存档配置（设置页「存档配置」）。
 *
 * 存的是前端那份完整 cfg 的 JSON 原文（远程位置 + 自动存档开关/时机 + 本地快照参数）。
 * 单独一份文件（ARCHIVE_GLOBAL_PATH），不走 nayeplmtools_config.txt —— 理由见 pldmgr.h。
 *
 * 语义是「**未单独配置过的游戏的默认值**」：引擎（game_auto.c / game_saves.c）读配置时
 * 先看这份全局，有就用它，没有才回落到这个游戏自己的 per-game 配置，再没有才用内置默认。
 * 这样老装机（全局还没写过）行为完全不变，新装机设一次就全局生效。 */
int config_write_archive_global(const char *json);
/* 读全局存档配置原文。成功返回字节数（>0），文件不存在 / 读失败返回 -1。 */
int config_read_archive_global(char *out, size_t osz);

/* [CN-FORK] v0.6.0.177 多媒体存档配置（设置页「多媒体存档配置」弹窗）。
 * 与上面的全局存档配置**平级、互不相干**：这份是媒体自己的远程位置 + 自动备份开关，
 * 存那份是存档的。两份都存「前端 cfg 的 JSON 原文」，都不做校验、不落 nayeplmtools_config.txt
 * （行式文件 256 字节的行缓冲会把 JSON 截断成半个，理由见 pldmgr.h 的 MEDIA_ARCHIVE_PATH）。 */
int config_write_media_archive(const char *json);
/* 读多媒体存档配置原文。成功返回字节数（>0），文件不存在 / 读失败返回 -1。 */
int config_read_media_archive(char *out, size_t osz);
/* Serialized, atomic replacement/removal of matching list entries. */
int config_update_list_entry(const char *path, const char *old_key, const char *new_key);

/* Favorites management */
int config_get_favorites_csv(char *out_buf, size_t max_size, int *out_file_exists);
int config_save_favorites_csv(const char *csv);
int pldmgr_favorites_update_entry(const char *old_key, const char *new_key);

#endif
