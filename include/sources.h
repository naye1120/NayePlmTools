#ifndef SOURCES_H
#define SOURCES_H

#include <stddef.h>

/* [CN-FORK] v0.5.2.67 首次运行时预置默认来源（内置官方源默认关 + 启用 repo.630810.xyz）。
 * 仅当 sources.json 不存在时写一次，由 main.c 在开机序列早期调用。 */
void sources_seed_defaults(void);

/* List all configured sources as complete JSON. Returns 0, or -1 on failure. */
int sources_list_json(char *buf, size_t size);

/* Replace the sources list from a JSON POST body. */
int sources_save(const char *json, size_t len);

/* Add a new source by URL. On success, msg_buf receives the source name. */
int sources_add(const char *url, char *msg_buf, size_t msg_size);

/* Remove a source by index. Index 0 (default) cannot be removed. */
int sources_remove(int index, char *msg_buf, size_t msg_size);

/* Ensure a source repository cache is fresh by URL */
int sources_ensure_fresh(const char *url, int force_refresh);

/* Build multi-source repository JSON (grouped by source).
 * Returns complete bytes written, or 0 on serialization failure. */
size_t sources_multi_repository_list_json(char *buf, size_t size, int force_refresh);

/* Cached icon catalog for a configured source ID; no remote URL accepted. */
size_t sources_icons_json(const char *source_id, char *buf, size_t size);
int sources_clear_cache(void);

/* Unified repository install pipeline.
 * Finds entry by id in source cache (refreshing if missing or on error).
 * Handles conflicts, mode=replace|keep_both, and downloads to slot/.part before commit. */
int sources_repo_install(const char *source_url, const char *id,
                         const char *mode, const char *replace_slot,
                         char *out_json, size_t out_size);

#endif
