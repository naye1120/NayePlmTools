#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <errno.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>
#include <pthread.h>
#include <fcntl.h>
#include <limits.h>

#include "slot.h"
#include "collection_helpers.h"
#include "sha256.h"
#include "json_helpers.h"
#include "pldmgr.h"
#include "config.h"
#include "autoload.h"
#include "utils.h"

static int is_supported_ext(const char *name) {
    const char *ext = strrchr(name, '.');
    if (!ext) return 0;
    return (strcasecmp(ext, ".elf") == 0 || strcasecmp(ext, ".bin") == 0);
}

/* Storage operations share one recursive lock. Downloads happen outside it;
 * selecting a key, preparing a slot, and publishing it are serialized. */
static pthread_once_t storage_once = PTHREAD_ONCE_INIT;
static pthread_mutex_t storage_mutex;
static int storage_recovery_required;
static int storage_mutex_error;
static void storage_mutex_init(void) {
    pthread_mutexattr_t attr;
    storage_mutex_error = pthread_mutexattr_init(&attr);
    if (storage_mutex_error) return;
    storage_mutex_error = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    if (!storage_mutex_error) storage_mutex_error = pthread_mutex_init(&storage_mutex, &attr);
    pthread_mutexattr_destroy(&attr);
}
static int storage_lock(void) {
    int rc = pthread_once(&storage_once, storage_mutex_init);
    if (rc || storage_mutex_error) return -1;
    return pthread_mutex_lock(&storage_mutex) ? -1 : 0;
}
static void storage_unlock(void) { pthread_mutex_unlock(&storage_mutex); }

int slot_mode_valid(const char *mode) {
    return !mode || !mode[0] || !strcmp(mode, "new") ||
           !strcmp(mode, "replace") || !strcmp(mode, "keep_both");
}
static int storage_leaf_valid(const char *leaf) {
    return leaf && leaf[0] && leaf[0] != '.' && !strchr(leaf, '/') &&
           !strstr(leaf, "..") && strlen(leaf) < 122;
}
static int storage_key_valid(const char *key) {
    const char *leaf = NULL;
    if (key && !strncmp(key, "local/", 6)) leaf = key + 6;
    if (key && !strncmp(key, "repo/", 5)) leaf = key + 5;
    return storage_leaf_valid(leaf);
}
/* Only private transaction directories are recursively removed. Never follow
 * links (including links left by an interrupted or externally modified job). */
static struct dirent *storage_next(DIR *d, int *rc) {
    errno = 0;
    struct dirent *ent = readdir(d);
    if (!ent && errno) *rc = -1;
    return ent;
}
static int storage_remove_tree(const char *path) {
    struct stat st;
    if (lstat(path, &st)) return errno == ENOENT ? 0 : -1;
    if (!S_ISDIR(st.st_mode)) return unlink(path);
    DIR *d = opendir(path);
    if (!d) return -1;
    int rc = 0;
    struct dirent *ent;
    while ((ent = storage_next(d, &rc))) {
        if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..")) continue;
        char child[1024];
        if (snprintf(child, sizeof(child), "%s/%s", path, ent->d_name) >= (int)sizeof(child) ||
            storage_remove_tree(child)) { rc = -1; break; }
    }
    if (closedir(d)) rc = -1;
    return rc ? rc : rmdir(path);
}
/* Some PS5 filesystems do not support syncing directory descriptors. File
 * flush/close/sync failures are always fatal; unsupported directory fsync is
 * documented as a durability limitation, not treated as a rollback. */
static int storage_sync_dir(const char *path) {
    int fd = open(path, O_RDONLY | O_DIRECTORY);
    if (fd < 0) return -1;
    int rc = fsync(fd), saved = errno;
    if (close(fd)) return -1;
    if (rc && saved != EINVAL && saved != ENOTSUP) { errno = saved; return -1; }
    return 0;
}
static int storage_write(const char *path, const char *text, size_t length) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    int rc = fwrite(text, 1, length, f) == length ? 0 : -1;
    if (fflush(f)) rc = -1;
    if (!rc && fsync(fileno(f))) rc = -1;
    if (fclose(f)) rc = -1;
    return rc;
}
/* Verify the staged bytes independently after checked copy/flush/sync/close.
 * A short read or media removal cannot turn a truncated copy into success. */
static int storage_copy(const char *source, const char *destination) {
    FILE *in = fopen(source, "rb");
    if (!in) return -1;
    struct stat st;
    if (fstat(fileno(in), &st) || !S_ISREG(st.st_mode) || st.st_size <= 0) {
        fclose(in); return -1;
    }
    FILE *out = fopen(destination, "wb");
    if (!out) { fclose(in); return -1; }
    unsigned char buf[8192], expected[32], actual[32];
    SHA256_CTX hash;
    sha256_init(&hash);
    size_t n, total = 0;
    int rc = 0;
    while ((n = fread(buf, 1, sizeof(buf), in))) {
        sha256_update(&hash, buf, n); total += n;
        if (fwrite(buf, 1, n, out) != n) { rc = -1; break; }
    }
    if (ferror(in) || total != (size_t)st.st_size) rc = -1;
    if (fclose(in)) rc = -1;
    if (fflush(out)) rc = -1;
    if (!rc && fsync(fileno(out))) rc = -1;
    if (fclose(out)) rc = -1;
    sha256_final(&hash, expected);
    if (rc) return -1;
    in = fopen(destination, "rb");
    if (!in) return -1;
    sha256_init(&hash); size_t verified = 0;
    while ((n = fread(buf, 1, sizeof(buf), in))) {
        sha256_update(&hash, buf, n); verified += n;
    }
    if (ferror(in) || verified != total) rc = -1;
    if (fclose(in)) rc = -1;
    sha256_final(&hash, actual);
    return rc || memcmp(expected, actual, sizeof(actual)) ? -1 : 0;
}

/* The journal names a validated sibling. If old exists but the public slot
 * does not, publication never happened: restore old. Otherwise the public
 * directory is the committed version. Staging and backups are then garbage.
 * Called at startup before migration, requests, or autoload begin. */
