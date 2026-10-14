#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "sources.h"
#include "config.h"
#include "json_helpers.h"
#include "pldmgr.h"
void config_read(PldmgrConfig *config) { memset(config, 0, sizeof(*config)); config->multi_sources_enabled = 1; }
int config_write_last_update(long value) { (void)value; return 0; }
/* [CN-FORK] v0.6.0.176 fork 的 sources.c default_source() 会读 DEFAULT_REPO_ENABLED
 * （上游不读），本 runner 不链 config.c，故补一个与 json_backend_driver 相同的桩。 */
int config_read_bool(const char *key, int fallback) { (void)key; return fallback; }
int download_to_file_limited(const char *url, const char *destination, size_t limit) {
    const char *record = getenv("ICON_TEST_RECORD");
    FILE *stream = fopen(record, "a"); if (!stream) return -1;
    fprintf(stream, "%s\n", url); fclose(stream);
    if (getenv("ICON_TEST_FAIL_CATALOG") && !strcmp(url, REPOSITORY_SOURCE_URL)) return -1;
    if (getenv("ICON_TEST_FAIL") && strcmp(url, REPOSITORY_SOURCE_URL)) return -1;
    const char *fixture = getenv(!strcmp(url, REPOSITORY_SOURCE_URL) ? "ICON_TEST_CATALOG" : "ICON_TEST_ICONS");
    char *text = NULL; size_t size = 0;
    if (!fixture || read_file_text(fixture, &text, &size) || size > limit) { free(text); return -1; }
    int result = write_file_text(destination, text, size); free(text); return result;
}
static void *cache_worker(void *context) {
    const char *operation = context;
    char *buffer = malloc(4 * 1024 * 1024);
    if (!buffer) return (void *)1;
    int error = 0;
    for (int i = 0; i < 30; i++) {
        if (!strcmp(operation, "list")) error |= !sources_multi_repository_list_json(buffer, 4 * 1024 * 1024, 0);
        else if (!strcmp(operation, "clear")) error |= sources_clear_cache() != 0;
        else sources_icons_json("default", buffer, 4 * 1024 * 1024);
    }
    free(buffer); return error ? (void *)1 : NULL;
}
int main(int argc, char **argv) {
    if (argc < 2) return 2;
    if (!strcmp(argv[1], "concurrent")) {
        pthread_t threads[3]; const char *operations[] = {"list", "clear", "icons"};
        for (int i = 0; i < 3; i++) if (pthread_create(&threads[i], NULL, cache_worker, (void *)operations[i])) return 2;
        int error = 0;
        for (int i = 0; i < 3; i++) { void *result; if (pthread_join(threads[i], &result) || result) error = 1; }
        return error;
    }
    if (!strcmp(argv[1], "configure") && argc > 2) return sources_save(argv[2], strlen(argv[2])) ? 1 : 0;
    if (!strcmp(argv[1], "clear")) return sources_clear_cache() ? 1 : 0;
    char *output = malloc(4 * 1024 * 1024); if (!output) return 2;
    size_t size = !strcmp(argv[1], "icons") ? sources_icons_json(argc > 2 ? argv[2] : "default", output, 4 * 1024 * 1024) :
        sources_multi_repository_list_json(output, 4 * 1024 * 1024, argc > 2);
    if (size) puts(output);
    free(output); return size ? 0 : 1;
}
