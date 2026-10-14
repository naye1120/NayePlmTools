#ifndef REPOSITORY_H
#define REPOSITORY_H

#include <stddef.h>

/* Payload entry from a repository JSON */
typedef struct RepoPayload {
    char id[128];
    char name[128];
    char filename[256];
    char url[1024];
    char source[1024];
    char source_direct[1024];
    char description[1024];
    char last_update[64];
    char version[64];
    char checksum[65];
    char source_name[256];
    char category[128];
    char icon_key[128];
    char release_notes[2048];
    char author[128];
    char details_url[1024];
} RepoPayload;

#define PLDMGR_PAYLOAD_DOWNLOAD_LIMIT ((size_t)512 * 1024 * 1024)
#define PLDMGR_MANIFEST_DOWNLOAD_LIMIT ((size_t)16 * 1024 * 1024)
int repository_checksum_valid(const char *checksum);
int repository_entry_valid(const RepoPayload *entry);
int repository_self_update_entry_valid(const RepoPayload *entry, const char *source_url);
/* Version reads preserve the descriptor offset; only versioned ELF files qualify. */
int repository_self_update_version_fd(int fd, char *version, size_t size);
/* Returns 1 for a strictly newer update, 0 when none is available. */
int repository_check_self_update(char *out_path, size_t size, char *version, size_t version_size);
int repository_install_self_update(const RepoPayload *entry, const char *source_url, const char *part_path);
int download_to_file_limited(const char *url, const char *out_path, size_t limit);

/* Download a URL to a local file. Returns 0 on success, -1 on failure. */
int download_to_file(const char *url, const char *out_path);

/* Parse a JSON string containing payload objects into a RepoPayload array.
 * Caller must free *out_items. Returns 0 on success, -1 on failure. */
int parse_repository_payloads(const char *json, RepoPayload **out_items, size_t *out_count);

/* ── Repository cache management ────────────────────────── */

/* Ensure the repository cache is up-to-date. If force_refresh, always re-download. */
int repository_ensure_fresh(int force_refresh);

/* Build complete /repository_payloads JSON. Returns bytes written, or 0 on failure. */
size_t repository_list_json(char *json_buf, size_t buf_size, int force_refresh);

#endif


