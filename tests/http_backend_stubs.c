/* Hardware/services are replaced; routing and response ownership are real. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include "pldmgr.h"
#include "config.h"
#include "payload_mgr.h"
#include "repository.h"
#include "sources.h"
#include "history_mgr.h"
#include "process_mgr.h"
#include "ps5_launcher.h"
#include "slot.h"
#include "log_server.h"

int test_json_failure;
_Atomic unsigned int test_mutation_calls, test_active_calls;
static size_t json(char *out, size_t size, const char *value) {
    if (test_json_failure) { if (size) out[0] = 0; return 0; }
    int n = snprintf(out, size, "%s", value);
    return n < 0 ? 0 : (size_t)n;
}

unsigned int test_config_saves, test_source_saves, test_imports;
size_t test_post_size;

#ifndef PLDMGR_TEST_REAL_LOGS
void pldmgr_log(const char *fmt, ...) { (void)fmt; }
void log_server_set_active(void) { test_active_calls++; }
#endif
void pldmgr_notify(const char *fmt, ...) { (void)fmt; }
#ifndef PLDMGR_TEST_REAL_CONFIG
void config_read(PldmgrConfig *cfg) { memset(cfg, 0, sizeof(*cfg)); cfg->autoload_delay = 5; }
int config_read_bool(const char *key, int fallback) { (void)key; return fallback; }
int config_handle_set_json(const char *data) {
    test_config_saves++; test_mutation_calls++;
    test_post_size = strlen(data);
    return strcmp(data, "fail_test") == 0 ? -1 : (strcmp(data, "invalid_test") == 0 ? CONFIG_INVALID : 0);
}
int config_get_favorites_csv(char *out, size_t size, int *exists) {
    if (size) out[0] = '\0';
    *exists = 0;
    return test_json_failure ? -1 : 0;
}
#endif
AutoloadSnapshot test_autoload_snapshot={.state=AUTOLOAD_DISABLED,.remaining=-1,.remaining_ms=-1000,.delay=5};
#ifndef PLDMGR_TEST_REAL_AUTOLOAD
void pldmgr_autoload_frontend_ready(void) {}
void pldmgr_autoload_get_snapshot(AutoloadSnapshot *snapshot) {
    *snapshot=test_autoload_snapshot;
}
const char *pldmgr_autoload_state_name(AutoloadState state) { (void)state; return "disabled"; }
void pldmgr_autoload_get_status(int *total, int *done, char *current) {
    *total = 0; *done = 0; current[0] = '\0';
}
int pldmgr_autoload_get_remaining_seconds(void) { return -1; }
long long pldmgr_autoload_get_remaining_ms(void) { return -1000; }
void pldmgr_autoload_reset(void) { test_mutation_calls++; }
void pldmgr_autoload_abort(void) { test_mutation_calls++; }
#endif
#ifndef PLDMGR_TEST_REAL_LOGS
int pldmgr_server_is_active(void) { return test_active_calls != 0; }
#endif
int slot_resolve(const char *key, char *out, size_t size) {
    test_mutation_calls++; (void)key; (void)out; (void)size; return -1;
}
int slot_local_key(const char *name, char *out, size_t size) {
    snprintf(out, size, "%s", name); return 0;
}
int ps5_launch_elf(const char *path) { (void)path; abort(); }
int ps5_launch_elf_cancellable(const char *path, PayloadLaunchCancelled cancelled, void *context) { (void)path; (void)cancelled; (void)context; abort(); }
int payload_mgr_delete_payload(const char *key) { (void)key; test_mutation_calls++; return 0; }
int payload_mgr_import_to_storage_mode(const char *filename, const char *path,
    const char *mode, const char *origin, const char *detail,
    char *msg, size_t msg_size, char *slot, size_t slot_size) {
    (void)filename; (void)path; (void)mode; (void)origin; (void)detail;
    snprintf(msg, msg_size, "Test import"); snprintf(slot, slot_size, "local/test");
    test_imports++; test_mutation_calls++;
    if (mode && strcmp(mode, "fail_test") == 0) return -1;
    char destination[1024];
    snprintf(destination, sizeof(destination), "%s/%s", PAYLOADS_STORAGE_DIR, filename);
    return rename(path, destination);
}
int payload_mgr_check_existing(const char *filename, char *out, size_t size) {
    (void)filename; json(out, size, "{\"status\":\"ready\"}"); return 0;
}
int payload_mgr_usb_check(const char *path, char *out, size_t size) {
    (void)path; json(out, size, "{\"status\":\"ready\"}"); return 0;
}
int payload_mgr_usb_move(const char *path, const char *mode, int keep, char *out, size_t size) {
    test_mutation_calls++; (void)path; (void)mode; (void)keep; json(out, size, "{\"ok\":true}"); return 0;
}
size_t payload_mgr_list_json(char *out, size_t size) { return json(out, size, "{\"items\":[]}"); }
size_t process_list_json(char *out, size_t size) { return json(out, size, "{\"processes\":[]}"); }
int process_kill(int pid) { test_mutation_calls++; return pid == 123 ? 0 : -1; }
size_t history_mgr_to_json(char *out, size_t size) { return json(out, size, "{\"history\":[]}"); }
size_t repository_list_json(char *out, size_t size, int refresh) {
    if (refresh) test_mutation_calls++; return json(out, size, "{\"payloads\":[]}");
}
size_t sources_multi_repository_list_json(char *out, size_t size, int refresh) {
    return repository_list_json(out, size, refresh);
}
int sources_repo_install(const char *url, const char *id, const char *mode,
                        const char *replace, char *out, size_t size) {
    test_mutation_calls++; (void)url; (void)id; (void)mode; (void)replace; json(out, size, "{\"ok\":true}"); return 0;
}
int sources_list_json(char *out, size_t size) { json(out, size, "{\"sources\":[]}"); return test_json_failure ? -1 : 0; }
int sources_save(const char *data, size_t size) {
    test_source_saves++; test_mutation_calls++;
    test_post_size = size;
    return strcmp(data, "fail_test") == 0 ? -1 : (strcmp(data, "invalid_test") == 0 ? CONFIG_INVALID : 0);
}
int sources_add(const char *url, char *msg, size_t size) {
    test_mutation_calls++; (void)url; snprintf(msg, size, "Test source"); return 0;
}
int sources_remove(int index, char *msg, size_t size) {
    test_mutation_calls++; (void)index; snprintf(msg, size, "Removed"); return 0;
}
#ifndef PLDMGR_TEST_REAL_LOGS
size_t log_build_json(char *out, size_t size) { return json(out, size, "{\"logs\":[]}"); }
ssize_t log_stream_callback(void *cls, uint64_t pos, char *out, size_t max) {
    (void)cls; (void)pos; (void)out; (void)max; return MHD_CONTENT_READER_END_OF_STREAM;
}
void log_stream_init(struct LogSSEConnection *conn, const char *id) {
    (void)id; memset(conn, 0, sizeof(*conn));
}
void log_stream_cleanup(void *cls) { free(cls); }
#endif

int sources_clear_cache(void) { test_mutation_calls++; return 0; }
size_t sources_icons_json(const char *id, char *buf, size_t size) { (void)id; return json(buf, size, "{\"icons\":{}}"); }