static int slot_storage_recover_impl(int clean_requests) {
    if (storage_lock()) return -1;
    int rc = 0;
    const char *kinds[] = { "local", "repo" };
    for (size_t i = 0; i < 2 && !rc; i++) {
        char parent[640];
        snprintf(parent, sizeof(parent), "%s/%s", PAYLOADS_STORAGE_DIR, kinds[i]);
        DIR *d = opendir(parent);
        if (!d) { if (errno != ENOENT) rc = -1; continue; }
        struct dirent *ent;
        while ((ent = storage_next(d, &rc))) {
            if (strncmp(ent->d_name, ".txn-", 5)) continue;
            char txn[768], journal[800], old[800], target[800], leaf[128];
            snprintf(txn, sizeof(txn), "%s/%s", parent, ent->d_name);
            snprintf(journal, sizeof(journal), "%s/target", txn);
            snprintf(old, sizeof(old), "%s/old", txn);
            struct stat st;
            if (lstat(txn, &st) || !S_ISDIR(st.st_mode)) { rc = -1; break; }
            if (!lstat(journal, &st) && !S_ISREG(st.st_mode)) { rc = -1; break; }
            FILE *f = fopen(journal, "rb");
            if (!f) {
                /* No journal means no rename of the old slot was attempted. */
                if (errno != ENOENT) { rc = -1; break; }
                if (!lstat(old, &st) || errno != ENOENT || storage_remove_tree(txn)) { rc = -1; break; }
                continue;
            }
            size_t n = fread(leaf, 1, sizeof(leaf)-1, f);
            int bad = ferror(f) || !feof(f);
            if (fclose(f)) bad = 1;
            leaf[n] = 0;
            if (bad || strlen(leaf) != n || !storage_leaf_valid(leaf)) { rc = -1; break; }
            snprintf(target, sizeof(target), "%s/%s", parent, leaf);
            if (lstat(target, &st)) {
                if (errno != ENOENT) { rc = -1; break; }
                if (!lstat(old, &st)) {
                    if (!S_ISDIR(st.st_mode) || rename(old, target) || storage_sync_dir(parent)) { rc = -1; break; }
                } else if (errno != ENOENT) { rc = -1; break; }
            }
            else if (!S_ISDIR(st.st_mode)) { rc = -1; break; }
            if (storage_remove_tree(txn)) { rc = -1; break; }
        }
        if (closedir(d)) rc = -1;
    }
    /* Startup owns no active uploads/downloads. Remove abandoned request
     * files only after recovering all slot directories. */
    if (!rc && clean_requests) {
        DIR *d = opendir(PAYLOADS_STORAGE_DIR);
        if (!d) { if (errno != ENOENT) rc = -1; }
        else {
            struct dirent *ent;
            while ((ent = storage_next(d, &rc))) {
                size_t prefix = !strncmp(ent->d_name, ".download-", 10) ? 10 :
                                !strncmp(ent->d_name, ".upload-", 8) ? 8 : 0;
                if (!prefix || strlen(ent->d_name) != prefix + 6) continue;
                char path[768]; struct stat st;
                snprintf(path, sizeof(path), "%s/%s", PAYLOADS_STORAGE_DIR, ent->d_name);
                if (lstat(path, &st) || !S_ISREG(st.st_mode) || unlink(path)) { rc = -1; break; }
            }
            if (closedir(d)) rc = -1;
        }
    }
    storage_recovery_required = rc != 0;
    storage_unlock();
    return rc;
}

int slot_storage_recover(void) { return slot_storage_recover_impl(1); }

int slot_commit_file(const char *base_key, const char *source, const char *mode,
                     SlotMeta *meta, int check_repo_conflict, char *out_key, size_t out_size) {
    if (!storage_key_valid(base_key) || !source || !meta || !out_key || out_size < 128 ||
        !slot_mode_valid(mode) || !meta->filename[0] || meta->filename[0] == '.' ||
        strchr(meta->filename, '/') || strstr(meta->filename, "..") || !is_supported_ext(meta->filename)) return -1;
    if (storage_lock()) return -1;
    if (storage_recovery_required && slot_storage_recover_impl(0)) { storage_unlock(); return -1; }
    int rc = -1, moved_old = 0;
    char key[128], target[640], parent[640], txn[768] = "", prepared[800], old[800], journal[800], payload[1024];
    if (check_repo_conflict && !strcmp(meta->kind, SLOT_KIND_REPO)) {
        SlotMeta *slots = NULL; size_t count = 0;
        if (slot_list_all(&slots, &count)) goto done;
        int conflict = 0;
        char source_hash[16];
        slot_source_hash(meta->source_url, source_hash, sizeof(source_hash));
        for (size_t i = 0; i < count; i++) {
            char other_hash[16];
            slot_source_hash(slots[i].source_url, other_hash, sizeof(other_hash));
            if (!strcmp(slots[i].kind, SLOT_KIND_REPO) && !strcmp(slots[i].id, meta->id) &&
                strcmp(slots[i].slot_key, base_key) && strcmp(source_hash, other_hash)) { conflict = 1; break; }
        }
        slot_free_all(slots);
        if (conflict) { errno = EEXIST; goto done; }
    }
    snprintf(key, sizeof(key), "%s", base_key);
    struct stat st;
    for (int suffix = 1; suffix <= 10000; suffix++) {
        snprintf(target, sizeof(target), "%s/%s", PAYLOADS_STORAGE_DIR, key);
        if (lstat(target, &st)) { if (errno != ENOENT) goto done; break; }
        if (mode && !strcmp(mode, "replace")) {
            if (!S_ISDIR(st.st_mode)) goto done;
            break;
        }
        if (!mode || strcmp(mode, "keep_both") || suffix == 10000) { errno = EEXIST; goto done; }
        if (snprintf(key, sizeof(key), "%s-%d", base_key, suffix+1) >= (int)sizeof(key)) goto done;
    }
    snprintf(parent, sizeof(parent), "%s/%.*s", PAYLOADS_STORAGE_DIR,
             (int)(strchr(key, '/')-key), key);
    if (ensure_dir_recursive(parent)) goto done;
    snprintf(txn, sizeof(txn), "%s/.txn-XXXXXX", parent);
    if (!mkdtemp(txn)) { txn[0] = 0; goto done; }
    snprintf(prepared, sizeof(prepared), "%s/new", txn);
    snprintf(old, sizeof(old), "%s/old", txn);
    snprintf(journal, sizeof(journal), "%s/target", txn);
    if (mkdir(prepared, 0700)) goto done;
    snprintf(payload, sizeof(payload), "%s/%s", prepared, meta->filename);
    if (storage_copy(source, payload)) goto done;
    snprintf(meta->slot_key, sizeof(meta->slot_key), "%s", key);
    snprintf(meta->slot_dir, sizeof(meta->slot_dir), "%s", target);
    snprintf(meta->payload_path, sizeof(meta->payload_path), "%s/%s", target, meta->filename);
    if (slot_write_meta(prepared, meta) || storage_sync_dir(prepared)) goto done;
    const char *leaf = strchr(key, '/')+1;
    if (storage_write(journal, leaf, strlen(leaf)) || storage_sync_dir(txn) || storage_sync_dir(parent)) goto done;
    if (!lstat(target, &st)) {
        if (rename(target, old)) goto done;
        moved_old = 1;
        if (storage_sync_dir(txn) || storage_sync_dir(parent)) goto rollback;
    } else if (errno != ENOENT) goto done;
    if (rename(prepared, target)) goto rollback;
    /* Publication succeeded: never remove the published directory on a late
     * cleanup failure. The retained journal can be safely cleaned at startup. */
    snprintf(out_key, out_size, "%s", key);
    rc = 0;
    if (storage_sync_dir(parent)) {
        pldmgr_log("[PLDMGR] Slot published; directory sync failed for %s\n", key);
        txn[0] = 0; goto done;
    }
    goto done;
rollback:
    if (moved_old && rename(old, target)) {
        pldmgr_log("[PLDMGR] Slot rollback needs startup recovery: %s\n", key);
        txn[0] = 0; storage_recovery_required = 1;
    } else if (moved_old) storage_sync_dir(parent);
done:
    if (txn[0] && storage_remove_tree(txn))
        pldmgr_log("[PLDMGR] Retaining storage transaction for recovery: %s\n", txn);
    storage_unlock();
    return rc;
}

