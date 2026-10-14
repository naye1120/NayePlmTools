#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <microhttpd.h>
#include "http_server.h"
#include "pldmgr.h"
#include "log_server.h"

/* Keep regression comparisons against pre-cleanup source snapshots runnable. */
void http_request_completed(void *cls, struct MHD_Connection *connection,
                            void **context, enum MHD_RequestTerminationCode reason);
#pragma weak http_request_completed

static void complete_request(void *cls, struct MHD_Connection *connection,
                             void **context, enum MHD_RequestTerminationCode reason) {
    if (http_request_completed)
        http_request_completed(cls, connection, context, reason);
}

static int serving;
static int fail_malloc, fail_create, fail_queue, fail_cors;
static int fail_realloc, fail_mkstemp, fail_fdopen, fail_write, fail_flush, fail_close;
static char last_temp[1024];
static int last_fd = -1;
static unsigned int queued_status, cors_count;
static char body[256];
static _Atomic unsigned int created, destroyed;
static struct MHD_Response *outstanding[32];

struct Argument { const char *key; const char *value; };
struct Request {
    const char *url;
    const char *method;
    unsigned int status;
    struct Argument args[4];
};

void *http_test_malloc(size_t size) {
    if (fail_malloc) return NULL;
    return malloc(size);
}

void *http_test_realloc(void *pointer, size_t size) {
    return fail_realloc ? NULL : realloc(pointer, size);
}

int http_test_mkstemp(char *path) {
    if (fail_mkstemp) { errno = ENOSPC; return -1; }
    int fd = mkstemp(path);
    if (!serving) {
        snprintf(last_temp, sizeof(last_temp), "%s", path);
        last_fd = fd;
    }
    return fd;
}

FILE *http_test_fdopen(int fd, const char *mode) {
    return fail_fdopen ? NULL : fdopen(fd, mode);
}

size_t http_test_fwrite(const void *data, size_t size, size_t count, FILE *file) {
    return fail_write ? 0 : fwrite(data, size, count, file);
}

int http_test_fflush(FILE *file) {
    int result = fflush(file);
    return fail_flush ? EOF : result;
}

int http_test_fclose(FILE *file) {
    int result = fclose(file);
    return fail_close ? EOF : result;
}

static void remember(struct MHD_Response *response) {
    if (!response) return;
    created++;
    if (!serving) {
        for (size_t i = 0; i < 32; i++) {
            if (!outstanding[i]) { outstanding[i] = response; return; }
        }
        abort();
    }
}

struct MHD_Response *http_test_create_response(size_t size, void *buffer,
                                              enum MHD_ResponseMemoryMode mode) {
    if (fail_create) return NULL; /* MHD leaves the buffer with its caller on failure. */
    struct MHD_Response *response = MHD_create_response_from_buffer(size, buffer, mode);
    remember(response);
    if (!serving && response) {
        size_t n = size < sizeof(body) - 1 ? size : sizeof(body) - 1;
        if (n) memcpy(body, buffer, n);
        body[n] = '\0';
    }
    return response;
}

struct MHD_Response *http_test_create_callback(uint64_t size, size_t block_size,
    MHD_ContentReaderCallback reader, void *cls, MHD_ContentReaderFreeCallback cleanup) {
    if (fail_create) return NULL;
    struct MHD_Response *response = MHD_create_response_from_callback(
        size, block_size, reader, cls, cleanup);
    remember(response);
    return response;
}

static enum MHD_Result count_header(void *cls, enum MHD_ValueKind kind,
                                  const char *key, const char *value) {
    (void)cls; (void)kind;
    if (strcasecmp(key, "Access-Control-Allow-Origin") == 0) {
        assert(strcmp(value, "http://localhost") == 0 || strcmp(value, "*") == 0);
        cors_count++;
    }
    return MHD_YES;
}

