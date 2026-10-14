#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "repository.h"
#include "slot.h"
#include "payload_mgr.h"
#include "collection_helpers.h"
#include "sources.h"

static size_t malloc_calls, realloc_calls, strdup_calls;
static size_t fail_malloc, fail_realloc, fail_strdup;
static int checks, failures;

void *allocation_test_malloc(size_t size) {
    if (++malloc_calls == fail_malloc) { errno = ENOMEM; return NULL; }
    return malloc(size);
}
void *allocation_test_realloc(void *pointer, size_t size) {
    if (++realloc_calls == fail_realloc) { errno = ENOMEM; return NULL; }
    return realloc(pointer, size);
}
char *allocation_test_strdup(const char *string) {
    if (++strdup_calls == fail_strdup) { errno = ENOMEM; return NULL; }
    return strdup(string);
}
static void reset(void) { malloc_calls = realloc_calls = strdup_calls = 0; fail_malloc = fail_realloc = fail_strdup = 0; }
static void expect(int condition, const char *message) {
    checks++;
    if (!condition) { failures++; fprintf(stderr, "FAIL %s\n", message); }
}
/* No test may download or launch anything. Fresh cache tests use other suites. */
int download_to_file(const char *url, const char *path) { (void)url; (void)path; abort(); }
void pldmgr_log(const char *fmt, ...) { (void)fmt; }

static char *manifest(size_t count) {
    char *json = malloc(count * 128 + 4);
    if (!json) exit(2);
    size_t pos = 0;
    json[pos++] = '[';
    for (size_t i = 0; i < count; i++)
        pos += (size_t)sprintf(json + pos, "%s{\"name\":\"item-%zu\",\"filename\":\"safe.elf\",\"url\":\"https://example.invalid\"}", i ? "," : "", i);
    strcpy(json + pos, "]");
    return json;
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    if (strcmp(argv[1], "repository") == 0) {
        char *json = manifest(500);
        RepoPayload *items; size_t count;
        reset();
        expect(parse_repository_payloads(json, &items, &count) == 0 && count == 500, "complete repository");
        size_t growths = realloc_calls, allocations = malloc_calls;
        expect(growths <= 6 && allocations == 0, "geometric parser growth without per-item malloc");
        free(items);
#ifdef LEGACY_ALLOCATION
        if (growths > 8) growths = 8;
#endif
        for (size_t failure = 1; failure <= growths; failure++) {
            reset(); fail_realloc = failure;
            items = NULL; count = 0;
            int result = parse_repository_payloads(json, &items, &count);
            expect(result != 0 && !items && !count, "each parser growth failure returns no partial collection");
            free(items);
        }
        free(json);
        reset(); json = manifest(collection_limit(sizeof(RepoPayload)) + 1);
        int result = parse_repository_payloads(json, &items, &count);
        expect(result != 0 && !items && !count, "repository item/byte limit rejects partial output"); free(items); free(json);
        reset(); json = malloc(16 * 1024 * 1024 + 2); memset(json, ' ', 16 * 1024 * 1024 + 1); json[16 * 1024 * 1024 + 1] = 0;
        result = parse_repository_payloads(json, &items, &count);
        expect(result != 0 && !items && !count, "repository input size bounded"); free(items); free(json);
        result = parse_repository_payloads("[{\"name\":\"valid\",\"filename\":\"safe.elf\",\"url\":\"https://example.invalid\"},{\"name\":\"unfinished", &items, &count);
        expect(result != 0 && !items && !count, "unterminated trailing object does not publish earlier items"); free(items);
        size_t capacity = 16;
        char *owned = malloc(16); memset(owned, 'x', 16);
        expect(collection_reserve(owned, &capacity, COLLECTION_MAX_ITEMS + 1, 1) == NULL && capacity == 16 && owned[0] == 'x',
               "item limit leaves ownership/capacity intact");
        expect(collection_reserve(owned, &capacity, 1, SIZE_MAX) == NULL && capacity == 16 && owned[0] == 'x',
               "byte/multiplication limit leaves ownership intact");
        free(owned);
    } else if (strcmp(argv[1], "slots") == 0) {
        SlotMeta *items; size_t count;
        reset(); expect(slot_list_all(&items, &count) == 0 && count == 70, "complete slot scan");
        size_t growths = realloc_calls, reads = malloc_calls;
        expect(growths == 3, "slot array grows geometrically"); free(items);
        for (size_t failure = 1; failure <= growths; failure++) {
            reset(); fail_realloc = failure;
            int result = slot_list_all(&items, &count);
            expect(result != 0 && !items && !count, "slot growth failure is not partial success"); free(items);
        }
        for (size_t failure = 1; failure <= reads; failure++) {
            reset(); fail_malloc = failure;
            int result = slot_list_all(&items, &count);
            expect(result != 0 && !items && !count, "metadata allocation failure aborts slot scan"); free(items);
        }
    } else if (strcmp(argv[1], "source_listing") == 0) {
        char *output = malloc(4194304); if (!output) return 2;
        reset(); expect(sources_multi_repository_list_json(output, 4194304, 0) != 0, "cached source listing complete");
        size_t growths = realloc_calls, allocations = malloc_calls;
#ifdef LEGACY_ALLOCATION
        if (growths > 8) growths = 8;
        if (allocations > 8) allocations = 8;
#endif
        for (size_t failure = 1; failure <= growths; failure++) {
            reset(); fail_realloc = failure;
            expect(sources_multi_repository_list_json(output, 4194304, 0) == 0 && output[0] == 0,
                   "parser allocation failure propagates through source listing");
        }
        for (size_t failure = 1; failure <= allocations; failure++) {
            reset(); fail_malloc = failure;
            expect(sources_multi_repository_list_json(output, 4194304, 0) == 0 && output[0] == 0,
                   "cache/icon allocation failure is not an empty successful source");
        }
        free(output);
    } else if (strcmp(argv[1], "usb") == 0) {
        char *output = malloc(4194304); if (!output) return 2;
        reset(); expect(payload_mgr_list_json(output, 4194304) != 0 && strdup_calls == 70, "all three USB scan paths collected");
        size_t growths = realloc_calls, copies = strdup_calls;
        for (size_t failure = 1; failure <= growths; failure++) {
            reset(); fail_realloc = failure;
            expect(payload_mgr_list_json(output, 4194304) == 0 && output[0] == 0, "USB growth failure cleans complete scan");
        }
        for (size_t failure = 1; failure <= copies; failure++) {
            reset(); fail_strdup = failure;
            expect(payload_mgr_list_json(output, 4194304) == 0 && output[0] == 0, "USB strdup failure cleans complete scan");
        }
        free(output);
    } else return 2;
    printf("Allocation tests (%s): %d checks, %d failures\n", argv[1], checks, failures);
    return failures ? 1 : 0;
}

int download_to_file_limited(const char *url, const char *path, size_t limit) {
    (void)limit; return download_to_file(url, path);
}