int slot_source_hash(const char *source_url, char *out_hash8, size_t out_size) {
    if (!source_url || !out_hash8 || out_size < 9) return -1;

    /* Normalize: trimmed, no trailing '/' */
    const char *p = source_url;
    while (*p && isspace((unsigned char)*p)) p++;

    size_t len = strlen(p);
    while (len > 0 && isspace((unsigned char)p[len - 1])) len--;
    while (len > 0 && p[len - 1] == '/') len--;

    char norm[1024];
    if (len >= sizeof(norm)) len = sizeof(norm) - 1;
    memcpy(norm, p, len);
    norm[len] = '\0';

    SHA256_CTX ctx;
    unsigned char hash[32];
    sha256_init(&ctx);
    sha256_update(&ctx, (const unsigned char *)norm, strlen(norm));
    sha256_final(&ctx, hash);

    snprintf(out_hash8, out_size, "%02x%02x%02x%02x",
             hash[0], hash[1], hash[2], hash[3]);
    return 0;
}

int slot_slug(const char *input, char *out_slug, size_t out_size) {
    if (!input || !out_slug || out_size == 0) return -1;

    size_t out_idx = 0;
    int last_dash = 1; /* treat start as dash to avoid leading dash */

    for (size_t i = 0; input[i] != '\0' && out_idx + 1 < out_size; i++) {
        unsigned char c = (unsigned char)input[i];
        if (isalnum(c)) {
            out_slug[out_idx++] = (char)tolower(c);
            last_dash = 0;
        } else if ((c == '.' || c == '_') && out_idx > 0 && out_slug[out_idx - 1] != '.') {
            out_slug[out_idx++] = (char)c;
            last_dash = 0;
        } else {
            /* Separator */
            if (!last_dash) {
                out_slug[out_idx++] = '-';
                last_dash = 1;
            }
        }
    }

    /* Strip trailing dashes, dots, or underscores */
    while (out_idx > 0 && (out_slug[out_idx - 1] == '-' ||
                           out_slug[out_idx - 1] == '.' ||
                           out_slug[out_idx - 1] == '_')) {
        out_idx--;
    }

    out_slug[out_idx] = '\0';

    if (out_idx == 0) {
        strncpy(out_slug, "payload", out_size - 1);
        out_slug[out_size - 1] = '\0';
    }

    return 0;
}

int slot_local_key(const char *filename, char *out_key, size_t out_size) {
    if (!filename || !out_key || out_size == 0) return -1;

    const char *base = strrchr(filename, '/');
    base = base ? base + 1 : filename;

    char clean[256];
    strncpy(clean, base, sizeof(clean) - 1);
    clean[sizeof(clean) - 1] = '\0';

    /* Strip extension (.elf or .bin) */
    char *dot = strrchr(clean, '.');
    if (dot && (strcasecmp(dot, ".elf") == 0 || strcasecmp(dot, ".bin") == 0)) {
        *dot = '\0';
    }

    /* Look for version markers: [_\s\.-]v?[0-9] */
    size_t clen = strlen(clean);
    for (size_t i = 0; i < clen; i++) {
        char sep = clean[i];
        if (sep == '_' || sep == '-' || sep == '.' || sep == ' ') {
            size_t next = i + 1;
            if (next < clen && (clean[next] == 'v' || clean[next] == 'V')) {
                next++;
            }
            if (next < clen && isdigit((unsigned char)clean[next])) {
                clean[i] = '\0';
                break;
            }
        }
    }

    /* Strip trailing [-_\s\.]ps[45] */
    clen = strlen(clean);
    if (clen >= 4) {
        char sep = clean[clen - 4];
        if (sep == '-' || sep == '_' || sep == '.' || sep == ' ') {
            if ((clean[clen - 3] == 'p' || clean[clen - 3] == 'P') &&
                (clean[clen - 2] == 's' || clean[clen - 2] == 'S') &&
                (clean[clen - 1] == '4' || clean[clen - 1] == '5')) {
                clean[clen - 4] = '\0';
            }
        }
    }

    /* If clean ended up empty, use base name */
    if (clean[0] == '\0') {
        strncpy(clean, base, sizeof(clean) - 1);
        clean[sizeof(clean) - 1] = '\0';
        dot = strrchr(clean, '.');
        if (dot) *dot = '\0';
    }

    return slot_slug(clean, out_key, out_size);
}

int slot_repo_key(const char *id, const char *source_url, char *out_slot, size_t out_size) {
    if (!id || !source_url || !out_slot || out_size == 0) return -1;

    char safe_id[128];
    slot_slug(id, safe_id, sizeof(safe_id));

    char hash8[16];
    if (slot_source_hash(source_url, hash8, sizeof(hash8)) != 0) return -1;

    snprintf(out_slot, out_size, "repo/%s@%s", safe_id, hash8);
    return 0;
}

int slot_find_next_local_key(const char *base_key, char *out_slot, size_t out_size) {
    if (!base_key || !out_slot || out_size == 0) return -1;

    char safe_key[128];
    slot_slug(base_key, safe_key, sizeof(safe_key));

    char check_path[640];
    snprintf(check_path, sizeof(check_path), "%s/local/%s", PAYLOADS_STORAGE_DIR, safe_key);
    if (access(check_path, F_OK) != 0) {
        snprintf(out_slot, out_size, "local/%s", safe_key);
        return 0;
    }

    for (int i = 2; i <= 1000; i++) {
        snprintf(check_path, sizeof(check_path), "%s/local/%s-%d", PAYLOADS_STORAGE_DIR, safe_key, i);
        if (access(check_path, F_OK) != 0) {
            snprintf(out_slot, out_size, "local/%s-%d", safe_key, i);
            return 0;
        }
    }

    snprintf(out_slot, out_size, "local/%s-new", safe_key);
    return 0;
}

