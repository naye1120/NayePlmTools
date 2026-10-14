#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "process_mgr.h"
#include "process_records.h"

static unsigned char bytes[4096];
static size_t length;
static const char *scenario;
static int queries, fetches, app_calls, kills;

void pldmgr_log(const char *format, ...) { (void)format; }
void *test_malloc(size_t size) { return !strcmp(scenario, "alloc-fail") ? NULL : malloc(size); }
int test_usleep(useconds_t usec) { (void)usec; return 0; }
static int dead_pid;
int test_kill(pid_t pid, int signal) {
    if (pid == 777777) {
        errno = ESRCH;
        return -1;
    }
    if (signal == 0) {
        if (pid == dead_pid) {
            errno = ESRCH;
            return -1;
        }
        return 0;
    }
    kills++;
    if (signal == SIGTERM) {
        if (pid != 888888) dead_pid = pid;
        return 0;
    }
    if (signal == SIGKILL) {
        dead_pid = pid;
        return 0;
    }
    return 0;
}
int sceKernelGetAppInfo(pid_t pid, void *info) {
    app_calls++;
    *(uint32_t *)info = pid == 42 ? 1 : 0;
    return pid == 43 ? -1 : 0;
}
int sysctl(const int *mib, unsigned int count, void *out, size_t *size, const void *input, size_t input_size) {
    (void)input; (void)input_size;
    assert(count == 4 && mib[0] == 1 && mib[1] == 14 && mib[2] == 8 && mib[3] == 0);
    if (!out) {
        queries++;
        if (!strcmp(scenario, "query-fail")) { errno = EIO; return -1; }
        *size = !strcmp(scenario, "query-large") ? 16u * 1024u * 1024u + 1 : length;
        if (!strcmp(scenario, "grow") && queries == 1) *size = 4;
        return 0;
    }
    fetches++;
    if (!strcmp(scenario, "fetch-fail")) { errno = EIO; return -1; }
    if (!strcmp(scenario, "grow-always") || (!strcmp(scenario, "grow") && fetches == 1)) {
        *size = length + 100; errno = ENOMEM; return -1;
    }
    assert(*size >= length);
    memcpy(out, bytes, length);
    *size = length + (!strcmp(scenario, "returned-large") ? 1 : 0);
    return 0;
}
static void add_record(int size, pid_t pid, const char *name, int64_t rss) {
    assert(size >= 467 && length + (size_t)size <= sizeof(bytes));
    unsigned char *start = bytes + length;
    memcpy(start, &size, 4);
    memcpy(start + 72, &pid, 4);
    memcpy(start + 264, &rss, 8);
    memcpy(start + 447, name, strlen(name) + 1);
    length += (size_t)size;
}
int main(int argc, char **argv) {
    assert(argc == 2); scenario = argv[1];
    if (!strcmp(scenario, "empty")) length = 0;
    else if (!strcmp(scenario, "short-header")) length = 3;
    else if (!strcmp(scenario, "zero")) {
        add_record(467, 41, "pldmgr.elf", 0); memset(bytes, 0, 4);
    }
    else if (!strcmp(scenario, "negative") ||
             !strcmp(scenario, "short") || !strcmp(scenario, "short-list") || !strcmp(scenario, "oversized")) {
        int size = !strcmp(scenario, "zero") ? 0 : !strcmp(scenario, "negative") ? -1 :
                   !strcmp(scenario, "oversized") ? INT_MAX : 4;
        memcpy(bytes, &size, 4); length = 4;
    } else {
        add_record(467, getpid(), "pldmgr.elf", 100);
        add_record(1088, 41, "pldmgr.elf", 1024);
        add_record(469, 42, "game.elf", 2048);
        add_record(467, 43, "mini-syscore.elf", 0);
        add_record(467, 44, "a\"\\\n.elf", 1);
        if (!strcmp(scenario, "unterminated")) memset(bytes + 447, 'X', 20);
        if (!strcmp(scenario, "negative-rss")) { int64_t rss = -1; memcpy(bytes + 264, &rss, 8); }
        if (!strcmp(scenario, "huge-rss")) { int64_t rss = INT64_MAX; memcpy(bytes + 264, &rss, 8); }
        if (!strcmp(scenario, "max-name")) memcpy(bytes + 447, "abcdefghijklmno.elf", 20);
        if (!strcmp(scenario, "trailing")) bytes[length++] = 1;
        if (!strcmp(scenario, "bad-after-match")) { memset(bytes + length, 0, 4); length += 4; }
        if (!strcmp(scenario, "nonpositive")) { pid_t pid = -5; memcpy(bytes + 467 + 72, &pid, 4); }
        if (!strcmp(scenario, "thread-only")) {
            memcpy(bytes + 467 + 394, "pldmgr.elf", 11);
            memcpy(bytes + 467 + 447, "other.elf\0", 10);
        }
    }
    pid_t found = !strcmp(scenario, "short-list") ? -1 : process_find_pid("pldmgr.elf");
    int lookup_queries = queries, lookup_fetches = fetches;
    queries = fetches = 0;
    char output[8192]; memset(output, 'X', sizeof(output));
    size_t used = process_list_json(output, !strcmp(scenario, "small-output") ? 32 : sizeof(output));
    assert(used ? output[used] == '\0' : output[0] == '\0');
    dead_pid = 0; kills = 0;
    assert(process_kill(0) == -1 && process_kill(-1) == -1 && process_kill(getpid()) == -1 && kills == 0);
    assert(process_kill(999999) == 0 && kills == 1);
    if (!strcmp(scenario, "normal")) {
        dead_pid = 0; kills = 0;
        assert(process_kill(888888) == 0 && kills == 2);
        dead_pid = 0; kills = 0;
        assert(process_kill(777777) == -1 && kills == 0);
    }
    printf("{\"found\":%d,\"length\":%zu,\"queries\":%d,\"fetches\":%d,\"lookup_queries\":%d,\"lookup_fetches\":%d,\"app_calls\":%d,\"data\":%s}\n",
           (int)found, used, queries, fetches, lookup_queries, lookup_fetches, app_calls, used ? output : "null");
}