enum MHD_Result http_test_add_header(struct MHD_Response *response,
                                    const char *name, const char *value) {
    if (fail_cors && strcmp(name, "Access-Control-Allow-Origin") == 0)
        return MHD_NO;
    return MHD_add_response_header(response, name, value);
}

enum MHD_Result http_test_queue(struct MHD_Connection *connection,
                               unsigned int status, struct MHD_Response *response) {
    assert(response); /* A failed response allocation must never be queued. */
    if (serving) return MHD_queue_response(connection, status, response);
    queued_status = status;
    cors_count = 0;
    MHD_get_response_headers(response, count_header, NULL);
    return fail_queue ? MHD_NO : MHD_YES;
}

void http_test_destroy(struct MHD_Response *response) {
    assert(response);
    if (!serving) {
        int found = 0;
        for (size_t i = 0; i < 32; i++) {
            if (outstanding[i] == response) { outstanding[i] = NULL; found = 1; break; }
        }
        assert(found); /* Detect double destruction. */
    }
    destroyed++;
    MHD_destroy_response(response);
}

const char *http_test_lookup(struct MHD_Connection *connection,
                            enum MHD_ValueKind kind, const char *key) {
    if (serving) return MHD_lookup_connection_value(connection, kind, key);
    struct Request *request = (struct Request *)connection;
    if (kind == MHD_HEADER_KIND) {
        if (!strcasecmp(key, "Host")) return "localhost";
        if (!strcasecmp(key, "Origin")) return "http://localhost";
        if (!strcasecmp(key, "X-PLDMGR-Request")) return "1";
    }
    for (size_t i = 0; i < 4; i++) {
        if (request->args[i].key && strcmp(request->args[i].key, key) == 0)
            return request->args[i].value;
    }
    return NULL;
}

static int failures, checks;

static void run_request(struct Request request, int oom, int creation_failure, int queue_failure) {
    created = destroyed = 0;
    queued_status = cors_count = 0;
    body[0] = '\0';
    fail_malloc = oom;
    fail_create = creation_failure;
    fail_queue = queue_failure;
    void *context = NULL;
    size_t upload_size = 0;
    enum MHD_Result result = http_on_request(NULL, (struct MHD_Connection *)&request,
        request.url, request.method, "HTTP/1.1", "", &upload_size, &context);
    if (strcmp(request.method, "OPTIONS") != 0 && result == MHD_YES && !queued_status) {
        result = http_on_request(NULL, (struct MHD_Connection *)&request,
            request.url, request.method, "HTTP/1.1", "", &upload_size, &context);
    }
    complete_request(NULL, (struct MHD_Connection *)&request, &context,
                     MHD_REQUEST_TERMINATED_COMPLETED_OK);
    unsigned int expected_status = oom ? MHD_HTTP_INTERNAL_SERVER_ERROR : request.status;
    int failed = (created != destroyed);
    if (creation_failure || fail_cors || (oom && strcmp(request.url, "/events") == 0)) {
        failed |= result != MHD_NO || queued_status != 0;
    } else {
        failed |= result != (queue_failure ? MHD_NO : MHD_YES);
        failed |= queued_status != expected_status || cors_count != 1;
        if (oom) failed |= strstr(body, "memory") == NULL;
    }
    checks++;
    if (failed) {
        fprintf(stderr, "FAIL %s %s [malloc=%d create=%d queue=%d]: result=%d status=%u cors=%u created=%u released=%u\n",
            request.method, request.url, oom, creation_failure, queue_failure,
            result, queued_status, cors_count, created, destroyed);
        failures++;
    }
    /* Permit running against the original leaking source without exhausting RAM. */
    for (size_t i = 0; i < 32; i++) {
        if (outstanding[i]) { MHD_destroy_response(outstanding[i]); outstanding[i] = NULL; }
    }
    fail_malloc = fail_create = fail_queue = 0;
}