static int slot_read_meta_unlocked(const char *slot_dir, SlotMeta *out_meta) {
    if (!slot_dir || !out_meta) return -1;
    memset(out_meta, 0, sizeof(SlotMeta));

    strncpy(out_meta->slot_dir, slot_dir, sizeof(out_meta->slot_dir) - 1);

    /* Derive slot_key from slot_dir (relative to PAYLOADS_STORAGE_DIR) */
    const char *rel = slot_dir;
    size_t base_len = strlen(PAYLOADS_STORAGE_DIR);
    if (strncmp(slot_dir, PAYLOADS_STORAGE_DIR, base_len) == 0) {
        rel = slot_dir + base_len;
        if (*rel == '/') rel++;
    }
    strncpy(out_meta->slot_key, rel, sizeof(out_meta->slot_key) - 1);

    char meta_path[640];
    snprintf(meta_path, sizeof(meta_path), "%s/meta.json", slot_dir);

    char *json = NULL;
    size_t size = 0;
    int read_result = read_file_text(meta_path, &json, &size);
    if (read_result != 0 && errno != ENOENT) return -1;
    if (read_result == 0 && json && size > 0) {
        const char *end = json + size;
        char schema_str[16] = "";
        json_extract_string(json, end, "schema", schema_str, sizeof(schema_str));
        out_meta->schema = atoi(schema_str);
        if (out_meta->schema == 0) out_meta->schema = 2;

        json_extract_string(json, end, "kind", out_meta->kind, sizeof(out_meta->kind));
        json_extract_string(json, end, "filename", out_meta->filename, sizeof(out_meta->filename));
        json_extract_string(json, end, "installed_at", out_meta->installed_at, sizeof(out_meta->installed_at));

        /* Repo fields */
        json_extract_string(json, end, "id", out_meta->id, sizeof(out_meta->id));
        json_extract_string(json, end, "source_url", out_meta->source_url, sizeof(out_meta->source_url));
        json_extract_string(json, end, "source_name", out_meta->source_name, sizeof(out_meta->source_name));
        if (out_meta->source_name[0] == '\0' && out_meta->source_url[0] != '\0') {
            if (strcmp(out_meta->source_url, REPOSITORY_SOURCE_URL) == 0 ||
                is_legacy_official_repo_url(out_meta->source_url)) {
                strncpy(out_meta->source_name, "Official Repository", sizeof(out_meta->source_name) - 1);
            }
        }
        json_extract_string(json, end, "name", out_meta->name, sizeof(out_meta->name));
        json_extract_string(json, end, "version", out_meta->version, sizeof(out_meta->version));
        json_extract_string(json, end, "checksum", out_meta->checksum, sizeof(out_meta->checksum));
        json_extract_string(json, end, "last_update", out_meta->last_update, sizeof(out_meta->last_update));
        json_extract_string(json, end, "url", out_meta->url, sizeof(out_meta->url));
        json_extract_string(json, end, "source", out_meta->source, sizeof(out_meta->source));
        json_extract_string(json, end, "source_direct", out_meta->source_direct, sizeof(out_meta->source_direct));
        json_extract_string(json, end, "category", out_meta->category, sizeof(out_meta->category));
        json_extract_string_prefix(json, end, "description", out_meta->description, sizeof(out_meta->description));
        json_extract_string(json, end, "author", out_meta->author, sizeof(out_meta->author));
        json_extract_string(json, end, "details_url", out_meta->details_url, sizeof(out_meta->details_url));

        /* Local fields */
        json_extract_string(json, end, "origin", out_meta->origin, sizeof(out_meta->origin));
        json_extract_string(json, end, "origin_detail", out_meta->origin_detail, sizeof(out_meta->origin_detail));

        free(json);
    } else {
        if (json) free(json);
        /* If meta.json is absent, detect kind from slot_key prefix */
        if (strncmp(out_meta->slot_key, "repo/", 5) == 0) {
            strcpy(out_meta->kind, SLOT_KIND_REPO);
        } else {
            strcpy(out_meta->kind, SLOT_KIND_LOCAL);
            strcpy(out_meta->origin, "web_upload");
        }
        out_meta->schema = PAYLOAD_SLOT_SCHEMA;
    }

    /* Find actual payload file in slot_dir */
    if (out_meta->filename[0] != '\0') {
        char test_path[640];
        snprintf(test_path, sizeof(test_path), "%s/%s", slot_dir, out_meta->filename);
        if (access(test_path, F_OK) == 0) {
            strncpy(out_meta->payload_path, test_path, sizeof(out_meta->payload_path) - 1);
            out_meta->payload_path[sizeof(out_meta->payload_path) - 1] = '\0';
        }
    }

    if (out_meta->payload_path[0] == '\0') {
        DIR *d = opendir(slot_dir);
        if (d) {
            struct dirent *ent;
            while ((ent = readdir(d)) != NULL) {
                if (ent->d_name[0] == '.') continue;
                if (strcmp(ent->d_name, "meta.json") == 0) continue;
                if (is_supported_ext(ent->d_name)) {
                    snprintf(out_meta->payload_path, sizeof(out_meta->payload_path),
                             "%s/%s", slot_dir, ent->d_name);
                    if (out_meta->filename[0] == '\0') {
                        strncpy(out_meta->filename, ent->d_name, sizeof(out_meta->filename) - 1);
                    }
                    break;
                }
            }
            closedir(d);
        }
    }

    if (out_meta->payload_path[0] == '\0' && out_meta->filename[0] != '\0') {
        snprintf(out_meta->payload_path, sizeof(out_meta->payload_path),
                 "%s/%s", slot_dir, out_meta->filename);
    }

    return 0;
}

/* [CN-FORK] v0.6.0.176 栈帧红线：slot_write_meta_unlocked 原来在栈上放 buf[65536]
 * 加一整套 6 倍最坏转义缓冲（合计 ~108KB；-Os 把 impl 内联进 slot_write_meta 后
 * 整帧 111064 B）。真机 MHD 每条连接一条 pthread、栈只有 64KB，必 SIGSEGV。
 * 整块搬堆上：调用方 malloc 一次、用完即释（方向同 game_saves.c 的 gs_cp_t）。 */
typedef struct {
    char buf[65536];
    char fn_e[6 * 256], at_e[6 * 64];
    char kind_e[6 * 16];
    char id_e[6 * 128], surl_e[6 * 1024], sname_e[6 * 256], name_e[6 * 256];
    char ver_e[6 * 64], csum_e[6 * 65], lup_e[6 * 64], url_e[6 * 1024];
    char src_e[6 * 1024], sd_e[6 * 1024], cat_e[6 * 128], desc_e[6 * 1024];
    char auth_e[6 * 128], det_e[6 * 1024];
    char org_e[6 * 64], od_e[6 * 1024];
} SlotMetaJsonScratch;

static int slot_write_meta_impl(const char *slot_dir, const SlotMeta *meta,
                               SlotMetaJsonScratch *sc) {
    if (!slot_dir || !meta) return -1;

    JsonListBuilder jb = { sc->buf, sizeof(sc->buf), 0, 1, 0 };

    json_escape_checked(&jb, meta->kind[0] ? meta->kind : SLOT_KIND_LOCAL,
                        sc->kind_e, sizeof(sc->kind_e));
    json_escape_checked(&jb, meta->filename, sc->fn_e, sizeof(sc->fn_e));
    json_escape_checked(&jb, meta->installed_at, sc->at_e, sizeof(sc->at_e));

    json_append(&jb, "{\n"
                     "  \"schema\": %d,\n"
                     "  \"kind\": \"%s\",\n"
                     "  \"filename\": \"%s\",\n"
                     "  \"installed_at\": \"%s\"",
                meta->schema ? meta->schema : PAYLOAD_SLOT_SCHEMA,
                sc->kind_e,
                sc->fn_e, sc->at_e);

    if (strcmp(meta->kind, SLOT_KIND_REPO) == 0) {

        json_escape_checked(&jb, meta->id, sc->id_e, sizeof(sc->id_e));
        json_escape_checked(&jb, meta->source_url, sc->surl_e, sizeof(sc->surl_e));
        json_escape_checked(&jb, meta->source_name, sc->sname_e, sizeof(sc->sname_e));
        json_escape_checked(&jb, meta->name, sc->name_e, sizeof(sc->name_e));
        json_escape_checked(&jb, meta->version, sc->ver_e, sizeof(sc->ver_e));
        json_escape_checked(&jb, meta->checksum, sc->csum_e, sizeof(sc->csum_e));
        json_escape_checked(&jb, meta->last_update, sc->lup_e, sizeof(sc->lup_e));
        json_escape_checked(&jb, meta->url, sc->url_e, sizeof(sc->url_e));
        json_escape_checked(&jb, meta->source, sc->src_e, sizeof(sc->src_e));
        json_escape_checked(&jb, meta->source_direct, sc->sd_e, sizeof(sc->sd_e));
        json_escape_checked(&jb, meta->category, sc->cat_e, sizeof(sc->cat_e));
        json_escape_checked(&jb, meta->description, sc->desc_e, sizeof(sc->desc_e));
        json_escape_checked(&jb, meta->author, sc->auth_e, sizeof(sc->auth_e));
        json_escape_checked(&jb, meta->details_url, sc->det_e, sizeof(sc->det_e));

        json_append(&jb, ",\n"
                         "  \"id\": \"%s\",\n"
                         "  \"source_url\": \"%s\",\n"
                         "  \"source_name\": \"%s\",\n"
                         "  \"name\": \"%s\",\n"
                         "  \"version\": \"%s\",\n"
                         "  \"checksum\": \"%s\",\n"
                         "  \"last_update\": \"%s\",\n"
                         "  \"url\": \"%s\",\n"
                         "  \"source\": \"%s\",\n"
                         "  \"source_direct\": \"%s\",\n"
                         "  \"category\": \"%s\",\n"
                         "  \"description\": \"%s\",\n"
                         "  \"author\": \"%s\",\n"
                         "  \"details_url\": \"%s\"",
                    sc->id_e, sc->surl_e, sc->sname_e, sc->name_e, sc->ver_e,
                    sc->csum_e, sc->lup_e, sc->url_e, sc->src_e, sc->sd_e,
                    sc->cat_e, sc->desc_e, sc->auth_e, sc->det_e);
    } else {
        json_escape_checked(&jb, meta->origin, sc->org_e, sizeof(sc->org_e));
        json_escape_checked(&jb, meta->origin_detail, sc->od_e, sizeof(sc->od_e));

        json_append(&jb, ",\n"
                         "  \"origin\": \"%s\",\n"
                         "  \"origin_detail\": \"%s\"",
                    sc->org_e, sc->od_e);
    }

    json_append(&jb, "\n}\n");

    if (jb.error) return -1;

    char tmp_path[640], final_path[640];
    snprintf(tmp_path, sizeof(tmp_path), "%s/.meta-XXXXXX", slot_dir);
    snprintf(final_path, sizeof(final_path), "%s/meta.json", slot_dir);

    int fd = mkstemp(tmp_path);
    if (fd < 0) return -1;
    if (close(fd) || storage_write(tmp_path, sc->buf, jb.pos)) {
        remove(tmp_path); return -1;
    }
    if (rename(tmp_path, final_path)) { remove(tmp_path); return -1; }
    return 0;
}

