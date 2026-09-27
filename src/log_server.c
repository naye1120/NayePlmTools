#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <pthread.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h> /* [CN-FORK] 日志落盘需要 mkdir */

#include "log_server.h"
#include "utils.h"

/* Ring buffer state */
static char log_buffer[MAX_LOG_LINES][MAX_LOG_LINE_LEN];
static int log_head = 0;
static int log_count = 0;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t log_cond = PTHREAD_COND_INITIALIZER;
static int log_updated = 0;

static volatile int server_active_flag = 0;

/* [CN-FORK] 日志落盘：实机死机排查之前完全没有证据可查 —— 日志只进内存环形缓冲 + SSE，
 * 一断电全没。这里追加写 /data/nayeplmtools/pldmgr.log 并带时间戳，每行 fflush，
 * 保证死机前最后一条还在。调用方已持 log_mutex，log_fp 的并发由它串行。 */
#define PLDMGR_LOG_PATH "/data/nayeplmtools/pldmgr.log"
#define PLDMGR_LOG_MAX_BYTES (512 * 1024)
static FILE *log_fp = NULL;
static long  log_bytes = 0;
static int   log_dir_tried = 0;

static void log_append_file(const char *line) {
    if (log_fp && log_bytes > PLDMGR_LOG_MAX_BYTES) {
        fclose(log_fp);
        log_fp = NULL;
        log_bytes = 0;
    }
    if (!log_fp) {
        if (!log_dir_tried) {
            log_dir_tried = 1;
            mkdir("/data/nayeplmtools", 0777);
        }
        log_fp = fopen(PLDMGR_LOG_PATH, log_bytes ? "a" : "w");
        if (!log_fp) return;
    }

    char ts[32] = "";
    time_t now = time(NULL);
    struct tm tmv;
    if (localtime_r(&now, &tmv) != NULL)
        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tmv);

    int w = fprintf(log_fp, "[%s] %s\n", ts, line);
    if (w > 0) log_bytes += w;
    fflush(log_fp);
}

void log_server_set_active(void) { server_active_flag = 1; }
int pldmgr_server_is_active(void) { return server_active_flag; }

void pldmgr_log(const char *fmt, ...) {
    char line[MAX_LOG_LINE_LEN];
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);

    /* Print to stdout */
    printf("%s", line);

    /* Remove trailing newline for internal storage if present */
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
        line[len - 1] = '\0';
        len--;
    }

    if (len == 0)
        return;

    /* Add to circular buffer with lock */
    pthread_mutex_lock(&log_mutex);
    strncpy(log_buffer[log_head], line, MAX_LOG_LINE_LEN);
    log_head = (log_head + 1) % MAX_LOG_LINES;
    if (log_count < MAX_LOG_LINES)
        log_count++;

    /* Signal SSE clients */
    log_updated++;
    pthread_cond_broadcast(&log_cond);
    log_append_file(line); /* [CN-FORK] 同一把锁下写盘，保证死机前最后一条已落盘 */
    pthread_mutex_unlock(&log_mutex);
}

ssize_t log_stream_callback(void *cls, uint64_t pos, char *buf, size_t max) {
    struct LogSSEConnection *conn = (struct LogSSEConnection *)cls;

    pthread_mutex_lock(&log_mutex);

    /* Initial catch-up */
    if (!conn->sent_initial) {
        char initial_batch[MAX_LOG_LINES * (MAX_LOG_LINE_LEN + 16)];
        size_t offset = 0;

        for (int i = 0; i < log_count; i++) {
            int idx = (log_head - log_count + i + MAX_LOG_LINES) % MAX_LOG_LINES;
            offset += snprintf(initial_batch + offset, sizeof(initial_batch) - offset,
                               "data: %s\n\n", log_buffer[idx]);
        }

        conn->sent_initial = 1;
        conn->last_log_version = log_updated;

        if (offset > 0) {
            size_t to_copy = (offset < max) ? offset : max;
            memcpy(buf, initial_batch, to_copy);
            pthread_mutex_unlock(&log_mutex);
            return to_copy;
        }
    }

    /* Wait for new logs */
    while (conn->last_log_version == log_updated) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 5; /* Keep-alive ping interval */

        if (pthread_cond_timedwait(&log_cond, &log_mutex, &ts) == ETIMEDOUT) {
            /* Send a comment keep-alive */
            const char *ping = ": ping\n\n";
            memcpy(buf, ping, strlen(ping));
            pthread_mutex_unlock(&log_mutex);
            return strlen(ping);
        }
    }

    /* Send new log */
    int idx = (log_head - 1 + MAX_LOG_LINES) % MAX_LOG_LINES;
    size_t len = snprintf(buf, max, "data: %s\n\n", log_buffer[idx]);
    conn->last_log_version = log_updated;

    pthread_mutex_unlock(&log_mutex);
    return len;
}

void log_stream_cleanup(void *cls) { free(cls); }

size_t log_build_json(char *buf, size_t buf_size) {
    size_t bpos = 0;
    bpos += snprintf(buf + bpos, buf_size - bpos, "{\"logs\":[");

    pthread_mutex_lock(&log_mutex);
    for (int i = 0; i < log_count; i++) {
        int idx = (log_head - log_count + i + MAX_LOG_LINES) % MAX_LOG_LINES;
        char escaped[MAX_LOG_LINE_LEN * 2];
        pldmgr_json_escape(log_buffer[idx], escaped, sizeof(escaped));
        bpos += snprintf(buf + bpos, buf_size - bpos,
                         "\"%s\"%s", escaped,
                         (i == log_count - 1) ? "" : ",");
    }
    pthread_mutex_unlock(&log_mutex);

    bpos += snprintf(buf + bpos, buf_size - bpos, "]}");
    return bpos;
}