static int unit_tests(int ownership_only, int oom_only) {
    extern int test_json_failure;
    struct Request requests[] = {
        {"/", "OPTIONS", 200, {{0}}},
        {"/", "GET", 200, {{0}}},
        {"/cache.appcache", "GET", 200, {{0}}},
        {"/favicon.svg", "GET", 200, {{0}}},
        {"/icon.png", "GET", 200, {{0}}},
        {"/update_check", "GET", 200, {{0}}},
        {"/manage:check", "GET", 400, {{0}}},
        {"/manage:check", "GET", 400, {{"filename", "../bad.elf"}}},
        {"/manage:check", "GET", 200, {{"filename", "test.elf"}}},
        {"/list_payloads", "GET", 200, {{0}}},
        {"/processes_list", "GET", 200, {{0}}},
        {"/history_list", "GET", 200, {{0}}},
        {"/process_kill", "POST", 400, {{0}}},
        {"/process_kill", "POST", 200, {{"pid", "123"}}},
        {"/process_kill", "POST", 500, {{"pid", "-1"}}},
        {"/repository_icons", "GET", 200, {{"source", "default"}}},
        {"/repository_cache_clear", "GET", 405, {{0}}},
        {"/repository_cache_clear", "POST", 200, {{0}}},
        {"/repository_payloads", "GET", 200, {{0}}},
        {"/repository_refresh", "POST", 200, {{0}}},
        {"/repository_install", "POST", 400, {{0}}},
        {"/repository_install", "POST", 200, {{"id", "test"}}},
        {"/repository_install", "POST", 200, {{"filename", "test.elf"}}},
        {"/sources_list", "GET", 200, {{0}}},
        {"/sources_add", "POST", 400, {{0}}},
        {"/sources_add", "POST", 200, {{"url", "http://example.test/repo.json"}}},
        {"/sources_remove", "POST", 400, {{0}}},
        {"/sources_remove", "POST", 200, {{"index", "1"}}},
        {"/usb_move_check", "GET", 400, {{0}}},
        {"/usb_move_check", "GET", 200, {{"path", "/mnt/usb0/test.elf"}}},
        {"/usb_move_perform", "POST", 400, {{0}}},
        {"/usb_move_perform", "POST", 200, {{"path", "/mnt/usb0/test.elf"}}},
        {"/loadpayload:missing.elf", "POST", 400, {{0}}},
        {"/loadpayload:pldmgr.elf", "POST", 400, {{0}}},
        {"/manage:delete", "POST", 400, {{0}}},
        {"/manage:delete", "POST", 200, {{"slot", "local/test"}}},
        {"/log", "GET", 200, {{0}}},
        {"/version", "GET", 200, {{0}}},
        {"/getip", "GET", 200, {{0}}},
        {"/autoload_status", "GET", 200, {{0}}},
        {"/autoload_clear", "POST", 200, {{0}}},
        {"/abort", "POST", 200, {{0}}},
        {"/get_config", "GET", 200, {{0}}},
        {"/events", "GET", 200, {{0}}},
        {"/set_config", "POST", 400, {{0}}},
        {"/sources_set", "POST", 400, {{0}}},
        {"/manage:upload", "POST", 400, {{0}}},
        {"/shutdown", "POST", 200, {{0}}},
    };
    for (size_t i = 0; i < sizeof(requests) / sizeof(requests[0]); i++) {
        if (oom_only) break;
        run_request(requests[i], 0, 0, 0);
        run_request(requests[i], 0, 0, 1);
        if (!ownership_only) {
            run_request(requests[i], 0, 1, 0);
            fail_cors = 1;
            run_request(requests[i], 0, 0, 0);
            fail_cors = 0;
        }
    }
    if (!ownership_only) {
        struct Request allocated[] = {
            {"/manage:check", "GET", 200, {{"filename", "test.elf"}}},
            {"/list_payloads", "GET", 200, {{0}}},
            {"/processes_list", "GET", 200, {{0}}},
            {"/history_list", "GET", 200, {{0}}},
            {"/repository_payloads", "GET", 200, {{0}}},
            {"/repository_refresh", "POST", 200, {{0}}},
            {"/repository_install", "POST", 200, {{"id", "test"}}},
            {"/sources_list", "GET", 200, {{0}}},
            {"/usb_move_check", "GET", 200, {{"path", "test"}}},
            {"/usb_move_perform", "POST", 200, {{"path", "test"}}},
            {"/log", "GET", 200, {{0}}},
            {"/autoload_status", "GET", 200, {{0}}},
            {"/get_config", "GET", 200, {{0}}},
            {"/events", "GET", 200, {{0}}},
            {"/set_config", "POST", 400, {{0}}},
            {"/sources_set", "POST", 400, {{0}}},
            {"/manage:upload", "POST", 400, {{0}}},
        };
        for (size_t i = 0; i < sizeof(allocated) / sizeof(allocated[0]); i++) {
            if (oom_only && strcmp(allocated[i].url, "/events") == 0) continue;
            run_request(allocated[i], 1, 0, 0);
            if (!oom_only) run_request(allocated[i], 1, 1, 0);
        }
    }
    if (!ownership_only && !oom_only) {
        test_json_failure = 1;
        struct Request invalid[] = {
            {"/manage:check", "GET", 500, {{"filename", "test.elf"}}},
            {"/list_payloads", "GET", 500, {{0}}},
            {"/processes_list", "GET", 500, {{0}}},
            {"/history_list", "GET", 500, {{0}}},
            {"/repository_payloads", "GET", 500, {{0}}},
            {"/repository_refresh", "POST", 500, {{0}}},
            {"/repository_install", "POST", 500, {{"id", "test"}}},
            {"/sources_list", "GET", 500, {{0}}},
            {"/usb_move_check", "GET", 500, {{"path", "test"}}},
            {"/usb_move_perform", "POST", 500, {{"path", "test"}}},
            {"/log", "GET", 500, {{0}}},
            {"/get_config", "GET", 500, {{0}}},
        };
        for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
            run_request(invalid[i], 0, 0, 0);
            if (!strstr(body, "error")) { failures++; fprintf(stderr, "FAIL incomplete JSON error document\n"); }
            run_request(invalid[i], 0, 0, 1);
            run_request(invalid[i], 0, 1, 0);
        }
        test_json_failure = 0;
    }
    printf("HTTP response tests: %d checks, %d failures (libmicrohttpd %s)\n",
           checks, failures, MHD_get_version());
    return failures ? 1 : 0;
}