static int slot_write_meta_unlocked(const char *slot_dir, const SlotMeta *meta) {
    if (!slot_dir || !meta) return -1;
    SlotMetaJsonScratch *sc = malloc(sizeof(*sc));
    if (!sc) return -1;
    int rc = slot_write_meta_impl(slot_dir, meta, sc);
    free(sc);
    return rc;
}
static int scan_slot_subdir(const char *parent_dir, SlotMeta **out_slots, size_t *out_count, size_t *capacity) {
    DIR *d = opendir(parent_dir);
    if (!d) return errno == ENOENT ? 0 : -1;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;

        char sub_path[512];
        snprintf(sub_path, sizeof(sub_path), "%s/%s", parent_dir, ent->d_name);

        struct stat st;
        if (stat(sub_path, &st) != 0 || !S_ISDIR(st.st_mode)) continue;

        SlotMeta meta;
        if (slot_read_meta(sub_path, &meta) != 0) { closedir(d); return -1; }
        if (meta.payload_path[0] != '\0') {
            SlotMeta *tmp = collection_reserve(*out_slots, capacity, *out_count + 1, sizeof(SlotMeta));
            if (!tmp) { closedir(d); return -1; }
            *out_slots = tmp;
            (*out_slots)[*out_count] = meta;
            (*out_count)++;
        }
    }
    closedir(d);
    return 0;
}

static int slot_list_all_unlocked(SlotMeta **out_slots, size_t *out_count) {
    if (!out_slots || !out_count) return -1;
    *out_slots = NULL;
    *out_count = 0;

    char repo_dir[512], local_dir[512];
    snprintf(repo_dir, sizeof(repo_dir), "%s/repo", PAYLOADS_STORAGE_DIR);
    snprintf(local_dir, sizeof(local_dir), "%s/local", PAYLOADS_STORAGE_DIR);

    size_t capacity = 0;
    if (scan_slot_subdir(repo_dir, out_slots, out_count, &capacity) != 0 ||
        scan_slot_subdir(local_dir, out_slots, out_count, &capacity) != 0) {
        free(*out_slots);
        *out_slots = NULL;
        *out_count = 0;
        return -1;
    }

    return 0;
}

void slot_free_all(SlotMeta *slots) {
    if (slots) free(slots);
}

#ifndef PLDMGR_USB_ROOT_FORMAT
#define PLDMGR_USB_ROOT_FORMAT "/mnt/usb%d"
#endif
static int canonical_under(const char *path, const char *root) {
    char resolved[PATH_MAX];
    if (!realpath(root, resolved)) return 0;
    size_t n = strlen(resolved);
    return !strncmp(path, resolved, n) && path[n] == '/';
}
static int canonical_usb_path(const char *path, const char *root, const char *library, int include_root) {
    char mount[PATH_MAX], lib[PATH_MAX];
    /* USB mount names are fixed. A substituted mount or library symlink must
     * not turn an unrelated external directory into an authorized root. */
    if (!realpath(root, mount) || strcmp(mount, root)) return 0;
    if (include_root && canonical_under(path, mount)) return 1;
    if (!realpath(library, lib) || !canonical_under(lib, mount)) return 0;
    return canonical_under(path, lib);
}
int slot_validate_payload_path(const char *path, char *out_path, size_t out_size) {
    if (!out_path || !out_size) return -1;
    out_path[0] = 0;
    if (!path || path[0] != '/' || strstr(path, "..") || strstr(path, "/.") ||
        !is_supported_ext(path) || is_pldmgr_name(path)) return -1;
    char resolved[PATH_MAX]; struct stat st;
    if (!realpath(path, resolved) || !is_supported_ext(resolved) || is_pldmgr_name(resolved) ||
        strstr(resolved, "/.") || stat(resolved, &st) || !S_ISREG(st.st_mode) || st.st_size <= 0) return -1;
    int allowed = canonical_under(resolved, BASE_DATA_DIR);
    for (int i = 0; i < 8 && !allowed; i++) {
        char root[512], library[528];
        snprintf(root, sizeof(root), PLDMGR_USB_ROOT_FORMAT, i);
        snprintf(library, sizeof(library), "%s/" PLDMGR_USB_LIB_NAME, root);
        allowed = canonical_usb_path(resolved, root, library, config_read_bool("SCAN_USB_PAYLOADS", 0));
    }
    if (!allowed || strlen(resolved) >= out_size) return -1;
    strcpy(out_path, resolved);
    return 0;
}
/* Open the canonical path by walking directory descriptors. O_NOFOLLOW on
 * every component rejects a symlink substituted after canonical validation. */
static int open_without_symlinks(const char *path) {
    if (strlen(path) >= PATH_MAX) return -1;
    char walk[PATH_MAX]; strcpy(walk, path);
    int fd = open("/", O_RDONLY | O_DIRECTORY);
    if (fd < 0) return -1;
    char *state = NULL, *part = strtok_r(walk + 1, "/", &state);
    while (part) {
        char *next = strtok_r(NULL, "/", &state);
        int child = openat(fd, part, O_RDONLY | O_NOFOLLOW | (next ? O_DIRECTORY : O_NONBLOCK));
        int close_failed = close(fd);
        if (child < 0 || close_failed) { if (child >= 0) close(child); return -1; }
        fd = child; part = next;
    }
    struct stat st;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size <= 0) { close(fd); return -1; }
    return fd;
}

