#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "json_helpers.h"
#include "utils.h"
#include "history_mgr.h"
#include "log_server.h"
#include "payload_mgr.h"
#include "slot.h"
#include "sources.h"
#include "config.h"
#include "process_mgr.h"
#include "pldmgr.h"
#define LEGACY_PATHS_MAP_PATH PAYLOADS_STORAGE_DIR "/.legacy_paths.json"

static int format_failure;
int json_test_vsnprintf(char *buf, size_t size, const char *fmt, va_list args) {
    if (format_failure) return -1;
    return vsnprintf(buf, size, fmt, args);
}

static int failures, checks;
static void expect(int condition, const char *name) {
    checks++;
    if (!condition) { fprintf(stderr, "FAIL %s\n", name); failures++; }
}

static void emit(const char *output) {
    size_t size = 6 * strlen(output) + 1;
    char *escaped = malloc(size);
    assert(escaped && pldmgr_json_escape(output, escaped, size) == 0);
    printf("\nRESULT \"%s\"\n", escaped);
    free(escaped);
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    if (strcmp(argv[1], "builder") == 0) {
        for (size_t capacity = 0; capacity < 25; capacity++) {
            char guarded[32];
            memset(guarded, 'G', sizeof(guarded));
            JsonListBuilder jb = { guarded + 1, capacity, 0, 1, 0 };
            int first = json_append(&jb, "{\"value\":\"%s\"", "quoted");
            int last = json_append(&jb, "}");
            if (capacity <= strlen("{\"value\":\"quoted\"}")) {
                expect(first != 0 || last != 0, "capacity overflow is reported");
                expect(jb.pos == 0 && (!capacity || guarded[1] == 0), "overflow discards the document");
                expect(json_append(&jb, "{}") != 0 && jb.pos == 0, "failure is sticky");
            } else expect(strcmp(jb.buf, "{\"value\":\"quoted\"}") == 0, "exact complete document");
            expect(guarded[0] == 'G', "prefix guard untouched");
            for (size_t i = capacity + 1; i < sizeof(guarded); i++)
                expect(guarded[i] == 'G', "suffix guard untouched");
        }
#ifndef LEGACY_SIZE
        char output[32], escaped[2];
        JsonListBuilder jb = { output, sizeof(output), 0, 1, 0 };
        json_append(&jb, "{");
        expect(json_escape_checked(&jb, "\"", escaped, sizeof(escaped)) != 0 && jb.pos == 0,
               "escape overflow poisons builder");
        expect(json_format(output, 2, "{}") == 0 && output[0] == 0, "single-format overflow clears output");
        format_failure = 1;
        jb = (JsonListBuilder){ output, sizeof(output), 0, 1, 0 };
        expect(json_append(&jb, "{}") != 0 && output[0] == 0, "negative formatter result clears output");
#endif
    } else if (strcmp(argv[1], "history") == 0) {
        char path[512], output[65536];
        for (int i = 0; i < 20; i++) {
            memset(path, 1, sizeof(path) - 1);
            path[0] = (char)('A' + i); path[511] = 0;
            history_mgr_add(path);
        }
        size_t size = history_mgr_to_json(output, sizeof(output));
        expect(size > 8192 && size < sizeof(output), "full expanded history fits bounded buffer");
        emit(output);
        for (size_t capacity = 0; capacity < 40; capacity++) {
            char guard[42]; memset(guard, 'G', sizeof(guard));
            expect(history_mgr_to_json(guard + 1, capacity) == 0, "small history buffer returns failure");
            expect(!capacity || guard[1] == 0, "small history output cleared");
            expect(guard[capacity + 1] == 'G', "small history bound respected");
        }
    } else if (strcmp(argv[1], "logs") == 0) {
        char line[256], output[160000];
        memset(line, 1, sizeof(line) - 1); line[255] = 0;
        for (int i = 0; i < MAX_LOG_LINES; i++) pldmgr_log("%s", line);
        size_t size = log_build_json(output, sizeof(output));
        expect(size > 65536 && size < sizeof(output), "full expanded logs fit bounded buffer");
        emit(output);
#ifndef LEGACY_SIZE
        for (size_t capacity = 0; capacity < 40; capacity++) {
            char guard[42]; memset(guard, 'G', sizeof(guard));
            expect(log_build_json(guard + 1, capacity) == 0, "small log buffer returns failure");
            expect(!capacity || guard[1] == 0, "small log output cleared");
            expect(guard[capacity + 1] == 'G', "small log bound respected");
        }
#endif
    } else if (strcmp(argv[1], "payloads") == 0 || strcmp(argv[1], "payload_limit") == 0) {
        char *output = malloc(4194304);
        assert(output);
        size_t size = payload_mgr_list_json(output, 4194304);
        if (strcmp(argv[1], "payload_limit") == 0)
            expect(size == 0 && output[0] == 0, "library beyond 4 MiB fails completely");
        else {
            expect(size > 65536 && size < 4194304, "large library exceeds old 64 KiB capacity");
            emit(output);
        }
        expect(payload_mgr_list_json(output, 32) == 0 && output[0] == 0, "small library buffer fails completely");
        free(output);
    } else if (strcmp(argv[1], "processes") == 0) {
        char *output = malloc(4194304); assert(output);
        size_t size = process_list_json(output, 4194304);
        expect(size > 65536 && size < 4194304, "large process fixture is complete");
        emit(output);
        expect(process_list_json(output, 32) == 0 && output[0] == 0,
               "small process buffer fails completely");
        free(output);
    } else if (strcmp(argv[1], "repository") == 0 || strcmp(argv[1], "repository_limit") == 0) {
        char *output = malloc(4194304); assert(output);
        size_t size = sources_multi_repository_list_json(output, 4194304, 0);
        if (strcmp(argv[1], "repository_limit") == 0)
            expect(size == 0 && output[0] == 0, "repository beyond 4 MiB fails completely");
        else {
            expect(size > 65536 && size < 4194304, "repository exceeds old small-list capacity");
            emit(output);
            expect(sources_multi_repository_list_json(output, 32, 0) == 0 && output[0] == 0,
                   "small repository buffer fails completely");
        }
        free(output);
    } else if (strcmp(argv[1], "writers") == 0) {
        char path[512], meta_path[640];
        snprintf(path, sizeof(path), "%s/local/unchanged", PAYLOADS_STORAGE_DIR);
        ensure_dir_recursive(path);
        snprintf(meta_path, sizeof(meta_path), "%s/meta.json", path);
        const char *original = "{\"kind\":\"local\",\"filename\":\"safe.elf\"}";
        assert(write_file_text(meta_path, original, strlen(original)) == 0);
        SlotMeta meta = {0}; strcpy(meta.kind, "local"); strcpy(meta.filename, "changed.elf");
        format_failure = 1;
        expect(slot_write_meta(path, &meta) != 0, "format failure prevents metadata commit");
        char *saved; size_t size;
        assert(read_file_text(meta_path, &saved, &size) == 0);
        expect(strcmp(saved, original) == 0, "old metadata preserved on serialization failure"); free(saved);
        assert(write_file_text(SOURCES_CONFIG_PATH, "{\"sources\":[]}", 14) == 0);
        expect(sources_save("{\"sources\":[]}", 14) != 0, "format failure prevents source save");
        assert(read_file_text(SOURCES_CONFIG_PATH, &saved, &size) == 0);
        expect(strcmp(saved, "{\"sources\":[]}") == 0, "old sources preserved on serialization failure"); free(saved);
    } else if (strcmp(argv[1], "legacy_map") == 0) {
        char output[128];
        const char *map = "{\"old\":\"local/safe\"}";
        assert(write_file_text(LEGACY_PATHS_MAP_PATH, map, strlen(map)) == 0);
        expect(slot_pop_legacy_paths_json(output, 8) == -2, "oversized map is rejected");
        expect(access(LEGACY_PATHS_MAP_PATH, F_OK) == 0, "oversized map file retained");
        assert(write_file_text(LEGACY_PATHS_MAP_PATH, map, strlen(map)) == 0);
        expect(slot_pop_legacy_paths_json(output, sizeof(output)) == 0 && strcmp(output, map) == 0,
               "complete map returned");
        const char *invalid = "{\"old\":\"unterminated}";
        assert(write_file_text(LEGACY_PATHS_MAP_PATH, invalid, strlen(invalid)) == 0);
        expect(slot_pop_legacy_paths_json(output, sizeof(output)) == -2, "malformed map rejected");
        expect(access(LEGACY_PATHS_MAP_PATH, F_OK) == 0, "malformed map file retained");
    } else if (strcmp(argv[1], "favorites") == 0) {
        char output[4096]; int exists;
        FILE *f = fopen(FAVORITES_CONFIG_PATH, "w"); assert(f);
        fputs("  local/one \r\n\n local/two\t\n", f); fclose(f);
        expect(config_get_favorites_csv(output, sizeof(output), &exists) == 0 && exists &&
               strcmp(output, "local/one,local/two") == 0, "favorites trim and CRLF retained");
        f = fopen(FAVORITES_CONFIG_PATH, "w"); assert(f);
        for (int i = 0; i < 4095; i++) fputc('a', f); fclose(f);
        expect(config_get_favorites_csv(output, sizeof(output), &exists) == 0 && strlen(output) == 4095 &&
               strspn(output, "a") == 4095,
               "exact favorites limit preserved");
        f = fopen(FAVORITES_CONFIG_PATH, "a"); assert(f); fputc('a', f); fclose(f);
        expect(config_get_favorites_csv(output, sizeof(output), &exists) != 0 && output[0] == 0,
               "over-limit favorites rejected completely");
        f = fopen(FAVORITES_CONFIG_PATH, "w"); assert(f);
        for (int line = 0; line < 9; line++) {
            for (int i = 0; i < 511; i++) fputc('a', f);
            fputc('\n', f);
        }
        fclose(f);
        expect(config_get_favorites_csv(output, sizeof(output), &exists) != 0 && output[0] == 0,
               "accumulated favorites limit checked");
        f = fopen(FAVORITES_CONFIG_PATH, "w"); assert(f); fputc(0, f); fclose(f);
        expect(config_get_favorites_csv(output, sizeof(output), &exists) != 0 && output[0] == 0,
               "NUL favorites rejected");
    } else return 2;
    printf("JSON size checks: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