extern unsigned int test_config_saves, test_source_saves, test_imports;
extern size_t test_post_size;

static void expect(int condition, const char *description) {
    checks++;
    if (!condition) {
        fprintf(stderr, "FAIL request lifecycle: %s\n", description);
        failures++;
    }
}

struct Transaction { struct Request request; void *context; };

static enum MHD_Result invoke(struct Transaction *transaction, const char *data, size_t size) {
    size_t remaining = size;
    enum MHD_Result result = http_on_request(NULL, (struct MHD_Connection *)&transaction->request,
        transaction->request.url, "POST", "HTTP/1.1", data, &remaining, &transaction->context);
    expect(!size || !remaining, "body chunk consumed");
    return result;
}

static struct Transaction begin(struct Request request) {
    fail_malloc = fail_create = fail_queue = fail_cors = 0;
    fail_realloc = fail_mkstemp = fail_fdopen = fail_write = fail_flush = fail_close = 0;
    queued_status = cors_count = 0;
    test_config_saves = test_source_saves = test_imports = 0;
    test_post_size = 0;
    last_temp[0] = '\0'; last_fd = -1;
    struct Transaction transaction = {request, NULL};
    invoke(&transaction, "", 0);
    return transaction;
}

static void finish(struct Transaction *transaction, enum MHD_RequestTerminationCode reason) {
    complete_request(NULL, (struct MHD_Connection *)&transaction->request,
                     &transaction->context, reason);
    expect(transaction->context == NULL, "completion clears the context");
    complete_request(NULL, (struct MHD_Connection *)&transaction->request,
                     &transaction->context, reason);
    expect(created == destroyed, "completion leaves no response references");
    if (last_temp[0]) expect(access(last_temp, F_OK) != 0, "owned temporary file removed");
    if (last_fd >= 0) expect(fcntl(last_fd, F_GETFD) == -1 && errno == EBADF, "upload descriptor closed");
}