int slot_open_payload(const char *path, char *out_path, size_t out_size) {
    if (slot_validate_payload_path(path, out_path, out_size)) return -1;
    return open_without_symlinks(out_path);
}

int slot_open_self_update(const char *path) {
    const char *root = PAYLOADS_STORAGE_DIR "/";
    if (!path || strncmp(path, root, strlen(root))) return -1;
    const char *dir = path + strlen(root), *file = strchr(dir, '/');
    /* [CN-FORK] 库目录名按本 fork 约定是 nayeplmtools；旧名 pldmgr / payload-manager
     * 仍当 legacy 认（升级前留在盘上的目录）。 */
    if (!file) return -1;
    size_t lib_len = strlen(PLDMGR_USB_LIB_NAME);
    int lib_ok = ((size_t)(file - dir) == lib_len && !strncasecmp(dir, PLDMGR_USB_LIB_NAME, lib_len)) ||
                 ((file - dir) == 6 && !strncasecmp(dir, "pldmgr", 6)) ||
                 ((file - dir) == 15 && !strncasecmp(dir, "payload-manager", 15));
    if (!lib_ok) return -1;
    file++;
    if (strchr(file, '/') || strstr(file, "..") || !is_pldmgr_name(file) || !is_supported_ext(file)) return -1;
    int fd = open_without_symlinks(path);
    unsigned char magic[4];
    if (fd >= 0 && (pread(fd, magic, sizeof(magic), 0) != 4 || memcmp(magic, "\177ELF", 4))) {
        close(fd); return -1;
    }
    return fd;
}

int slot_resolve(const char *slot_or_path, char *out_path, size_t out_size) {
    if (!slot_or_path || !out_path || !out_size) return -1;
    out_path[0] = 0;
    if (slot_or_path[0] == '/') return slot_validate_payload_path(slot_or_path, out_path, out_size);
    if (is_pldmgr_name(slot_or_path)) return -1;
    if (!strncmp(slot_or_path, "repo/", 5) || !strncmp(slot_or_path, "local/", 6)) {
        if (!storage_key_valid(slot_or_path)) return -1;
        char dir[640]; SlotMeta meta;
        snprintf(dir, sizeof(dir), "%s/%s", PAYLOADS_STORAGE_DIR, slot_or_path);
        if (slot_read_meta(dir, &meta)) return -1;
        return slot_validate_payload_path(meta.payload_path, out_path, out_size);
    }
    /* Retain bare-filename compatibility, without interpreting arbitrary paths
     * as a request to launch some unrelated managed file with that basename. */
    if (strchr(slot_or_path, '/') || !is_supported_ext(slot_or_path)) return -1;
    SlotMeta *slots = NULL; size_t count = 0;
    if (slot_list_all(&slots, &count)) return -1;
    int rc = -1;
    for (size_t i = 0; i < count; i++) {
        if (!strcmp(slots[i].filename, slot_or_path)) {
            rc = slot_validate_payload_path(slots[i].payload_path, out_path, out_size); break;
        }
    }
    slot_free_all(slots);
    return rc;
}

static int slot_delete_unlocked(const char *slot_key) {
    if (!slot_key || slot_key[0] == '\0') return -1;
    if (strstr(slot_key, "..") || slot_key[0] == '/') return -1;

    char slot_dir[640];
    snprintf(slot_dir, sizeof(slot_dir), "%s/%s", PAYLOADS_STORAGE_DIR, slot_key);

    DIR *d = opendir(slot_dir);
    if (!d) return -1;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        char file_path[768];
        snprintf(file_path, sizeof(file_path), "%s/%s", slot_dir, ent->d_name);
        if (remove(file_path)) { closedir(d); return -1; }
    }
    if (closedir(d) || rmdir(slot_dir)) return -1;

    /* Remove from autoload.txt and favorites.txt */
    int autoload_result = pldmgr_autoload_update_config_entry(slot_key, NULL);
    int favorites_result = pldmgr_favorites_update_entry(slot_key, NULL);
    return autoload_result || favorites_result ? -1 : 0;
}

int slot_version_compare(const char *v1, const char *v2) {
    if (!v1 && !v2) return 0;
    if (!v1) return -1;
    if (!v2) return 1;

    /* 1. Trim whitespace */
    while (*v1 && isspace((unsigned char)*v1)) v1++;
    while (*v2 && isspace((unsigned char)*v2)) v2++;

    /* Drop leading v / V */
    if (*v1 == 'v' || *v1 == 'V') v1++;
    if (*v2 == 'v' || *v2 == 'V') v2++;

    const char *p1 = v1;
    const char *p2 = v2;

    while (*p1 || *p2) {
        /* Skip non-alphanumeric separators */
        while (*p1 && !isalnum((unsigned char)*p1)) p1++;
        while (*p2 && !isalnum((unsigned char)*p2)) p2++;

        if (!*p1 && !*p2) break;

        /* If one side ran out of tokens */
        if (!*p1) {
            /* Remaining token is letters -> pre-release -> lower -> v1 > v2 */
            if (isalpha((unsigned char)*p2)) return 1;
            /* Remaining token is digits -> sub-version -> higher -> v1 < v2 */
            return -1;
        }
        if (!*p2) {
            if (isalpha((unsigned char)*p1)) return -1;
            return 1;
        }

        int is_digit1 = isdigit((unsigned char)*p1);
        int is_digit2 = isdigit((unsigned char)*p2);

        /* Digit > Letter at same position */
        if (is_digit1 && !is_digit2) return 1;
        if (!is_digit1 && is_digit2) return -1;

        if (is_digit1 && is_digit2) {
            const char *s1 = p1;
            while (*p1 && isdigit((unsigned char)*p1)) p1++;
            size_t len1 = p1 - s1;

            const char *s2 = p2;
            while (*p2 && isdigit((unsigned char)*p2)) p2++;
            size_t len2 = p2 - s2;

            /* Strip leading zeros */
            while (len1 > 1 && *s1 == '0') { s1++; len1--; }
            while (len2 > 1 && *s2 == '0') { s2++; len2--; }

            if (len1 != len2) {
                return (len1 > len2) ? 1 : -1;
            }
            int cmp = memcmp(s1, s2, len1);
            if (cmp != 0) return cmp > 0 ? 1 : -1;
        } else {
            const char *s1 = p1;
            while (*p1 && isalpha((unsigned char)*p1)) p1++;
            size_t len1 = p1 - s1;

            const char *s2 = p2;
            while (*p2 && isalpha((unsigned char)*p2)) p2++;
            size_t len2 = p2 - s2;

            size_t min_len = len1 < len2 ? len1 : len2;
            for (size_t i = 0; i < min_len; i++) {
                int c1 = tolower((unsigned char)s1[i]);
                int c2 = tolower((unsigned char)s2[i]);
                if (c1 != c2) return c1 - c2;
            }
            if (len1 != len2) {
                return (len1 > len2) ? 1 : -1;
            }
        }
    }

    return 0;
}

#define LAYOUT_MARKER_PATH PAYLOADS_STORAGE_DIR "/.layout"
#define LEGACY_PATHS_MAP_PATH PAYLOADS_STORAGE_DIR "/.legacy_paths.json"

typedef struct MigrationMapEntry {
    char old_path[512];
    char old_filename[256];
    char slot_key[128];
} MigrationMapEntry;

/* [CN-FORK] v0.6.0.176 栈帧红线：这段「读配置 → 把旧载荷名换成 slot key → 回写」逻辑
 * 原本在 slot_migrate_legacy 里内联两份，各带一个 char lines[128][256]（32KB/份），
 * 加上 new_meta（~9.8KB）后整帧 42488 B。抽成独立函数并让 lines 走堆，
 * slot_migrate_legacy 的栈帧降到 ~26KB（还剩 new_meta + lp_buf[16384]）。
 * skip_delay_lines：autoload.txt 里有 `!1000` 延时行要跳过，favorites.txt 没有这种行。 */
static void remap_legacy_config_keys(const char *path, const MigrationMapEntry *map,
                                     size_t map_count, int skip_delay_lines) {
    FILE *f = fopen(path, "r");
    if (!f) return;
    char (*lines)[256] = malloc(128 * 256);
    if (!lines) { fclose(f); return; }

    int lcount = 0;
    while (lcount < 128 && fgets(lines[lcount], sizeof(lines[0]), f)) {
        lines[lcount][strcspn(lines[lcount], "\r\n")] = '\0';
        char *start = lines[lcount];
        while (*start && isspace((unsigned char)*start)) start++;
        char *end = start + strlen(start);
        while (end > start && isspace((unsigned char)*(end - 1))) end--;
        *end = '\0';
        if (start != lines[lcount]) memmove(lines[lcount], start, end - start + 1);
        lcount++;
    }
    fclose(f);

    int changed = 0;
    for (int i = 0; i < lcount; i++) {
        if (lines[i][0] == '\0') continue;
        if (skip_delay_lines && lines[i][0] == '!') continue;
        /* If line is already a slot key, leave it */
        if (strncmp(lines[i], "repo/", 5) == 0 || strncmp(lines[i], "local/", 6) == 0) continue;

        for (size_t m = 0; m < map_count; m++) {
            if (strcmp(lines[i], map[m].old_filename) == 0 ||
                strcmp(lines[i], map[m].old_path) == 0) {
                strncpy(lines[i], map[m].slot_key, sizeof(lines[0]) - 1);
                changed = 1;
                break;
            }
        }
    }

    if (changed) {
        f = fopen(path, "w");
        if (f) {
            for (int i = 0; i < lcount; i++) {
                fprintf(f, "%s\n", lines[i]);
            }
            fclose(f);
        }
    }
    free(lines);
}