static void request_tests(void) {
    int initial_checks = checks;
    struct Request upload = {"/manage:upload", "POST", 200, {{"filename", "test.elf"}}};
    for (int cancelled = 0; cancelled < 2; cancelled++) {
        struct Transaction transaction = begin(upload);
        expect(last_temp[0] && access(last_temp, F_OK) == 0, "upload owns a file");
        invoke(&transaction, "payload", 7);
        if (!cancelled) {
            invoke(&transaction, "", 0);
            expect(queued_status == 200 && test_imports == 1, "successful upload imports once");
            invoke(&transaction, "", 0);
            expect(test_imports == 1, "repeat callback does not import twice");
        }
        finish(&transaction, cancelled ? MHD_REQUEST_TERMINATED_CLIENT_ABORT : MHD_REQUEST_TERMINATED_COMPLETED_OK);
        if (cancelled) expect(test_imports == 0, "aborted upload never imports");
    }

    for (int fault = 0; fault < 6; fault++) {
        struct Transaction transaction = begin(upload);
        if (fault == 0) fail_write = 1;
        if (fault == 1) fail_flush = 1;
        if (fault == 2) fail_close = 1;
        if (fault == 3) fail_create = 1;
        if (fault == 4) fail_queue = 1;
        if (fault == 5) fail_cors = 1;
        invoke(&transaction, "payload", 7);
        if (!queued_status) invoke(&transaction, "", 0);
        if (fault < 3) {
            expect(queued_status == 500, "write/flush/close error is HTTP 500");
            expect(test_imports == 0, "file I/O error prevents import");
        }
        finish(&transaction, MHD_REQUEST_TERMINATED_WITH_ERROR);
    }

    struct Transaction committed = begin(upload);
    invoke(&committed, "payload", 7);
    invoke(&committed, "", 0);
    char reused_path[1024];
    snprintf(reused_path, sizeof(reused_path), "%s", last_temp);
    FILE *new_owner = fopen(reused_path, "w");
    assert(new_owner);
    fputs("new owner", new_owner);
    fclose(new_owner);
    last_temp[0] = '\0';
    finish(&committed, MHD_REQUEST_TERMINATED_COMPLETED_OK);
    expect(access(reused_path, F_OK) == 0, "successful import relinquishes the consumed temp pathname");
    remove(reused_path);

    for (int fault = 0; fault < 2; fault++) {
        struct Transaction transaction = begin((struct Request){"/version", "GET", 200, {{0}}});
        finish(&transaction, MHD_REQUEST_TERMINATED_COMPLETED_OK);
        if (fault == 0) fail_mkstemp = 1; else fail_fdopen = 1;
        transaction = (struct Transaction){upload, NULL};
        queued_status = 0;
        invoke(&transaction, "", 0);
        expect(queued_status == 500, "temporary-file creation/open failure is HTTP 500");
        finish(&transaction, MHD_REQUEST_TERMINATED_WITH_ERROR);
    }

    char length[32];
    snprintf(length, sizeof(length), "%zu", HTTP_UPLOAD_BODY_LIMIT + 1);
    struct Request too_large = upload;
    too_large.args[1] = (struct Argument){"Content-Length", length};
    struct Transaction transaction = begin(too_large);
    expect(queued_status == 413 && !last_temp[0], "oversized declared upload opens no file");
    finish(&transaction, MHD_REQUEST_TERMINATED_COMPLETED_OK);
    transaction = begin(upload);
    invoke(&transaction, "x", HTTP_UPLOAD_BODY_LIMIT + 1);
    expect(queued_status == 413 && !test_imports, "oversized chunk rejected before reading input");
    finish(&transaction, MHD_REQUEST_TERMINATED_WITH_ERROR);
    transaction = begin(upload);
    invoke(&transaction, "", 0);
    expect(queued_status == 400 && !test_imports, "empty upload cannot overwrite a payload");
    finish(&transaction, MHD_REQUEST_TERMINATED_COMPLETED_OK);

    const char *invalid_lengths[] = {"", "-1", "1x", "18446744073709551616"};
    for (size_t i = 0; i < sizeof(invalid_lengths) / sizeof(invalid_lengths[0]); i++) {
        struct Request invalid = upload;
        invalid.args[1] = (struct Argument){"Content-Length", invalid_lengths[i]};
        transaction = begin(invalid);
        expect(queued_status == 400 && !last_temp[0], "invalid declared length opens no file");
        finish(&transaction, MHD_REQUEST_TERMINATED_COMPLETED_OK);
    }
    char long_filename[257];
    memset(long_filename, 'a', sizeof(long_filename) - 1);
    long_filename[sizeof(long_filename) - 1] = '\0';
    struct Request invalid_name = upload;
    invalid_name.args[0].value = long_filename;
    transaction = begin(invalid_name);
    expect(queued_status == 400 && !last_temp[0], "overlong filename is rejected rather than truncated");
    finish(&transaction, MHD_REQUEST_TERMINATED_COMPLETED_OK);

    for (int sources = 0; sources < 2; sources++) {
        struct Request post = {sources ? "/sources_set" : "/set_config", "POST", 200, {{0}}};
        transaction = begin(post);
        invoke(&transaction, "{\"value\":", 9);
        invoke(&transaction, "true}", 5);
        invoke(&transaction, "", 0);
        expect(queued_status == 200 && test_post_size == 14, "POST chunks accumulated and saved");
        invoke(&transaction, "", 0);
        expect(test_config_saves + test_source_saves == 1, "POST saves only once");
        finish(&transaction, MHD_REQUEST_TERMINATED_COMPLETED_OK);

        transaction = begin(post);
        invoke(&transaction, "partial", 7);
        finish(&transaction, MHD_REQUEST_TERMINATED_CLIENT_ABORT);
        expect(test_config_saves + test_source_saves == 0, "aborted POST is not saved");

        transaction = begin(post);
        invoke(&transaction, "fail_test", 9);
        invoke(&transaction, "", 0);
        expect(queued_status == 500, "backend save failure is propagated");
        finish(&transaction, MHD_REQUEST_TERMINATED_COMPLETED_OK);

        transaction = begin(post);
        invoke(&transaction, "partial", 7);
        fail_realloc = 1;
        char data[5000] = {0};
        invoke(&transaction, data, sizeof(data));
        expect(queued_status == 500 && test_config_saves + test_source_saves == 0,
               "reallocation failure preserves context without saving partial data");
        finish(&transaction, MHD_REQUEST_TERMINATED_WITH_ERROR);

        transaction = begin(post);
        invoke(&transaction, "x", HTTP_POST_BODY_LIMIT + 1);
        expect(queued_status == 413 && test_config_saves + test_source_saves == 0,
               "oversized unknown-length POST rejected before copying input");
        finish(&transaction, MHD_REQUEST_TERMINATED_WITH_ERROR);
        snprintf(length, sizeof(length), "%zu", HTTP_POST_BODY_LIMIT + 1);
        post.args[0] = (struct Argument){"Content-Length", length};
        transaction = begin(post);
        expect(queued_status == 413, "oversized declared POST rejected initially");
        finish(&transaction, MHD_REQUEST_TERMINATED_COMPLETED_OK);

        post.args[0] = (struct Argument){NULL, NULL};
        char *boundary = malloc(HTTP_POST_BODY_LIMIT);
        assert(boundary);
        memset(boundary, ' ', HTTP_POST_BODY_LIMIT);
        boundary[HTTP_POST_BODY_LIMIT - 2] = '{';
        boundary[HTTP_POST_BODY_LIMIT - 1] = '}';
        transaction = begin(post);
        invoke(&transaction, boundary, HTTP_POST_BODY_LIMIT / 2);
        invoke(&transaction, boundary + HTTP_POST_BODY_LIMIT / 2, HTTP_POST_BODY_LIMIT / 2);
        invoke(&transaction, "", 0);
        expect(queued_status == 200 && test_post_size == HTTP_POST_BODY_LIMIT,
               "POST at the exact body limit is complete and terminated");
        finish(&transaction, MHD_REQUEST_TERMINATED_COMPLETED_OK);
        transaction = begin(post);
        invoke(&transaction, boundary, HTTP_POST_BODY_LIMIT);
        invoke(&transaction, "x", 1);
        expect(queued_status == 413 && test_config_saves + test_source_saves == 0,
               "cumulative POST limit prevents a partial save");
        finish(&transaction, MHD_REQUEST_TERMINATED_WITH_ERROR);
        free(boundary);
    }

    fail_malloc = fail_create = fail_queue = fail_cors = 0;
    fail_realloc = fail_mkstemp = fail_fdopen = fail_write = fail_flush = fail_close = 0;
    FILE *autoload = fopen(AUTOLOAD_CONFIG_PATH, "w");
    assert(autoload);
    fputs("\r\nlocal/one\r\n!500\nlocal/two\n", autoload);
    fclose(autoload);
    extern AutoloadSnapshot test_autoload_snapshot;
    strcpy(test_autoload_snapshot.list,"local/running");
    run_request((struct Request){"/autoload_status", "GET", 200, {{0}}}, 0, 0, 0);
    expect(strstr(body,"local/running") != NULL && !strstr(body,"local/one"),
           "status returns the worker snapshot rather than a newly edited sequence");
    run_request((struct Request){"/get_config", "GET", 200, {{0}}}, 0, 0, 0);
    expect(strstr(body, "local/one,!500,local/two") != NULL, "config retains delay entries");
    for (int size = 4095; size <= 4096; size++) {
        autoload = fopen(AUTOLOAD_CONFIG_PATH, "w");
        assert(autoload);
        for (int i = 0; i < size; i++) fputc('a', autoload);
        fclose(autoload);
        for (int config = 0; config < 2; config++) {
            run_request((struct Request){config ? "/get_config" : "/autoload_status", "GET",
                                        !config || size == 4095 ? 200 : 500, {{0}}}, 0, 0, 0);
        }
    }
    autoload = fopen(AUTOLOAD_CONFIG_PATH, "w");
    assert(autoload);
    fputs("local/one", autoload);
    fputc('\0', autoload);
    fputs("local/two", autoload);
    fclose(autoload);
    run_request((struct Request){"/get_config", "GET", 500, {{0}}}, 0, 0, 0);
    memset(test_autoload_snapshot.list,0,sizeof(test_autoload_snapshot.list));
    remove(AUTOLOAD_CONFIG_PATH);
    printf("HTTP request lifecycle: %d additional checks, %d total failures\n", checks - initial_checks, failures);
}

/* Test-only observability and browser fixture routes, never in the payload. */
static enum MHD_Result test_server_request(void *cls, struct MHD_Connection *conn,
    const char *url, const char *method, const char *version, const char *data,
    size_t *size, void **context) {
    if (!strcmp(url, "/__test_state")) {
        extern _Atomic unsigned int test_mutation_calls, test_active_calls;
        extern int http_keep_running;
        char json[256];
        snprintf(json, sizeof(json), "{\"mutations\":%u,\"active\":%u,\"running\":%d}",
            test_mutation_calls, test_active_calls, http_keep_running);
        struct MHD_Response *resp = http_test_create_response(strlen(json),json,MHD_RESPMEM_MUST_COPY);
        if (!resp) return MHD_NO;
        enum MHD_Result rc = MHD_queue_response(conn,200,resp);http_test_destroy(resp);return rc;
    }
    if (!strcmp(url, "/__test_browser") || !strcmp(url, "/__test_mutation.js")) {
        int script = !strcmp(url, "/__test_mutation.js");
        FILE *f=fopen(script ? "/src/frontend/src/utils/mutationRequest.js" : "/src/tests/browser_mutation_fixture.html","rb");if(!f)return MHD_NO;
        char text[16384];size_t n=fread(text,1,sizeof(text),f);fclose(f);
        struct MHD_Response *resp=http_test_create_response(n,text,MHD_RESPMEM_MUST_COPY);
        if(!resp)return MHD_NO;
        MHD_add_response_header(resp,"Content-Type",script ? "text/javascript" : "text/html");
        enum MHD_Result rc=MHD_queue_response(conn,200,resp);http_test_destroy(resp);return rc;
    }
    return http_on_request(cls,conn,url,method,version,data,size,context);
}