int slot_migrate_legacy(void) {
    ensure_dir_recursive(PAYLOADS_STORAGE_DIR);

    /* Check layout marker */
    char *layout_content = NULL;
    size_t layout_size = 0;
    if (read_file_text(LAYOUT_MARKER_PATH, &layout_content, &layout_size) == 0 && layout_content) {
        if (atoi(layout_content) >= 2) {
            free(layout_content);
            return 0;
        }
        free(layout_content);
    }

    pldmgr_log("[PLDMGR] Starting one-time storage layout migration...\n");

    char repo_root[512], local_root[512];
    snprintf(repo_root, sizeof(repo_root), "%s/repo", PAYLOADS_STORAGE_DIR);
    snprintf(local_root, sizeof(local_root), "%s/local", PAYLOADS_STORAGE_DIR);
    ensure_dir_recursive(repo_root);
    ensure_dir_recursive(local_root);

    DIR *d = opendir(PAYLOADS_STORAGE_DIR);
    if (!d) return -1;

    MigrationMapEntry *map = NULL;
    size_t map_count = 0;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        if (strcmp(ent->d_name, "repo") == 0 || strcmp(ent->d_name, "local") == 0) continue;
        /* [CN-FORK] 自更新目录名：nayeplmtools（本 fork）+ 旧名 pldmgr / payload-manager */
        if (!strcasecmp(ent->d_name, PLDMGR_USB_LIB_NAME) ||
            !strcasecmp(ent->d_name, "pldmgr") ||
            !strcasecmp(ent->d_name, "payload-manager")) continue;

        char legacy_subdir[512];
        snprintf(legacy_subdir, sizeof(legacy_subdir), "%s/%s", PAYLOADS_STORAGE_DIR, ent->d_name);

        struct stat st;
        if (stat(legacy_subdir, &st) != 0 || !S_ISDIR(st.st_mode)) continue;

        DIR *sub = opendir(legacy_subdir);
        if (!sub) continue;

        struct dirent *sub_ent;
        while ((sub_ent = readdir(sub)) != NULL) {
            if (sub_ent->d_name[0] == '.') continue;
            if (!is_supported_ext(sub_ent->d_name)) continue;

            char old_file_path[640];
            snprintf(old_file_path, sizeof(old_file_path), "%s/%s", legacy_subdir, sub_ent->d_name);

            /* Read legacy sidecar if exists */
            char sidecar_path[700];
            snprintf(sidecar_path, sizeof(sidecar_path), "%s.json", old_file_path);

            SlotMeta new_meta;
            memset(&new_meta, 0, sizeof(new_meta));
            new_meta.schema = PAYLOAD_SLOT_SCHEMA;
            strncpy(new_meta.filename, sub_ent->d_name, sizeof(new_meta.filename) - 1);

            time_t now = time(NULL);
            struct tm tmv;
            localtime_r(&now, &tmv);
            strftime(new_meta.installed_at, sizeof(new_meta.installed_at), "%Y-%m-%dT%H:%M:%S%z", &tmv);

            char *sc_json = NULL;
            size_t sc_size = 0;
            char inst_src[128] = "", inst_detail[1024] = "";
            char sc_name[256] = "";

            if (read_file_text(sidecar_path, &sc_json, &sc_size) == 0 && sc_json) {
                const char *end = sc_json + sc_size;
                json_extract_string(sc_json, end, "install_source", inst_src, sizeof(inst_src));
                json_extract_string(sc_json, end, "install_source_detail", inst_detail, sizeof(inst_detail));
                json_extract_string(sc_json, end, "name", sc_name, sizeof(sc_name));
                json_extract_string(sc_json, end, "id", new_meta.id, sizeof(new_meta.id));
                json_extract_string(sc_json, end, "version", new_meta.version, sizeof(new_meta.version));
                json_extract_string(sc_json, end, "checksum", new_meta.checksum, sizeof(new_meta.checksum));
                json_extract_string(sc_json, end, "last_update", new_meta.last_update, sizeof(new_meta.last_update));
                json_extract_string(sc_json, end, "url", new_meta.url, sizeof(new_meta.url));
                json_extract_string(sc_json, end, "source", new_meta.source, sizeof(new_meta.source));
                json_extract_string(sc_json, end, "source_direct", new_meta.source_direct, sizeof(new_meta.source_direct));
                json_extract_string(sc_json, end, "category", new_meta.category, sizeof(new_meta.category));
                json_extract_string(sc_json, end, "description", new_meta.description, sizeof(new_meta.description));
                json_extract_string(sc_json, end, "author", new_meta.author, sizeof(new_meta.author));
                json_extract_string(sc_json, end, "details_url", new_meta.details_url, sizeof(new_meta.details_url));
                json_extract_string(sc_json, end, "source_name", new_meta.source_name, sizeof(new_meta.source_name));
                free(sc_json);
            }

            char target_slot[128] = "";
            if (strcmp(inst_src, "repository") == 0) {
                strcpy(new_meta.kind, SLOT_KIND_REPO);
                const char *surl = inst_detail[0] ? inst_detail : REPOSITORY_SOURCE_URL;
                if (is_legacy_official_repo_url(surl)) {
                    surl = REPOSITORY_SOURCE_URL;
                }
                strncpy(new_meta.source_url, surl, sizeof(new_meta.source_url) - 1);
                if (strcmp(new_meta.source_url, REPOSITORY_SOURCE_URL) == 0) {
                    strncpy(new_meta.source_name, "Official Repository", sizeof(new_meta.source_name) - 1);
                }
                if (sc_name[0]) {
                    strncpy(new_meta.name, sc_name, sizeof(new_meta.name) - 1);
                } else {
                    strncpy(new_meta.name, sub_ent->d_name, sizeof(new_meta.name) - 1);
                }
                if (new_meta.id[0] == '\0') {
                    slot_slug(new_meta.name, new_meta.id, sizeof(new_meta.id));
                }
                slot_repo_key(new_meta.id, new_meta.source_url, target_slot, sizeof(target_slot));
            } else {
                strcpy(new_meta.kind, SLOT_KIND_LOCAL);
                if (strcmp(inst_src, "usb") == 0) {
                    strcpy(new_meta.origin, "usb");
                    strncpy(new_meta.origin_detail, inst_detail, sizeof(new_meta.origin_detail) - 1);
                } else {
                    strcpy(new_meta.origin, "web_upload");
                }
                char lkey[128];
                slot_local_key(sub_ent->d_name, lkey, sizeof(lkey));
                slot_find_next_local_key(lkey, target_slot, sizeof(target_slot));
            }

            strncpy(new_meta.slot_key, target_slot, sizeof(new_meta.slot_key) - 1);

            char new_slot_dir[640];
            snprintf(new_slot_dir, sizeof(new_slot_dir), "%s/%s", PAYLOADS_STORAGE_DIR, target_slot);
            ensure_dir_recursive(new_slot_dir);

            char new_file_path[768];
            snprintf(new_file_path, sizeof(new_file_path), "%s/%s", new_slot_dir, sub_ent->d_name);

            /* Move payload file */
            if (rename(old_file_path, new_file_path) == 0) {
                slot_write_meta(new_slot_dir, &new_meta);
                remove(sidecar_path);

                /* Remove any .part or .tmp for this file */
                char part_path[700];
                snprintf(part_path, sizeof(part_path), "%s.part", old_file_path);
                remove(part_path);
                snprintf(part_path, sizeof(part_path), "%s.tmp", old_file_path);
                remove(part_path);

                /* Record in map */
                MigrationMapEntry *tmp = realloc(map, (map_count + 1) * sizeof(MigrationMapEntry));
                if (tmp) {
                    map = tmp;
                    strncpy(map[map_count].old_path, old_file_path, sizeof(map[map_count].old_path) - 1);
                    strncpy(map[map_count].old_filename, sub_ent->d_name, sizeof(map[map_count].old_filename) - 1);
                    strncpy(map[map_count].slot_key, target_slot, sizeof(map[map_count].slot_key) - 1);
                    map_count++;
                }

                pldmgr_log("[PLDMGR] Migrated: %s -> %s\n", old_file_path, target_slot);
            }
        }
        closedir(sub);

        /* Try removing legacy subdir if empty */
        rmdir(legacy_subdir);
    }
    closedir(d);

    /* Update autoload.txt */
    if (map && map_count > 0) {
        /* [CN-FORK] v0.6.0.176 两份重复的改写逻辑抽成 remap_legacy_config_keys()，
         * 大缓冲 lines[128][256] 也随之移出本函数栈帧（见该函数头注释）。 */
        remap_legacy_config_keys(AUTOLOAD_CONFIG_PATH, map, map_count, 1);
        remap_legacy_config_keys(FAVORITES_CONFIG_PATH, map, map_count, 0);

        /* Write legacy paths JSON file for frontend favorites remapping */
        char lp_buf[16384];
        JsonListBuilder jb = { lp_buf, sizeof(lp_buf), 0, 1, 0 };
        json_append(&jb, "{\n");
        for (size_t m = 0; m < map_count; m++) {
            char op_e[6 * 512], of_e[6 * 256], sk_e[6 * 128];
            json_escape_checked(&jb, map[m].old_path, op_e, sizeof(op_e));
            json_escape_checked(&jb, map[m].old_filename, of_e, sizeof(of_e));
            json_escape_checked(&jb, map[m].slot_key, sk_e, sizeof(sk_e));
            json_append(&jb, "  \"%s\": \"%s\",\n", op_e, sk_e);
            json_append(&jb, "  \"%s\": \"%s\"%s\n", of_e, sk_e, (m + 1 < map_count) ? "," : "");
        }
        json_append(&jb, "}\n");
        if (jb.error || write_file_text(LEGACY_PATHS_MAP_PATH, lp_buf, jb.pos) != 0) {
            free(map);
            return -1;
        }
        free(map);
    }

    /* Write layout marker 2 */
    write_file_text(LAYOUT_MARKER_PATH, "2\n", 2);
    pldmgr_log("[PLDMGR] Migration completed successfully. Layout v2 active.\n");
    return 0;
}

int slot_pop_legacy_paths_json(char *buf, size_t buf_size) {
    if (!buf || buf_size == 0) return -1;
    char *content = NULL;
    size_t size = 0;
    if (read_file_text(LEGACY_PATHS_MAP_PATH, &content, &size) == 0 && content) {
        if (size >= buf_size) { free(content); buf[0] = '\0'; return -2; }
        const char *end;
        int valid = json_find_object_end(content, content + size, &end) == 0;
        if (valid) {
            while (end < content + size && (*end == ' ' || *end == '\n' || *end == '\r' || *end == '\t')) end++;
            valid = end == content + size;
        }
        if (!valid) { free(content); buf[0] = '\0'; return -2; }
        memcpy(buf, content, size);
        buf[size] = '\0';
        free(content);
        remove(LEGACY_PATHS_MAP_PATH);
        return 0;
    }
    return -1;
}

int slot_read_meta(const char *slot_dir, SlotMeta *out_meta) {
    if (storage_lock()) return -1;
    int rc = slot_read_meta_unlocked(slot_dir, out_meta);
    storage_unlock();
    return rc;
}

int slot_write_meta(const char *slot_dir, const SlotMeta *meta) {
    if (storage_lock()) return -1;
    int rc = slot_write_meta_unlocked(slot_dir, meta);
    storage_unlock();
    return rc;
}

int slot_list_all(SlotMeta **out_slots, size_t *out_count) {
    if (storage_lock()) return -1;
    int rc = slot_list_all_unlocked(out_slots, out_count);
    storage_unlock();
    return rc;
}

int slot_delete(const char *slot_key) {
    if (storage_lock()) return -1;
    if (storage_recovery_required && slot_storage_recover_impl(0)) { storage_unlock(); return -1; }
    int rc = slot_delete_unlocked(slot_key);
    storage_unlock();
    return rc;
}