static struct MHD_Daemon *start_test_server(struct sockaddr_in *address) {
    return MHD_start_daemon(
        MHD_USE_THREAD_PER_CONNECTION | MHD_USE_INTERNAL_POLLING_THREAD,
        0, NULL, NULL, test_server_request, NULL,
        MHD_OPTION_NOTIFY_COMPLETED, complete_request, NULL,
        MHD_OPTION_SOCK_ADDR, address, MHD_OPTION_END);
}

static int serve(void) {
    serving = 1;
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(getenv("PLDMGR_TEST_PUBLIC") ? INADDR_ANY : INADDR_LOOPBACK);
    const char *fixed_port = getenv("PLDMGR_TEST_PORT");
    address.sin_port = htons(fixed_port ? (unsigned short)atoi(fixed_port) : 0);
#ifdef PLDMGR_TEST_REAL_LOGS
    log_stream_start();
#endif
    struct MHD_Daemon *daemon = start_test_server(&address);
    if (!daemon) return 1;
    const union MHD_DaemonInfo *info = MHD_get_daemon_info(daemon, MHD_DAEMON_INFO_BIND_PORT);
#ifdef PLDMGR_TEST_REAL_AUTOLOAD
    if (pldmgr_autoload_start()) { MHD_stop_daemon(daemon); return 1; }
#endif
    printf("PORT %u\n", info->port);
    fflush(stdout);
#ifdef PLDMGR_TEST_REAL_LOGS
    address.sin_port = htons(info->port);
    char command[64];
    while (fgets(command, sizeof(command), stdin)) {
        int first, count;
        if (sscanf(command, "emit %d %d", &first, &count) == 2) {
            assert(count >= 0 && count <= 200);
            for (int i = first; i < first + count; i++) pldmgr_log("entry %03d %0180d\n", i, i);
        } else if (!strcmp(command, "restart\n")) {
            log_stream_stop(); MHD_stop_daemon(daemon);
            log_stream_start(); daemon = start_test_server(&address); assert(daemon);
        } else break;
        puts("ACK"); fflush(stdout);
    }
    log_stream_stop();
#else
    (void)getchar();
#endif
#ifdef PLDMGR_TEST_REAL_AUTOLOAD
    pldmgr_autoload_stop();
#endif
    MHD_stop_daemon(daemon);
    printf("Application response references: created=%u released=%u\n", created, destroyed);
    return created == destroyed ? 0 : 1;
}

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "--serve") == 0) return serve();
    int selected = argc > 1;
    int result = unit_tests(selected && strcmp(argv[1], "--ownership-only") == 0,
                            selected && strcmp(argv[1], "--oom-only") == 0);
    if (!selected) request_tests();
    return result || failures ? 1 : 0;
}
